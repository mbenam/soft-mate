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
    m_chorus.Init(kSampleRate);
    m_delayL.Init();
    m_delayR.Init();
    m_reverb.Init(kSampleRate);
    recalcBPM();
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
            if (cmd.targetId >= 0 && cmd.targetId < Sequencer::NUM_GROOVES && cmd.row >= 0 && cmd.row < 16)
                m_sequencer.grooves[cmd.targetId].steps[cmd.row] = cmd.value;
        } else if (cmd.type == CommandType::LOAD_SONG) {
            auto* base = static_cast<uint8_t*>(cmd.u.song.data);
            if (base) {
                auto* seq = reinterpret_cast<Sequencer*>(base);
                auto* st  = reinterpret_cast<EngineState*>(base + sizeof(Sequencer));
                std::memcpy(&m_sequencer, seq, sizeof(Sequencer));
                // Copy instruments, mixer, effects, project — not play state
                for (int i = 0; i < 128; ++i)
                    m_state.instruments[i] = st->instruments[i];
                m_state.mixer     = st->mixer;
                m_state.effects   = st->effects;
                m_state.project   = st->project;
                m_state.bpm       = st->bpm;
                m_state.bpm_frac  = st->bpm_frac;
                for (int i = 0; i < 16; ++i)
                    m_state.scales[i] = st->scales[i];
                // Reset effects DSP state so the new song starts clean — without
                // this, chorus/delay/reverb buffers and DC blockers carry audio
                // from whatever was previously loaded (e.g. the demo song), causing
                // the in-app render path to diverge from m8_render's fresh engine.
                m_chorus.Init(kSampleRate);
                m_delayL.Reset();
                m_delayR.Reset();
                m_reverb.Init(kSampleRate);
                m_dcDelL = 0.0f; m_dcDelR = 0.0f;
                m_dcRevL = 0.0f; m_dcRevR = 0.0f;
                m_dcMixL = 0.0f; m_dcMixR = 0.0f;
                m_smoothChoFreq = 0.0f; m_smoothChoDepth = 0.0f;
                m_smoothDelL = 0.0f; m_smoothDelR = 0.0f;
                m_songGcRing.push(cmd.u.song.data);
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
        if (g != 0) grooveLength = g;
    }

    if (m_state.playTick[t] == 0) {
        int phIdx = m_state.currentPhrase;
        int transpose = 0;
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
                transpose = m_sequencer.chains[chainId][m_state.playChainRow[t]].tsp;
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
            int effTranspose = transpose;
            if (currentInst) {
                if (currentInst->type == InstType::INST_SAMPLER && currentInst->sampler.transp == 0)
                    effTranspose = 0;
                else if (currentInst->type == InstType::INST_MACROSYN && currentInst->macrosyn.transp == 0)
                    effTranspose = 0;
                else if (currentInst->type == InstType::INST_HYPERSYN && currentInst->hyper.transp == 0)
                    effTranspose = 0;
                else if (currentInst->type == InstType::INST_FMSYNTH && currentInst->fm.transp == 0)
                    effTranspose = 0;
                else if (currentInst->type == InstType::INST_WAVSYNTH && currentInst->wav.transp == 0)
                    effTranspose = 0;
            }
            int midi = step.note + effTranspose;
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;
            freq = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
            
            if (step.vol != VOL_EMPTY) v = (float)step.vol / 127.0f;
            
            m_state.pendingFreq[t] = freq;
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
            m_voices[t].noteOn(m_state.pendingFreq[t], m_state.pendingVol[t], m_state.pendingInst[t]);
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
void Engine::render(float* buffer, int frames) {
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
        float sendCho = 0.0f;
        float sendDel = 0.0f;
        float sendRev = 0.0f;

        for (int t = 0; t < 8; ++t) {
            float vSamp = m_voices[t].renderSample(m_envCtx);
            float tVol = m_state.mixer.track_vol[t] / 255.0f;
            vSamp *= tVol;
            
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
            
            float panL = std::cos(pan * 1.5707963f);
            float panR = std::sin(pan * 1.5707963f);
            
            mixL += vSamp * dry * panL;
            mixR += vSamp * dry * panR;
            
            sendCho += vSamp * cho;
            sendDel += vSamp * del;
            sendRev += vSamp * rev;
        }

        m_smoothChoFreq += ((m_state.effects.cho_mod_freq / 255.0f) * 10.0f - m_smoothChoFreq) * 0.005f;
        m_smoothChoDepth += (m_state.effects.cho_mod_depth / 255.0f - m_smoothChoDepth) * 0.005f;
        m_chorus.SetLfoFreq(m_smoothChoFreq);
        m_chorus.SetLfoDepth(m_smoothChoDepth);
        m_chorus.Process(sendCho);
        float choL = m_chorus.GetLeft();
        float choR = m_chorus.GetRight();

        float maxDel = kSampleRate * 2.0f - 1.0f; // 2 seconds max
        m_smoothDelL += ((m_state.effects.del_time_l / 255.0f) * maxDel - m_smoothDelL) * 0.001f;
        m_smoothDelR += ((m_state.effects.del_time_r / 255.0f) * maxDel - m_smoothDelR) * 0.001f;
        m_delayL.SetDelay(m_smoothDelL);
        m_delayR.SetDelay(m_smoothDelR);
        
        float delFeed = std::min(m_state.effects.del_feedback / 255.0f, 0.98f);
        float delL = dcBlock(m_delayL.Read(), m_dcDelL);
        float delR = dcBlock(m_delayR.Read(), m_dcDelR);
        m_delayL.Write(sendDel + delL * delFeed);
        m_delayR.Write(sendDel + delR * delFeed);

        float fb = m_state.effects.rev_decay / 255.0f; if(fb > 0.98f) fb = 0.98f; m_reverb.SetFeedback(fb);
        m_reverb.SetLpFreq(10000.0f);
        float revL = 0.0f, revR = 0.0f;
        m_reverb.Process(sendRev, sendRev, &revL, &revR);
        revL = dcBlock(revL, m_dcRevL);
        revR = dcBlock(revR, m_dcRevR);

        float master_cho = m_state.mixer.cho_vol / 255.0f;
        float master_del = m_state.mixer.del_vol / 255.0f;
        float master_rev = m_state.mixer.rev_vol / 255.0f;
        
        mixL += choL * master_cho + delL * master_del + revL * master_rev;
        mixR += choR * master_cho + delR * master_del + revR * master_rev;

        float master_vol = m_state.mixer.out_vol / 255.0f;
        mixL *= master_vol;
        mixR *= master_vol;

        mixL = dcBlock(mixL, m_dcMixL);
        mixR = dcBlock(mixR, m_dcMixR);

        mixL = std::tanh(mixL);
        mixR = std::tanh(mixR);

        buffer[i * 2]     = std::clamp(mixL, -1.0f, 1.0f);
        buffer[i * 2 + 1] = std::clamp(mixR, -1.0f, 1.0f);
    }
}

void Engine::notifyTrigSource(uint8_t instrumentIndex) {
    for (int t = 0; t < 8; ++t) {
        m_voices[t].triggerModsWithSource(instrumentIndex);
    }
}

} // namespace engine
} // namespace m8


