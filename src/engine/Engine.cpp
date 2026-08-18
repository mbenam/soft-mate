#include "EngineStateUpdater.h"
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace m8 {
namespace engine {

Engine::Engine(CommandRing<EngineCommand, 1024>& commandRing) 
    : m_commandRing(commandRing) 
{
    initChorus();
    m_delayL.Init();
    m_delayR.Init();
    m_reverb.Init(kSampleRate);
    recalcBPM();
}

// The two chorus engines differ only in their base delay, and that difference is
// the whole reason the return has a stereo image. Two identical comb filters on
// the same input give identical output no matter how they are panned, which is
// exactly the trap daisysp::Chorus falls into.
//
// Left keeps ChorusEngine::Init's own 0.75 (6.03 ms), so for centred material the
// left channel is bit-for-bit what this path produced before. Right is offset to
// 0.45 (3.65 ms) and is the channel that changes.
//
// CHOICE, not a measurement -- like the phaser sweep range and the flanger delay
// range, in the same approximation class. The offset is within the 1-10 ms window
// a chorus conventionally uses. The two LFO phases stay locked because DaisySP
// exposes no way to offset them; the delay difference is what decorrelates the
// two channels.
static constexpr float kChorusDelayL = 0.75f;
static constexpr float kChorusDelayR = 0.45f;

void Engine::initChorus() {
    m_chorusL.Init(kSampleRate);
    m_chorusR.Init(kSampleRate);
    m_chorusL.SetDelay(kChorusDelayL);
    m_chorusR.SetDelay(kChorusDelayR);
}

// The instrument's TRANSP flag, whatever its type. It lives in a different
// struct per synth, so this is the one place that knows all five. It gates two
// things now -- chain/song transpose and the SCALE -- which is why it is a
// function rather than the chain of else-ifs it used to be inline.
static bool instTranspEnabled(const Instrument* inst) {
    if (!inst) return true;
    switch (inst->type) {
    case InstType::INST_SAMPLER:  return inst->sampler.transp   != 0;
    case InstType::INST_MACROSYN: return inst->macrosyn.transp  != 0;
    case InstType::INST_HYPERSYN: return inst->hyper.transp     != 0;
    case InstType::INST_FMSYNTH:  return inst->fm.transp        != 0;
    case InstType::INST_WAVSYNTH: return inst->wav.transp       != 0;
    default:                      return true;
    }
}

static constexpr int TICKS_PER_ROW = 6;

void Engine::recalcBPM() {
    const float bpm = static_cast<float>(m_state.bpm)
                    + static_cast<float>(m_state.bpm_frac) / 100.0f;
    const float rowsPerSec  = bpm * 4.0f / 60.0f;
    const float ticksPerSec = rowsPerSec * TICKS_PER_ROW;
    m_samplesPerTick = static_cast<double>(kSampleRate / ticksPerSec);
    if (m_samplesPerTick < 1.0) m_samplesPerTick = 1.0;
    m_envCtx.samplesPerTick = m_samplesPerTick;
}

void Engine::applyParameterUpdate(const EngineCommand& cmd) {
    EngineStateUpdater::applyParameterUpdate(m_state, cmd);
    if (cmd.paramId == ParamID::BPM_INT || cmd.paramId == ParamID::BPM_FRAC) recalcBPM();
}
void Engine::processCommands() {
    EngineCommand cmd;
    while (m_commandRing.pop(cmd)) {

        if (cmd.type == CommandType::PLAY_START) {
            m_state.playMode = static_cast<PlayMode>(cmd.value);
            // currentPhrase is only read in PHRASE mode (tickTrack's base
            // phIdx); currentChain only in CHAIN mode. Setting both to
            // cmd.targetId unconditionally used to leave whichever one the
            // current mode doesn't use holding a value from the wrong ID
            // space (e.g. a song row stored as if it were a phrase id).
            if (m_state.playMode == PlayMode::PHRASE) m_state.currentPhrase = cmd.targetId;
            else if (m_state.playMode == PlayMode::CHAIN) m_state.currentChain = cmd.targetId;
            m_state.activeCol = cmd.col;
            if (m_state.playMode == PlayMode::SONG) m_songRow = cmd.targetId;

            for(int t=0; t<8; ++t) {
                m_state.playSongRow[t] = (m_state.playMode == PlayMode::SONG) ? m_songRow : 0;
                m_state.playChainRow[t] = 0;
                m_state.playPhraseRow[t] = 0;
                m_state.playTick[t] = 0;
                m_state.playGrooveIndex[t] = 0;
                m_state.pendingDel[t] = -1;
                m_state.pendingKil[t] = -1;
                m_state.nextHop[t] = -1;
                if (m_state.playMode == PlayMode::CHAIN) m_state.playChainRow[m_state.activeCol] = cmd.row;
                if (m_state.playMode == PlayMode::PHRASE) m_state.playPhraseRow[m_state.activeCol] = cmd.row;
            }
            for(int t=0; t<8; ++t) {
                publishPlayhead(t);
            }
            m_tickPhase = 0.0;
        } else if (cmd.type == CommandType::PLAY_STOP) {
            m_state.playMode = PlayMode::NONE;
            for(int t=0; t<8; ++t) {
                if (m_voices[t].isActive()) emitNoteOff(t);
                m_tableState[t] = {};   // reset table state
                publishPlayhead(t);
            }
        } else if (cmd.type == CommandType::LOAD_SAMPLE) {
            if (cmd.targetId < 0 || cmd.targetId >= (int)m_state.instruments.size()) {
                // GC ring overflow leaks one buffer. m_gcRing.push() ignores its return value.
                if (cmd.u.sample.data) m_gcRing.push(cmd.u.sample);
                continue;
            }
            Instrument& inst = m_state.instruments[cmd.targetId];
            const SampleHandle oldHandle = inst.sampler.sample;

            SampleHandle existing = m_samplePool.find(cmd.u.sample.path);
            SampleHandle newHandle = -1;
            if (existing >= 0) {
                m_samplePool.addRef(existing);
                newHandle = existing;
                // Incoming buffer is a duplicate — the UI decoded it needlessly, which is fine and cheap.
                // GC ring overflow leaks one buffer. m_gcRing.push() ignores its return value.
                if (cmd.u.sample.data) m_gcRing.push(cmd.u.sample);
            } else {
                newHandle = m_samplePool.install(cmd.u.sample);
                if (newHandle < 0) {
                    // Pool full — free the incoming buffer via the GC ring.
                    // GC ring overflow leaks one buffer. m_gcRing.push() ignores its return value.
                    if (cmd.u.sample.data) m_gcRing.push(cmd.u.sample);
                }
            }

            // Before the swap, silence any voice whose current instrument pointed at the released handle.
            if (oldHandle >= 0 && newHandle >= 0 && oldHandle != newHandle) {
                for (int t = 0; t < 8; ++t) {
                    const int vi = m_trackInstrument[t];
                    if (vi >= 0 && vi < (int)m_state.instruments.size()
                        && m_state.instruments[vi].type == InstType::INST_SAMPLER
                        && m_state.instruments[vi].sampler.sample == oldHandle) {
                        emitNoteOff(t);
                        m_voices[t].setSample(nullptr);
                    }
                }
                SampleData freed = m_samplePool.release(oldHandle);
                // GC ring overflow leaks one buffer. m_gcRing.push() ignores its return value.
                // The audio thread cannot free, and blocking is not an option, so leaking on a full 64-slot ring
                // is the correct trade. The UI drains it every frame.
                if (freed.data) m_gcRing.push(freed);
            }

            if (newHandle >= 0) {
                inst.sampler.sample = newHandle;
                std::memcpy(inst.sampler.samplePath, cmd.u.sample.path, 128);
            }
        } else if (cmd.type == CommandType::UPDATE_PARAM) {
            applyParameterUpdate(cmd);
        } else if (cmd.type == CommandType::SET_STEP) {
            if (cmd.targetId >= 0 && cmd.targetId < Sequencer::NUM_PHRASES && cmd.row >= 0 && cmd.row < Sequencer::ROWS)
                m_sequencer.phrases[cmd.targetId][cmd.row] = cmd.u.step;
        } else if (cmd.type == CommandType::SET_CHAIN_STEP) {
            if (cmd.targetId >= 0 && cmd.targetId < Sequencer::NUM_CHAINS && cmd.row >= 0 && cmd.row < Sequencer::ROWS)
                m_sequencer.chains[cmd.targetId][cmd.row] = cmd.u.chainStep;
        } else if (cmd.type == CommandType::SET_SONG_STEP) {
            if (cmd.targetId >= 0 && cmd.targetId < Sequencer::SONG_ROWS && cmd.row >= 0 && cmd.row < 8)
                m_sequencer.song[cmd.targetId].tracks[cmd.row] = cmd.value;
        } else if (cmd.type == CommandType::SET_GROOVE_STEP) {
            if (cmd.targetId >= 0 && cmd.targetId < Sequencer::NUM_GROOVES && cmd.row >= 0 && cmd.row < 16) {
                m_sequencer.grooves[cmd.targetId].steps[cmd.row] = cmd.value;
                uint8_t len = 16;
                for (int i = 0; i < 16; ++i) {
                    if (m_sequencer.grooves[cmd.targetId].steps[i] == 0xFF) {
                        len = static_cast<uint8_t>(i);
                        break;
                    }
                }
                if (len == 0) len = 1;
                m_sequencer.grooves[cmd.targetId].length = len;
            }
        } else if (cmd.type == CommandType::LOAD_SONG) {
            auto* data = static_cast<LoadedSongData*>(cmd.u.song.data);
            if (data) {
                m_sequencer = data->sequencer;
                // Copy instruments, mixer, effects, project — not play state
                for (int i = 0; i < 128; ++i)
                    m_state.instruments[i] = data->state.instruments[i];
                m_state.mixer     = data->state.mixer;
                m_state.effects   = data->state.effects;
                m_state.project   = data->state.project;
                m_state.bpm       = data->state.bpm;
                m_state.bpm_frac  = data->state.bpm_frac;
                for (int i = 0; i < 16; ++i)
                    m_state.scales[i] = data->state.scales[i];
                for (int i = 0; i < kEqTotal; ++i)
                    m_state.eqs[i] = data->state.eqs[i];
                // Per-track SCA overrides belong to the song that set them, so
                // they clear here or a track would carry the previous song's
                // scale into the new one -- the divergence invariant 11 forbids,
                // since a fresh engine loading the same file has none.
                // (trackGroove has the same latent problem and is left alone:
                // fixing it is a separate change with its own test.)
                for (int i = 0; i < 8; ++i) {
                    m_state.trackScale[i] = -1;
                    m_state.trackKey[i]   = -1;
                }
                m_state.project.scale = data->state.project.scale;
                m_state.project.key   = data->state.project.key;
                // Reset effects DSP state so the new song starts clean — without
                // this, chorus/delay/reverb buffers and DC blockers carry audio
                // from whatever was previously loaded (e.g. the demo song), causing
                // the in-app render path to diverge from m8_render's fresh engine.
                initChorus();
                // The other two ModFX algorithms carry state too -- allpass
                // memories, a delay line and their feedback -- so they reset
                // here for the same reason as everything else in this block
                // (invariant 11: the offline renderer starts from a fresh
                // engine, so anything stateful must be cleared or the two
                // paths diverge). Missing one of these is exactly the bug L9
                // caught in ReverbScM8 earlier today.
                m_phaser.reset();
                m_flanger.reset();
                m_modFxPhase = 0.0f;
                m_shimmer.reset();
                m_shimmerFeed = 0.0f;
                m_delayL.Reset();
                m_delayR.Reset();
                m_reverb.Init(kSampleRate);
                m_dcDelL = 0.0f; m_dcDelR = 0.0f;
                m_dcRevL = 0.0f; m_dcRevR = 0.0f;
                m_dcMixL = 0.0f; m_dcMixR = 0.0f;
                m_smoothChoFreq = 0.0f; m_smoothChoDepth = 0.0f;
                m_smoothDelL = 0.0f; m_smoothDelR = 0.0f;
                // Master chain state, for the same reason as the effects above:
                // the offline renderer starts from a fresh engine, so anything
                // stateful here must be cleared or the two paths diverge
                // (ARCHITECTURE.md invariant 11).
                m_djfL.reset(); m_djfR.reset();
                m_limGain = 1.0f;
                for (int i = 0; i < 8; ++i) m_trackEq[i].reset();
                m_mixEq.reset();
                for (int i = 0; i < 3; ++i) m_sendEq[i].reset();
                for (int i = 0; i < 2; ++i) { m_ottLpL[i] = 0.0f; m_ottLpR[i] = 0.0f; }
                for (int i = 0; i < 3; ++i) { m_ottEnvL[i] = 0.0f; m_ottEnvR[i] = 0.0f; }
                for (int i = 0; i < 9; ++i) {
                    m_meterBlockL[i] = m_meterBlockR[i] = 0.0f;
                    m_meterHeldL[i] = m_meterHeldR[i] = 0.0f;
                    m_meterClip[i] = false;
                }
                m_songGcRing.push(data);
                for (int i = 0; i < 8; ++i) {
                    m_voices[i].resetOscillator();
                    m_tableState[i] = {};
                }
                m_tableTickPhase = 0.0;
                recalcBPM();
            }
        }
    }
}


static inline bool valid(int i, int n) { return i >= 0 && i < n; }

void Engine::emitNoteOff(int t, int songRowOverride) {
    m_voices[t].noteOff();
    m_voices[t].setTableModulation(0.0f, 1.0f);  // clear table modulation
    EngineEvent e_off{};
    e_off.type = EventType::NOTE_OFF;
    e_off.track = t;
    e_off.phraseRow = m_state.playPhraseRow[t];
    e_off.chainRow = m_state.playChainRow[t];
    e_off.songRow = (songRowOverride >= 0) ? static_cast<uint8_t>(songRowOverride) : m_state.playSongRow[t];
    e_off.sampleTime = m_frameCounter;
    emit(e_off);
}

void Engine::syncSongRow() {
    // Copy the shared song row to all tracks and reset their chain/phrase
    // positions so they start fresh from the new song row.
    for (int t = 0; t < 8; ++t) {
        m_state.playSongRow[t] = m_songRow;
        m_state.playChainRow[t] = 0;
        m_state.playPhraseRow[t] = 0;
    }
}

void Engine::publishPlayhead(int t) {
    uint32_t state = (uint32_t(m_state.playSongRow[t]  & 0xFF) << 16)
                   | (uint32_t(m_state.playChainRow[t] & 0xFF) <<  8)
                   |  uint32_t(m_state.playPhraseRow[t] & 0xFF);
    state |= (static_cast<uint32_t>(m_state.playMode) & 0x03) << 24;
    state |= (static_cast<uint32_t>(m_state.activeCol) & 0x07) << 26;
    m_playheadState[t].store(state, std::memory_order_release);
}

void Engine::tickTrack(int t) {
    if (m_state.playMode == PlayMode::PHRASE || m_state.playMode == PlayMode::CHAIN) {
        if (t != m_state.activeCol) {
            if (m_voices[t].isActive()) emitNoteOff(t);
            return;
        }
    }
    
    // Calculate groove for current track — per-track override takes priority
    int grooveId = m_state.trackGroove[t] >= 0 ? m_state.trackGroove[t]
                                                : m_state.project.groove;
    int grooveLength = 6;
    int maxGrooveLen = 16;
    if (valid(grooveId, Sequencer::NUM_GROOVES)) {
        maxGrooveLen = m_sequencer.grooves[grooveId].length;
        if (maxGrooveLen > 16) maxGrooveLen = 16;
        if (maxGrooveLen == 0) maxGrooveLen = 1;
        if (!valid(m_state.playGrooveIndex[t], maxGrooveLen)) m_state.playGrooveIndex[t] = 0;
        uint8_t g = m_sequencer.grooves[grooveId].steps[m_state.playGrooveIndex[t]];
        if (g == 0xFF) {
            m_state.playGrooveIndex[t] = 0;
            g = m_sequencer.grooves[grooveId].steps[0];
        }
        if (g != 0 && g != 0xFF) grooveLength = g;
    }

    if (m_state.playTick[t] == 0) {
        int phIdx = m_state.currentPhrase;
        int transpose = m_state.project.transpose;
        uint8_t chainId = CHAIN_EMPTY;
        
        if (m_state.playMode == PlayMode::SONG) {
            if (!valid(m_songRow, Sequencer::SONG_ROWS)) m_songRow = 0;
            chainId = m_sequencer.song[m_songRow].tracks[t];
            if (chainId == CHAIN_EMPTY) {
                // Track is silent for this section. Don't advance — wait
                // for another track's chain to end, which advances everyone.
                if (m_voices[t].isActive()) emitNoteOff(t, m_songRow);
                return;
            }
        } else if (m_state.playMode == PlayMode::CHAIN) {
            chainId = m_state.currentChain;
        }
        
        if (m_state.playMode == PlayMode::SONG || m_state.playMode == PlayMode::CHAIN) {
            if (!valid(chainId, Sequencer::NUM_CHAINS)) {
                if (m_voices[t].isActive()) emitNoteOff(t);
                return;
            }
            if (!valid(m_state.playChainRow[t], Sequencer::ROWS)) m_state.playChainRow[t] = 0;
            uint8_t ph = m_sequencer.chains[chainId][m_state.playChainRow[t]].phrase;
            
            if (ph == PHRASE_EMPTY) {
                if (m_state.playMode == PlayMode::SONG) {
                    // Chain ended — flag the shared song row to advance.
                    // The actual advance happens in doTick() after all tracks
                    // have been ticked, so every track moves together.
                    m_songRowAdvance = true;
                    if (m_voices[t].isActive()) emitNoteOff(t, m_songRow);
                    return;
                } else { // PLAY_CHAIN stops
                    m_state.playMode = PlayMode::NONE;
                    if (m_voices[t].isActive()) emitNoteOff(t);
                    return;
                }
            }
            
            if (ph != PHRASE_EMPTY && chainId != CHAIN_EMPTY) {
                phIdx = ph;
                transpose = m_sequencer.chains[chainId][m_state.playChainRow[t]].tsp + m_state.project.transpose;
            }
        }
        
        if (!valid(phIdx, Sequencer::NUM_PHRASES)) {
            if (m_voices[t].isActive()) emitNoteOff(t);
            return;
        }
        if (!valid(m_state.playPhraseRow[t], Sequencer::ROWS)) m_state.playPhraseRow[t] = 0;
        const Step& step = m_sequencer.phrases[phIdx][m_state.playPhraseRow[t]];
        
        m_state.pendingDel[t] = -1;
        m_state.pendingKil[t] = -1;
        m_state.nextHop[t] = -1;
        
        auto parseFX = [&](const FxSlot& fx) {
            if (fx.cmd == FxCmd::NONE) return;
            if (fx.cmd == FxCmd::DEL) m_state.pendingDel[t] = std::min((int)fx.val, grooveLength - 1);
            else if (fx.cmd == FxCmd::KIL) m_state.pendingKil[t] = std::min((int)fx.val, grooveLength - 1);
            else if (fx.cmd == FxCmd::HOP) m_state.nextHop[t] = fx.val;
            else if (fx.cmd == FxCmd::TBL) {
                if (fx.val < Sequencer::NUM_TABLES) {
                    m_tableState[t].assignedTable = fx.val;
                    m_tableState[t].row = 0;
                    m_tableState[t].tickCount = 0;
                    if (m_trackInstrument[t] >= 0 && m_trackInstrument[t] < (int)m_state.instruments.size()) {
                        m_tableState[t].tableTickRate = m_state.instruments[m_trackInstrument[t]].getTblTic();
                    }
                }
            }
            else if (fx.cmd == FxCmd::GRV) {
                m_state.trackGroove[t] = (fx.val < Sequencer::NUM_GROOVES) ? fx.val : -1;
                m_state.playGrooveIndex[t] = 0;
            }
            // SCA XY / SCG XY -- X is the key, Y the scale number (manual, and
            // FX_COMMANDS_SPEC.md §SCA). SCA is per track, SCG is the song.
            //
            // The KEY NUMBERING IS UNVERIFIED: 0 = C here, which is what the
            // global key byte reads on a device whose PROJECT view shows C, but
            // the spec's own Tier-2 list flags this exact mapping as open and
            // guesses 0 = B instead. Settled by authoring SCA00/SCA10/SCA20 on a
            // track and reading the key the SCALE view then shows.
            else if (fx.cmd == FxCmd::SCA) {
                m_state.trackKey[t]   = (fx.val >> 4) & 0x0F;
                m_state.trackScale[t] = fx.val & 0x0F;
            }
            else if (fx.cmd == FxCmd::SCG) {
                m_state.project.key   = (fx.val >> 4) & 0x0F;
                m_state.project.scale = fx.val & 0x0F;
                // Global means global: a track that had taken an SCA override
                // goes back to following the project.
                for (int k = 0; k < 8; ++k) {
                    m_state.trackScale[k] = -1;
                    m_state.trackKey[k]   = -1;
                }
            }
        };
        parseFX(step.fx[0]); parseFX(step.fx[1]); parseFX(step.fx[2]);
        
        if (step.instr != INST_EMPTY) {
            m_trackInstrument[t] = step.instr;
        }
        const Instrument* currentInst = nullptr;
        if (m_trackInstrument[t] >= 0 && m_trackInstrument[t] < m_state.instruments.size()) {
            currentInst = &m_state.instruments[m_trackInstrument[t]];
        }
        
        float freq = 0.0f;
        float v = 0.64f;
        
        if (step.note != NOTE_EMPTY) {
            // The instrument TRANSP flag gates chain/song transpose: ON (1, the
            // default) follows transpose; OFF (0) plays the written note unchanged
            // (used e.g. for drum samples that must not pitch-shift with transpose).
            // Per the M8 manual it gates the SCALE too -- "Enable or disable
            // scales per-instrument by editing the TRANSP. option in Instrument
            // view" -- so one flag decides both.
            const bool transpOn = instTranspEnabled(currentInst);
            int effTranspose = transpOn ? transpose : 0;
            int midi = step.note + effTranspose;
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;

            // A track follows the project's scale and key unless SCA gave it its
            // own. PROJECT > SCALE selects the active scale, measured on fw
            // 6.5.2; the manual's "Scale 00 is the default scale for all 8
            // tracks" is just that field's default value.
            const int scaleIdx = m_state.trackScale[t] >= 0
                               ? m_state.trackScale[t]
                               : (m_state.project.scale & 0x0F);
            const int key = m_state.trackKey[t] >= 0
                          ? m_state.trackKey[t]
                          : m_state.project.key;
            const Scale& sc = m_state.scales[scaleIdx & 0x0F];
            float pitch = transpOn ? quantizeToScale(midi, sc, key)
                                   : static_cast<float>(midi);
            pitch = std::clamp(pitch, 0.0f, 127.0f);
            // TUNE is the A4 reference, so it applies whether or not the scale
            // does -- it retunes the machine, it does not quantise. Default
            // 440.00 reproduces the constant it replaced exactly.
            // UNVERIFIED: whether TUNE is per-scale or global. The 42-byte scale
            // record has no room for it, so it is almost certainly global and
            // stored elsewhere; we read scale 0's copy, which is the same value
            // either way while only scale 0 is ever active.
            freq = sc.tune * std::pow(2.0f, (pitch - 69.0f) / 12.0f);

            if (step.vol != VOL_EMPTY) v = (float)step.vol / 127.0f;
            
            m_state.pendingFreq[t] = freq;
            m_state.pendingNote[t] = step.note;
            m_state.pendingVol[t] = v;
            m_state.pendingVolValid[t] = false;
            m_state.pendingInst[t] = currentInst;
        } else if (step.note == NOTE_EMPTY && step.vol != VOL_EMPTY) {
            if (step.vol != VOL_EMPTY) v = (float)step.vol / 127.0f;
            m_state.pendingVol[t] = v;
            m_state.pendingVolValid[t] = true;
            m_state.pendingFreq[t] = 0.0f;
        } else {
            m_state.pendingFreq[t] = 0.0f;
        }
    }
    
    // Execute pending FX logic
    if (m_state.pendingKil[t] == m_state.playTick[t]) {
        emitNoteOff(t);
    }
    
    if (m_state.pendingDel[t] == m_state.playTick[t] || (m_state.pendingDel[t] == -1 && m_state.playTick[t] == 0)) {
        if (m_state.pendingFreq[t] > 0.0f) {
            if (m_state.pendingInst[t] && m_state.pendingInst[t]->type == InstType::INST_SAMPLER) {
                m_voices[t].setSample(m_samplePool.get(m_state.pendingInst[t]->sampler.sample));
            }
            m_voices[t].noteOn(m_state.pendingFreq[t], m_state.pendingVol[t], m_state.pendingInst[t], m_state.pendingNote[t]);
            EngineEvent e_on{};
            e_on.type = EventType::NOTE_ON;
            e_on.track = t;
            e_on.phraseRow = m_state.playPhraseRow[t];
            e_on.chainRow = m_state.playChainRow[t];
            e_on.songRow = m_state.playSongRow[t];
            e_on.instrument = static_cast<uint8_t>(m_trackInstrument[t]);
            e_on.sampleTime = m_frameCounter;
            e_on.frequency = m_state.pendingFreq[t];
            e_on.volume = m_state.pendingVol[t];
            emit(e_on);
            if (m_state.pendingInst[t]) {
                notifyTrigSource(m_trackInstrument[t]);
            }
            // Auto-assign instrument's own table (table index = trackInstrument[t])
            // and initialize tick rate from tbl_tic. Only if no table was already assigned by TBL FX.
            if (m_tableState[t].assignedTable < 0) {
                m_tableState[t].assignedTable = m_trackInstrument[t];
                m_tableState[t].row = 0;
                m_tableState[t].tickCount = 0;
            }
            // Always sync tick rate from instrument tbl_tic (unless TIC FX overrides it)
            if (m_trackInstrument[t] >= 0 && m_trackInstrument[t] < (int)m_state.instruments.size()) {
                m_tableState[t].tableTickRate = m_state.instruments[m_trackInstrument[t]].getTblTic();
            }
            m_state.pendingFreq[t] = 0.0f; // Prevent retrigger
        } else if (m_state.pendingVolValid[t]) {
            m_voices[t].setVolume(m_state.pendingVol[t]);
            m_state.pendingVolValid[t] = false;
        }
    }
    
    // Tick advancement
    m_state.playTick[t]++;
    
    if (m_state.playTick[t] >= grooveLength) {
        m_state.playTick[t] = 0;
        m_state.playGrooveIndex[t] = (m_state.playGrooveIndex[t] + 1) % maxGrooveLen;
        
        if (m_state.nextHop[t] != -1) {
            m_state.playPhraseRow[t] = m_state.nextHop[t];
        } else {
            m_state.playPhraseRow[t]++;
            if (m_state.playPhraseRow[t] >= Sequencer::ROWS) {
                m_state.playPhraseRow[t] = 0;
                if (m_state.playMode == PlayMode::CHAIN || m_state.playMode == PlayMode::SONG) {
                    m_state.playChainRow[t]++;
                    if (m_state.playChainRow[t] >= Sequencer::ROWS) {
                        m_state.playChainRow[t] = 0;
                    }
                }
            }
        }
    }
}

void Engine::tickTable(int t) {
    auto& ts = m_tableState[t];
    if (ts.assignedTable < 0) return;
    if (!valid(ts.assignedTable, Sequencer::NUM_TABLES)) return;

    const TableStep* row = &m_sequencer.tables[ts.assignedTable][ts.row];

    // --- Apply table row modulation to the voice ---
    float tableTransp = static_cast<float>(row->transp);  // semitones (signed)
    float tableVol = (row->vol == VOL_EMPTY) ? 1.0f : (row->vol / 127.0f);

    m_voices[t].setTableModulation(tableTransp, tableVol);

    // --- Parse table-internal FX (VOL, PIT, HOP, TIC) ---
    for (int f = 0; f < 3; ++f) {
        const auto& fx = row->fx[f];
        if (fx.cmd == FxCmd::HOP) {
            if (fx.val < Sequencer::ROWS) {
                ts.row = fx.val;
                ts.tickCount = 0;
                // Immediately apply the target row's modulation
                const TableStep* hopRow = &m_sequencer.tables[ts.assignedTable][ts.row];
                float hopTransp = static_cast<float>(hopRow->transp);
                float hopVol = (hopRow->vol == VOL_EMPTY) ? 1.0f : (hopRow->vol / 127.0f);
                m_voices[t].setTableModulation(hopTransp, hopVol);
                return;
            }
        }
        else if (fx.cmd == FxCmd::TIC) {
            ts.perColTickRate[f] = fx.val;
        }
        else if (fx.cmd == FxCmd::VOL) {
            float fxVol = (fx.val <= 127) ? (fx.val / 127.0f) : 1.0f;
            tableVol *= fxVol;
            m_voices[t].setTableModulation(tableTransp, tableVol);
        }
        else if (fx.cmd == FxCmd::PIT) {
            tableTransp += static_cast<float>(static_cast<int8_t>(fx.val));
            m_voices[t].setTableModulation(tableTransp, tableVol);
        }
    }

    // --- Advance to next row ---
    ts.row++;
    if (ts.row >= Sequencer::ROWS) ts.row = 0; // loop
    ts.tickCount = 0;
}

void Engine::doTick() {
    EngineEvent ev{};
    ev.type = EventType::TICK;
    ev.sampleTime = m_frameCounter;
    emit(ev);

    m_songRowAdvance = false;

    for (int t = 0; t < 8; ++t) {
        if (m_state.playMode != PlayMode::NONE) tickTrack(t);
        publishPlayhead(t);
    }

    // Advance the shared song row if any track's chain ended.
    if (m_state.playMode == PlayMode::SONG && m_songRowAdvance) {
        m_songRow = (m_songRow + 1) % Sequencer::SONG_ROWS;
        syncSongRow();
    }

    // Empty-song guard: if every track is at an empty chain, loop to row 0.
    if (m_state.playMode == PlayMode::SONG) {
        bool allEmpty = true;
        for (int t = 0; t < 8; ++t) {
            if (!valid(m_songRow, Sequencer::SONG_ROWS)) continue;
            uint8_t ch = m_sequencer.song[m_songRow].tracks[t];
            if (ch != CHAIN_EMPTY) { allEmpty = false; break; }
        }
        if (allEmpty) {
            m_songRow = 0;
            syncSongRow();
        }
    }

    // Tick all tracks' tables independently at their own rate
    for (int t = 0; t < 8; ++t) {
        auto& ts = m_tableState[t];
        if (ts.assignedTable < 0) continue;

        int effectiveRate = ts.tableTickRate;

        // TIC mode 0x00: advance only on note-on (handled in tickTrack), skip here
        if (effectiveRate == 0x00) continue;

        // TIC modes 0xFC-0xFE: map-based (octave/velocity/note) — advance on note-on only
        if (effectiveRate >= 0xFC && effectiveRate <= 0xFE) continue;

        // TIC mode 0xFF: 200 Hz fixed rate (240 samples at 48 kHz)
        if (effectiveRate == 0xFF) {
            m_tableTickPhase += 1.0;
            if (m_tableTickPhase >= kTableTickRate200Hz) {
                m_tableTickPhase -= kTableTickRate200Hz;
                tickTable(t);
            }
            continue;
        }

        // Normal tick rate (0x01-0xFB): advance every N phrase ticks
        ts.tickCount++;
        if (ts.tickCount >= effectiveRate) {
            tickTable(t);
        }
    }
}
// ---------------------------------------------------------------------------
// Master bus stages. Order is fixed by the manual (MIXER_SPEC.md §4):
// OTT -> [EQ, not built] -> LIM -> DJF -> MIX -> SPEAKER VOL.
// All state lives in members; nothing here allocates, locks, or branches on
// anything the audio thread doesn't already own.
// ---------------------------------------------------------------------------

// Over The Top: parallel multiband up/downward compression.
//
// APPROXIMATION, not a port (MIXER_SPEC.md §8). Three bands split by one-pole
// crossovers at ~200 Hz and ~2 kHz; each band is pushed toward a target level,
// so quiet parts come up and loud parts come down -- the characteristic OTT
// "everything is loud" effect. The real thing has per-band ratios, attack and
// release times we have no reference for.
//
// Value semantics from the manual: 00..80 mixes the effect in; from 80 up the
// dry mix fades out, so 0xFF is pure OTT.
void Engine::applyOtt(float& l, float& r) {
    const int amt = m_state.mixer.ott;
    if (amt <= 0) return;

    // One-pole crossover coefficients for 200 Hz and 2 kHz at kSampleRate.
    constexpr float kA0 = 0.0256f;   // ~200 Hz
    constexpr float kA1 = 0.2314f;   // ~2 kHz

    m_ottLpL[0] += kA0 * (l - m_ottLpL[0]);
    m_ottLpL[1] += kA1 * (l - m_ottLpL[1]);
    m_ottLpR[0] += kA0 * (r - m_ottLpR[0]);
    m_ottLpR[1] += kA1 * (r - m_ottLpR[1]);

    const float bandL[3] = { m_ottLpL[0], m_ottLpL[1] - m_ottLpL[0], l - m_ottLpL[1] };
    const float bandR[3] = { m_ottLpR[0], m_ottLpR[1] - m_ottLpR[0], r - m_ottLpR[1] };

    constexpr float kTarget = 0.25f;   // level each band is pulled toward
    constexpr float kAtk    = 0.02f;
    constexpr float kRel    = 0.0008f;
    constexpr float kFloor  = 0.0005f; // below this a band is treated as silent
    constexpr float kMaxUp  = 8.0f;    // ceiling on upward gain

    // TIME and COLOR, from the scope view (§UI-9). Both are anchored so 0x80 --
    // the default and what the manual calls 100% / neutral -- reproduces the
    // fixed behaviour these had before the bytes were located.
    //
    // TIME is envelope time as a percentage, 10%..1000%, so it scales the
    // envelope coefficients inversely: a longer time is a slower follower.
    const float timeScale = std::pow(10.0f, (m_state.effects.ott_time - 128.0f) / 128.0f);
    const float atkC = kAtk / timeScale;
    const float relC = kRel / timeScale;
    // COLOR tilts the band gains: above 0x80 toward treble, below toward bass.
    const float tilt = (m_state.effects.ott_color - 128.0f) / 128.0f;   // -1..+1
    const float bandTilt[3] = { 1.0f - 0.5f * tilt, 1.0f, 1.0f + 0.5f * tilt };

    float wetL = 0.0f, wetR = 0.0f;
    for (int b = 0; b < 3; ++b) {
        const float aL = std::fabs(bandL[b]);
        m_ottEnvL[b] += (aL > m_ottEnvL[b] ? atkC : relC) * (aL - m_ottEnvL[b]);
        const float aR = std::fabs(bandR[b]);
        m_ottEnvR[b] += (aR > m_ottEnvR[b] ? atkC : relC) * (aR - m_ottEnvR[b]);

        float gL = (m_ottEnvL[b] > kFloor) ? kTarget / m_ottEnvL[b] : 1.0f;
        float gR = (m_ottEnvR[b] > kFloor) ? kTarget / m_ottEnvR[b] : 1.0f;
        gL = std::clamp(gL, 1.0f / kMaxUp, kMaxUp) * bandTilt[b];
        gR = std::clamp(gR, 1.0f / kMaxUp, kMaxUp) * bandTilt[b];

        wetL += bandL[b] * gL;
        wetR += bandR[b] * gR;
    }

    // 00..80: fade the wet in over the dry. 80..FF: fade the dry out.
    const float a = amt / 128.0f;                 // 0..2
    const float wetMix = std::min(a, 1.0f);
    const float dryMix = (a <= 1.0f) ? 1.0f : std::max(0.0f, 2.0f - a);
    l = l * dryMix + wetL * wetMix;
    r = r * dryMix + wetR * wetMix;
}

// Main mix limiter. "Engaged when its value is above 00" (manual). Feed-forward
// peak limiter: one shared gain for both channels so the stereo image doesn't
// shift when only one side is loud. Fast attack, slow release.
void Engine::applyLimiter(float& l, float& r) {
    const int v = m_state.mixer.lim_val;
    if (v <= 0) { m_limGain = 1.0f; return; }

    // Higher LIM value = harder limiting: threshold falls from unity toward
    // half of full scale. The exact curve is NOT hardware-verified -- no
    // capture exists -- so it is deliberately gentle: at the default 0x40 the
    // threshold is ~0.87, which catches peaks without noticeably squashing
    // existing material. Do not "improve" this into a steeper curve without a
    // measurement (AGENTS.md §4).
    const float threshold = 1.0f - 0.5f * (static_cast<float>(v) / 255.0f);
    const float peak = std::max(std::fabs(l), std::fabs(r));
    const float wanted = (peak > threshold) ? (threshold / peak) : 1.0f;

    // ATK and REL, from the scope view (hw_findings §UI-9). Coefficients are
    // cached because exp() must not run per sample.
    //
    // Both defaults reproduce what the limiter did when these were constants:
    // ATK 0x00 is 0 ms, i.e. the old "attack immediately", and REL 0x00 is the
    // manual's AUTO, whose fast end (~100 ms) is where the old fixed 0.0002
    // coefficient sat.
    auto coefFor = [](float ms) {
        if (ms <= 0.0f) return 1.0f;
        return 1.0f - std::exp(-1.0f / (ms * 0.001f * kSampleRate));
    };
    if (m_state.mixer.lim_atk != m_limAtkByte) {
        m_limAtkByte = m_state.mixer.lim_atk;
        m_limAtkCoef = coefFor((m_limAtkByte / 255.0f) * 100.0f);   // 0..100 ms
    }
    if (m_state.mixer.lim_rel != m_limRelByte) {
        m_limRelByte = m_state.mixer.lim_rel;
        // 4..1000 ms; 0 means AUTO, which swings 100..900 ms with the amount of
        // reduction. Both AUTO ends are precomputed and interpolated below.
        m_limRelCoef = coefFor(4.0f + (m_limRelByte / 255.0f) * 996.0f);
        m_limRelFast = coefFor(100.0f);
        m_limRelSlow = coefFor(900.0f);
    }

    if (wanted < m_limGain) {
        m_limGain += m_limAtkCoef * (wanted - m_limGain);
    } else {
        float rel = m_limRelCoef;
        if (m_limRelByte == 0) {
            const float reduction = std::clamp(1.0f - m_limGain, 0.0f, 1.0f);
            rel = m_limRelFast + reduction * (m_limRelSlow - m_limRelFast);
        }
        m_limGain += rel * (wanted - m_limGain);
    }

    l *= m_limGain;
    r *= m_limGain;
}

// DJ filter. 0x80 is off; below engages the low-pass, above the high-pass.
// Only the default type (LP/HP) is modeled -- the other two modes (LP/BS and
// BS/HP) are selected in the Scope view, which we don't build, so no song can
// reach them from our UI (MIXER_SPEC.md §2).
void Engine::applyDjFilter(float& l, float& r) {
    const int v = m_state.mixer.djf_freq;
    if (v == 0x80) return;   // off: bypass entirely, filters keep their state

    const bool highPass = (v > 0x80);
    // Map either half onto 30 Hz .. 18 kHz, exponentially.
    const float t = highPass ? (v - 0x80) / 127.0f      // 0..1 upward
                             : (0x80 - v) / 128.0f;     // 0..1 downward
    const float cutoff = highPass ? 30.0f * std::pow(600.0f, t)
                                  : 18000.0f * std::pow(1.0f / 600.0f, t);

    // RES, from the scope view (§UI-9). ZdfSvf takes 0..1; 0.3 was the fixed
    // value before this byte was located, so a resonance of 00 -- the default,
    // and what every existing song carries -- lands on exactly that.
    const float res = 0.3f + (m_state.mixer.djf_res / 255.0f) * 0.65f;
    m_djfL.setParams(cutoff, res, kSampleRate);
    m_djfR.setParams(cutoff, res, kSampleRate);
    float hpL = 0.0f, hpR = 0.0f;
    const float lpL = m_djfL.process(l, hpL);
    const float lpR = m_djfR.process(r, hpR);
    l = highPass ? hpL : lpL;
    r = highPass ? hpR : lpR;
}

// The factory-default bank -- three flat bands. Namespace scope rather than a
// function-local static so the audio thread never touches a guard variable.
static const EqBank kFlatEqBank{};

// Ceiling on how much pitched-up tail is folded back. The reverb's own feedback
// already reaches 0.98, so this is the difference between a shimmer and an
// oscillator. Not hardware-verified -- chosen for stability, and A16 is what
// holds it there.
static constexpr float kShimmerMax = 0.45f;

// STEREO WIDTH on an effect return. 0xFF is unity and is the device's default
// for all three, so the early-out means a song that never touches these renders
// exactly as it did before this existed -- not "within tolerance", the same
// samples. Below unity the side component is scaled down, reaching mono at 0.
static inline void applyStereoWidth(float& l, float& r, int amt) {
    if (amt >= 255) return;
    const float w = amt / 255.0f;
    const float m = 0.5f * (l + r);
    const float s = 0.5f * (l - r) * w;
    l = m + s;
    r = m - s;
}

// Point each track's EQ at whatever bank its current instrument names. Called
// once per render() call rather than per sample: a bank change landing a few
// milliseconds late is inaudible, and configure() does nothing at all when the
// bank has not changed.
void Engine::configureTrackEqs() {
    for (int t = 0; t < 8; ++t) {
        const int inst = m_trackInstrument[t];
        int bank = 0;
        if (inst >= 0 && inst < static_cast<int>(m_state.instruments.size()))
            bank = m_state.instruments[inst].getEq();

        // 0 is "no EQ" (drawn as "--" on both instrument screens), so an
        // unassigned instrument gets the factory-default bank, which is three
        // flat bands and therefore an exact bypass.
        if (bank <= 0 || bank >= kMaxEqBanks) m_trackEq[t].configure(kFlatEqBank, kSampleRate);
        else                                  m_trackEq[t].configure(m_state.eqs[bank], kSampleRate);
    }
}

void Engine::publishMeters(int frames) {
    (void)frames;
    for (int i = 0; i < 9; ++i) {
        m_meterHeldL[i] = std::max(m_meterBlockL[i], m_meterHeldL[i] * kMeterDecay);
        m_meterHeldR[i] = std::max(m_meterBlockR[i], m_meterHeldR[i] * kMeterDecay);

        const uint32_t pl = uint32_t(std::clamp(m_meterHeldL[i], 0.0f, 1.0f) * 255.0f);
        const uint32_t pr = uint32_t(std::clamp(m_meterHeldR[i], 0.0f, 1.0f) * 255.0f);
        const uint32_t packed = pl | (pr << 8) | (m_meterClip[i] ? (1u << 16) : 0u);

        if (i < 8) m_trackLevel[i].store(packed, std::memory_order_release);
        else       m_masterLevel.store(packed, std::memory_order_release);

        m_meterBlockL[i] = 0.0f;
        m_meterBlockR[i] = 0.0f;
        m_meterClip[i] = false;
    }
}

void Engine::render(float* buffer, int frames) {
    configureTrackEqs();
    m_mixEq.configure(m_state.eqs[kEqMix], kSampleRate);
    m_sendEq[0].configure(m_state.eqs[kEqModFx],  kSampleRate);
    m_sendEq[1].configure(m_state.eqs[kEqDelay],  kSampleRate);
    m_sendEq[2].configure(m_state.eqs[kEqReverb], kSampleRate);

    for (int i = 0; i < frames; ++i) {
        m_frameCounter++;
        processCommands();
        if (m_tickPhase <= 0.0) {
            m_tickPhase += m_samplesPerTick;
            doTick();
        }
        m_tickPhase -= 1.0;

        float mixL = 0.0f;
        float mixR = 0.0f;
        // Sends are stereo and taken post-pan, so a hard-panned track reaches
        // the effects on the side it actually sits on (EQ_SPEC.md §8).
        float sendChoL = 0.0f, sendChoR = 0.0f;
        float sendDelL = 0.0f, sendDelR = 0.0f;
        float sendRevL = 0.0f, sendRevR = 0.0f;

        for (int t = 0; t < 8; ++t) {
            // Stereo from the voice. Only the sampler path fills the two
            // channels differently; every synth engine still returns one value
            // duplicated, so their audio is unchanged (see renderFrame).
            float vFrame[2] = {0.0f, 0.0f};
            m_voices[t].renderFrame(m_envCtx, vFrame);
            float tVol = m_state.mixer.track_vol[t] / 255.0f;
            vFrame[0] *= tVol;
            vFrame[1] *= tVol;
            
            float pan = 0.5f;
            float dry = 1.0f;
            float cho = 0.0f;
            float del = 0.0f;
            float rev = 0.0f;
            
            if (m_trackInstrument[t] >= 0 && m_trackInstrument[t] < m_state.instruments.size()) {
                const Instrument& inst = m_state.instruments[m_trackInstrument[t]];
                if (inst.type == InstType::INST_SAMPLER) {
                    pan = inst.sampler.pan / 255.0f;
                    dry = inst.sampler.dry / 255.0f;
                    cho = inst.sampler.cho / 255.0f;
                    del = inst.sampler.del / 255.0f;
                    rev = inst.sampler.rev / 255.0f;
                } else if (inst.type == InstType::INST_MACROSYN) {
                    pan = inst.macrosyn.pan / 255.0f;
                    dry = inst.macrosyn.dry / 255.0f;
                    cho = inst.macrosyn.cho / 255.0f;
                    del = inst.macrosyn.del / 255.0f;
                    rev = inst.macrosyn.rev / 255.0f;
                } else if (inst.type == InstType::INST_HYPERSYN) {
                    pan = inst.hyper.pan / 255.0f;
                    dry = inst.hyper.dry / 255.0f;
                    cho = inst.hyper.cho / 255.0f;
                    del = inst.hyper.del / 255.0f;
                    rev = inst.hyper.rev / 255.0f;
                } else if (inst.type == InstType::INST_FMSYNTH) {
                    pan = inst.fm.pan / 255.0f;
                    dry = inst.fm.dry / 255.0f;
                    cho = inst.fm.cho / 255.0f;
                    del = inst.fm.del / 255.0f;
                    rev = inst.fm.rev / 255.0f;
                } else if (inst.type == InstType::INST_WAVSYNTH) {
                    pan = inst.wav.pan / 255.0f;
                    dry = inst.wav.dry / 255.0f;
                    cho = inst.wav.cho / 255.0f;
                    del = inst.wav.del / 255.0f;
                    rev = inst.wav.rev / 255.0f;
                }
            }
            
            // Pan law: near channel at UNITY, far channel attenuated LINEARLY.
            //
            // MEASURED on hardware 2026-08-14, not assumed (hw_findings.md §UI-10
            // and §UI-12). Sweeping a macrosynth probe's PAN across 00/20/40/60/80
            // and deriving L = mid+side, R = mid-side from the captures:
            //
            //   PAN   L         R         R/L     pan/0x80
            //   00    0.006704  0.000000  0.000   0.000
            //   20    0.006862  0.001694  0.247   0.250
            //   40    0.006913  0.003447  0.499   0.500
            //   60    0.006968  0.005224  0.750   0.750
            //   80    0.007122  0.007122  1.000   1.000
            //
            // R/L tracks pan/0x80 to three decimals, and L stays flat within 6%
            // (noise). This was previously constant-power (cos/sin), which holds
            // L*L + R*R constant instead: that renders a centred track 3 dB
            // quieter than hardware and lifts the near channel by 3 dB as the pan
            // sweeps. Under cos/sin, L at hard left should have measured 0.01537
            // against 0.010866 actual.
            //
            // `pan` here is the byte scaled to 0..1, so centre (0x80 = 128) is
            // 128/255 = 0.50196 rather than exactly 0.5 -- hence normalising each
            // half by its own width instead of assuming symmetry about 0.5.
            constexpr float kPanCentre = 128.0f / 255.0f;
            float panL, panR;
            if (pan <= kPanCentre) {
                panL = 1.0f;
                panR = pan / kPanCentre;
            } else {
                panL = (1.0f - pan) / (1.0f - kPanCentre);
                panR = 1.0f;
            }
            // The upper half (pan > 0x80) is by symmetry with the lower, which was
            // the half actually swept; 0x80 and 0x00 are both pinned by
            // measurement. Sweep A0/C0/E0/FF if that assumption is ever in doubt.

            // Instrument EQ sits on the track's whole output, before it splits
            // into the dry path and the sends -- so the effects hear the EQ'd
            // signal too. MEASURED on hardware 2026-08-13, not assumed: with an
            // instrument's dry level at zero and its reverb send wide open, a
            // -20 dB shelf at 5 kHz in its EQ bank came back in the recording
            // as a 15-23 dB loss above 5 kHz and a centroid drop of 1649 Hz. If
            // the send were tapped before the EQ, that capture would have been
            // identical to the one without the EQ assigned. See hw_findings.md
            // §UI-6.
            //
            // Panning comes first because the EQ needs a stereo signal for its
            // MID/SIDE/LEFT/RIGHT modes to mean anything -- the voice path
            // itself is mono. One filter instance still serves both the dry and
            // the send paths, since they now carry the same signal.
            //
            // A bypassed EQ returns immediately without touching either value.
            float sigL = vFrame[0] * panL;
            float sigR = vFrame[1] * panR;
            m_trackEq[t].process(sigL, sigR);

            const float outL = sigL * dry;
            const float outR = sigR * dry;

            mixL += outL;
            mixR += outR;

            // Track meter: post-volume, post-pan, pre-sends -- what this track
            // actually contributes to the mix. Block maximum only; the decay
            // and the atomic publish happen once per render() in
            // publishMeters().
            const float aL = std::fabs(outL);
            const float aR = std::fabs(outR);
            if (aL > m_meterBlockL[t]) m_meterBlockL[t] = aL;
            if (aR > m_meterBlockR[t]) m_meterBlockR[t] = aR;
            if (aL >= 1.0f || aR >= 1.0f) m_meterClip[t] = true;

            // Fed from the same EQ'd, panned signal as the dry path above.
            sendChoL += sigL * cho;  sendChoR += sigR * cho;
            sendDelL += sigL * del;  sendDelR += sigR * del;
            sendRevL += sigL * rev;  sendRevR += sigR * rev;
        }

        // Each send's INPUT EQ, applied to the send bus before its effect --
        // which is what the device calls it and where it belongs.
        m_sendEq[0].process(sendChoL, sendChoR);
        m_sendEq[1].process(sendDelL, sendDelR);
        // The reverb's INPUT EQ is applied further down, after ModFX's and the
        // delay's REVERB SEND have been folded into this bus -- so it EQs
        // everything entering the reverb, which is what "input EQ" should mean.
        // ASSUMPTION: whether hardware taps those sends before or after the
        // reverb's input EQ is unmeasured. It is the same question §UI-6 had to
        // settle for the instrument EQ, and it wants the same treatment: mute
        // the dry path, drive one send, and compare captures.

        m_smoothChoFreq += ((m_state.effects.cho_mod_freq / 255.0f) * 10.0f - m_smoothChoFreq) * 0.005f;
        m_smoothChoDepth += (m_state.effects.cho_mod_depth / 255.0f - m_smoothChoDepth) * 0.005f;
        m_chorusL.SetLfoFreq(m_smoothChoFreq);
        m_chorusR.SetLfoFreq(m_smoothChoFreq);
        m_chorusL.SetLfoDepth(m_smoothChoDepth);
        m_chorusR.SetLfoDepth(m_smoothChoDepth);
        // ModFX is a slot with three algorithms, not a chorus (§UI-7). All
        // three share MOD DEPTH and MOD FRQ; only the processor differs.
        float choL = 0.0f, choR = 0.0f;
        if (m_state.effects.modfx_type == 0) {
            // One DaisySP ChorusEngine per channel, differing in base delay --
            // see kChorusDelayL/R for why, and for what daisysp::Chorus got
            // wrong. The 0.5 reproduces that class's gain_frac_, whose cross-pan
            // summed to unity, so the level here is unchanged and centred
            // material still gives the identical left channel.
            choL = 0.5f * m_chorusL.Process(sendChoL);
            choR = 0.5f * m_chorusR.Process(sendChoR);
        } else {
            // Phaser and flanger get a shared LFO, with the right channel a
            // quarter cycle behind the left so there is a stereo image for
            // STEREO WIDTH to narrow.
            m_modFxPhase += m_smoothChoFreq / kSampleRate;
            if (m_modFxPhase >= 1.0f) m_modFxPhase -= 1.0f;
            const float lfoL = 0.5f - 0.5f * std::cos(6.28318530718f * m_modFxPhase);
            float qp = m_modFxPhase + 0.25f;
            if (qp >= 1.0f) qp -= 1.0f;
            const float lfoR = 0.5f - 0.5f * std::cos(6.28318530718f * qp);

            if (m_state.effects.modfx_type == 1)
                m_phaser.process(sendChoL, sendChoR, lfoL, lfoR,
                                 m_smoothChoDepth, kSampleRate, choL, choR);
            else
                m_flanger.process(sendChoL, sendChoR, lfoL, lfoR,
                                  m_smoothChoDepth, kSampleRate, choL, choR);
        }
        applyStereoWidth(choL, choR, m_state.effects.cho_width);

        float maxDel = kSampleRate * 2.0f - 1.0f; // 2 seconds max
        m_smoothDelL += ((m_state.effects.del_time_l / 255.0f) * maxDel - m_smoothDelL) * 0.001f;
        m_smoothDelR += ((m_state.effects.del_time_r / 255.0f) * maxDel - m_smoothDelR) * 0.001f;
        m_delayL.SetDelay(m_smoothDelL);
        m_delayR.SetDelay(m_smoothDelR);
        
        float delFeed = std::min(m_state.effects.del_feedback / 255.0f, 0.98f);
        float delL = dcBlock(m_delayL.Read(), m_dcDelL);
        float delR = dcBlock(m_delayR.Read(), m_dcDelR);
        // Both lines now get their own channel. They always had independently
        // smoothed times, but were fed the identical signal, so the width and
        // time offset did far less than they looked like they did.
        m_delayL.Write(sendDelL + delL * delFeed);
        m_delayR.Write(sendDelR + delR * delFeed);
        // Width is applied to the return only, after the line has been written
        // back -- narrowing the output must not narrow what feeds the feedback
        // path, or the image would collapse further on every repeat.
        applyStereoWidth(delL, delR, m_state.effects.del_width);

        // ModFX and Delay each have their own REVERB SEND. Taken from their
        // post-width returns, so what the reverb hears is what the mix hears.
        // No feedback loop is created: the reverb's output feeds neither.
        const int choRevAmt = m_state.effects.cho_reverb;
        const int delRevAmt = m_state.effects.del_reverb;
        if (choRevAmt > 0 || delRevAmt > 0) {
            const float cr = choRevAmt / 255.0f;
            const float dr = delRevAmt / 255.0f;
            sendRevL += choL * cr + delL * dr;
            sendRevR += choR * cr + delR * dr;
        }
        // SHIMMER folds last, after the input EQ has had its say on everything
        // else -- it is the reverb's own tail coming back an octave up, not an
        // input to be filtered again. m_shimmerFeed holds the previous sample,
        // which is what stops this being an instantaneous loop.
        m_sendEq[2].process(sendRevL, sendRevR);
        if (m_state.effects.rev_shimmer > 0) {
            sendRevL += m_shimmerFeed;
            sendRevR += m_shimmerFeed;
        }

        float fb = m_state.effects.rev_decay / 255.0f; if(fb > 0.98f) fb = 0.98f; m_reverb.SetFeedback(fb);
        m_reverb.SetLpFreq(10000.0f);
        // ROOM SIZE / MOD DEPTH / MOD FREQ. These went from silent to live on
        // 2026-08-14, once ReverbSc was vendored and given setters for them.
        //
        // Each mapping is anchored so that the engine's own default byte lands
        // on exactly the stock tuning the reverb had when these were fixed
        // constants -- rev_size 0xFF, rev_mod_depth 0x20, rev_mod_freq 0xFF all
        // map to 1.0. A song that never touched them therefore renders
        // unchanged, which test A9 pins bit-for-bit.
        //
        // The curves themselves are NOT hardware-verified. They are chosen to
        // be sensible and to keep that anchor; they are approximations in the
        // same class as the LIM/DJF/OTT curves (MIXER_SPEC.md §8), and a
        // capture could well move them.
        m_reverb.SetRoomSize(0.25f + 0.75f * (m_state.effects.rev_size / 255.0f));
        m_reverb.SetPitchMod(m_state.effects.rev_mod_depth / 32.0f);
        m_reverb.SetModRate(0.1f + 0.9f * (m_state.effects.rev_mod_freq / 255.0f));
        float revL = 0.0f, revR = 0.0f;
        m_reverb.Process(sendRevL, sendRevR, &revL, &revR);
        revL = dcBlock(revL, m_dcRevL);
        revR = dcBlock(revR, m_dcRevR);

        // Pitch the tail up an octave and hold it for the next sample's input.
        // Two things keep this stable: the amount is capped well below unity,
        // and the fed-back signal goes through a tanh -- the reverb's own
        // feedback already runs up to 0.98, so an uncapped shimmer loop would
        // be a gain stage feeding itself.
        if (m_state.effects.rev_shimmer > 0) {
            const float amt = (m_state.effects.rev_shimmer / 255.0f) * kShimmerMax;
            m_shimmerFeed = std::tanh(m_shimmer.process(0.5f * (revL + revR))) * amt;
        } else {
            m_shimmerFeed = 0.0f;
        }

        applyStereoWidth(revL, revR, m_state.effects.rev_width);

        float master_cho = m_state.mixer.cho_vol / 255.0f;
        float master_del = m_state.mixer.del_vol / 255.0f;
        float master_rev = m_state.mixer.rev_vol / 255.0f;
        
        mixL += choL * master_cho + delL * master_del + revL * master_rev;
        mixR += choR * master_cho + delR * master_del + revR * master_rev;

        // ---- Master chain, in the manual's order (MIXER_SPEC.md §4) --------
        applyOtt(mixL, mixR);
        m_mixEq.process(mixL, mixR);   // main mix EQ (EQ_SPEC.md step 8)
        applyLimiter(mixL, mixR);
        applyDjFilter(mixL, mixR);

        // MIX: the song's master volume (the file's master_volume).
        const float mixVol = m_state.mixer.mix_vol / 255.0f;
        mixL *= mixVol;
        mixR *= mixVol;

        // SPEAKER VOL: this application's output level. Never persisted.
        const float speakerVol = m_state.mixer.out_vol / 255.0f;
        mixL *= speakerVol;
        mixR *= speakerVol;

        mixL = dcBlock(mixL, m_dcMixL);
        mixR = dcBlock(mixR, m_dcMixR);

        // SOFT CLIP -- the M8's own, now driven by its byte (§UI-9). The manual
        // calls it "a gentle saturation after the limiter to prevent harsh
        // clipping" and notes that raising MIX exaggerates it, which puts it
        // after MIX, exactly where this sits.
        //
        // Any non-zero value engages it. When it is off the hard clamp below is
        // all that stands between a hot mix and a clipped buffer -- which is
        // what the device does too, and is the point of the control.
        if (m_state.mixer.soft_clip != 0) {
            mixL = std::tanh(mixL);
            mixR = std::tanh(mixR);
        }

        const float aL = std::fabs(mixL);
        const float aR = std::fabs(mixR);
        if (aL > m_meterBlockL[8]) m_meterBlockL[8] = aL;
        if (aR > m_meterBlockR[8]) m_meterBlockR[8] = aR;
        if (aL >= 1.0f || aR >= 1.0f) m_meterClip[8] = true;

        buffer[i * 2]     = std::clamp(mixL, -1.0f, 1.0f);
        buffer[i * 2 + 1] = std::clamp(mixR, -1.0f, 1.0f);
    }

    publishMeters(frames);
}

void Engine::notifyTrigSource(uint8_t instrumentIndex) {
    for (int t = 0; t < 8; ++t) {
        m_voices[t].triggerModsWithSource(instrumentIndex);
    }
}

} // namespace engine
} // namespace m8


