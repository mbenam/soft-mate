#include "SynthVoice.h"
#include "Engine.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace m8 {
namespace engine {

static float polyBLEP(float t, float dt) {
    if (t < dt) { float u = t / dt; return u + u - u * u - 1.0f; }
    if (t > 1.0f - dt) { float u = (t - 1.0f) / dt; return u * u + u + u + 1.0f; }
    return 0.0f;
}

static constexpr float kFMModIndex = 6.0f;
static constexpr float kFMFeedbackScale = 3.14159265f;

void SynthVoice::initFMWavetables() {
    if (m_fmWavetableReady) return;
    constexpr float TWO_PI = 6.2831853f;
    for (int s = 0; s < kFMNumShapes; ++s) {
        for (int i = 0; i < kFMWavetableSize; ++i) {
            float phase = float(i) / float(kFMWavetableSize);
            float val = 0.0f;
            switch (s) {
            case 0: val = std::sin(phase * TWO_PI); break;
            case 1:
                val = std::sin(phase * TWO_PI) + 0.5f * std::sin(phase * TWO_PI * 2.0f);
                break;
            case 2:
                val = std::sin(phase * TWO_PI)
                    + 0.4f * std::sin(phase * TWO_PI * 2.0f)
                    + 0.2f * std::sin(phase * TWO_PI * 3.0f);
                break;
            case 3:
                val = std::sin(phase * TWO_PI)
                    + 0.35f * std::sin(phase * TWO_PI * 2.0f)
                    + 0.2f * std::sin(phase * TWO_PI * 3.0f)
                    + 0.1f * std::sin(phase * TWO_PI * 4.0f);
                break;
            case 4:
                val = std::sin(phase * TWO_PI)
                    + 0.6f * std::sin(phase * TWO_PI * 2.0f + 0.5f);
                break;
            case 5:
                val = std::sin(phase * TWO_PI)
                    + 0.3f * std::sin(phase * TWO_PI * 3.0f);
                break;
            case 6: {
                float t = 0.0f;
                for (int h = 0; h < 16; ++h) {
                    int n = 2 * h + 1;
                    t += ((h % 2 == 0 ? 1.0f : -1.0f) * std::sin(phase * TWO_PI * float(n))) / float(n * n);
                }
                val = t * 8.0f / (TWO_PI * TWO_PI);
                break;
            }
            case 7: {
                float t = 0.0f;
                for (int h = 1; h <= 32; ++h)
                    t += std::sin(phase * TWO_PI * float(h)) / float(h);
                val = -2.0f / 3.14159265f * t;
                break;
            }
            case 8: {
                float t = 0.0f;
                for (int h = 0; h < 16; ++h) {
                    int n = 2 * h + 1;
                    t += std::sin(phase * TWO_PI * float(n)) / float(n);
                }
                val = 4.0f / 3.14159265f * t;
                break;
            }
            case 9: val = (phase < 0.25f) ? 1.0f : -1.0f; break;
            case 10: val = (i == 0) ? 1.0f : 0.0f; break;
            case 11: {
                uint32_t seed = static_cast<uint32_t>(i * 2654435761u);
                seed ^= seed >> 13;
                val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
                break;
            }
            }
            if (s >= 1 && s <= 5) val *= 0.5f;
            m_fmWavetable[s][i] = std::clamp(val, -1.0f, 1.0f);
        }
    }
    m_fmWavetableReady = true;
}

float SynthVoice::readFMWavetable(const float* table, float phase) {
    float idx = phase * kFMWavetableSize;
    int i = static_cast<int>(idx) & (kFMWavetableSize - 1);
    float frac = idx - std::floor(idx);
    int next = (i + 1) & (kFMWavetableSize - 1);
    return table[i] * (1.0f - frac) + table[next] * frac;
}

void SynthVoice::generateWavShape(const WavSynthState& ws, float noteFreq) {
    int shape = std::clamp(ws.shape, 0, 8);
    int waveLen = std::clamp((ws.size * kWavBufSize) / 256, 4, kWavBufSize);
    int repeats = 1 + std::clamp(ws.mult, 0, 15);
    float warpShift = (ws.warp - 128) / 128.0f;
    float mirrorPos = ws.scan / 200.0f;

    for (int i = 0; i < waveLen; ++i) {
        float t = float(i) / float(waveLen);
        float shaped_t = std::fmod(t * repeats + warpShift, 1.0f);
        if (shaped_t < 0.0f) shaped_t += 1.0f;

        float val = 0.0f;
        switch (shape) {
        case 0: val = (shaped_t < 0.12f) ? 1.0f : -1.0f; break;
        case 1: val = (shaped_t < 0.25f) ? 1.0f : -1.0f; break;
        case 2: val = (shaped_t < 0.50f) ? 1.0f : -1.0f; break;
        case 3: val = (shaped_t < 0.75f) ? 1.0f : -1.0f; break;
        case 4: val = 2.0f * shaped_t - 1.0f; break;
        case 5:
            val = (shaped_t < 0.5f) ? (4.0f * shaped_t - 1.0f) : (3.0f - 4.0f * shaped_t);
            break;
        case 6: val = std::sin(shaped_t * 6.2831853f); break;
        case 7: {
            uint32_t seed = static_cast<uint32_t>(i * repeats) * 2654435761u;
            seed ^= seed >> 13;
            val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
            break;
        }
        case 8: {
            uint32_t seed = static_cast<uint32_t>(i) * 2654435761u;
            seed ^= seed >> 13;
            val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
            break;
        }
        }

        if (t > mirrorPos && mirrorPos > 0.0f) {
            float mirrorT = 2.0f * mirrorPos - t;
            if (mirrorT >= 0.0f) {
                float mt = std::fmod(mirrorT * repeats + warpShift, 1.0f);
                if (mt < 0.0f) mt += 1.0f;
                float mirrorVal = 0.0f;
                switch (shape) {
                case 0: mirrorVal = (mt < 0.12f) ? 1.0f : -1.0f; break;
                case 1: mirrorVal = (mt < 0.25f) ? 1.0f : -1.0f; break;
                case 2: mirrorVal = (mt < 0.50f) ? 1.0f : -1.0f; break;
                case 3: mirrorVal = (mt < 0.75f) ? 1.0f : -1.0f; break;
                case 4: mirrorVal = 2.0f * mt - 1.0f; break;
                case 5: mirrorVal = (mt < 0.5f) ? (4.0f * mt - 1.0f) : (3.0f - 4.0f * mt); break;
                case 6: mirrorVal = std::sin(mt * 6.2831853f); break;
                default: mirrorVal = val; break;
                }
                val = mirrorVal;
            }
        }

        m_wavBuf[i] = std::clamp(val, -1.0f, 1.0f);
    }
    m_wavBufLen = waveLen;
}

float SynthVoice::readWavBuf(const float* buf, int len, float phase) {
    float idx = phase * len;
    int i = static_cast<int>(idx) & (kWavBufSize - 1);
    float frac = idx - std::floor(idx);
    int next = (i + 1) & (kWavBufSize - 1);
    return buf[i] * (1.0f - frac) + buf[next] * frac;
}

SynthVoice::SynthVoice() {
    m_osc.Init(kSampleRate);
    m_osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    m_osc.SetAmp(1.0f);
    m_braidsOsc.Init();
    m_filter.Init(kSampleRate);
    m_gateStep = 1.0f / (kGateTime * kSampleRate);
    m_braidsReadIdx = 24;
    for (int i = 0; i < 24; ++i) m_braidsBuffer[i] = 0;
}

void SynthVoice::noteOn(float frequency, float volume, const Instrument* inst) {
    if (inst) m_instrument = inst;
    m_frequency = frequency;
    m_currentVolume = volume;
    m_active = true;
    m_finished = false;
    m_samplePhase = 0.0f;
    m_gateTarget = 1.0f;
    m_velocityTakeover = false;

    if (m_instrument && m_instrument->type == InstType::INST_SAMPLER) {
        m_sampler.computeRegion(m_instrument->sampler);
    }
    m_zdf.reset();
    m_braidsOsc.Strike();
    m_braidsReadIdx = 24;

    if (m_instrument && m_instrument->type == InstType::INST_FMSYNTH) {
        for (int i = 0; i < 4; ++i) {
            if (m_instrument->fm.ops[i].retrigger) {
                m_fmPhase[i] = 0.0f;
                m_fmPrevOut[i] = 0.0f;
            }
        }
    }

    if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
        m_wavPhase = 0;
        generateWavShape(m_instrument->wav, m_frequency);
    }

    for (int i = 0; i < 4; ++i) {
        if (m_instrument) {
            const Modulator& mod = m_instrument->mods[i];
            switch (mod.type) {
            case 0: m_ahdEnv[i].trigger(); break;
            case 1: m_adsrEnv[i].retrigger(); break;
            case 2: m_drumEnv[i].trigger(); break;
            case 3: m_lfo[i].trigger(); break;
            case 4: m_ahdEnv[i].trigger(); break;
            default: break;
            }
        } else {
            m_ahdEnv[i].trigger();
            m_lfo[i].trigger();
        }
    }
}

void SynthVoice::noteOff() {
    m_gateTarget = 0.0f;
    if (m_instrument) {
        for (int i = 0; i < 4; ++i) {
            if (m_instrument->mods[i].type == 1)
                m_adsrEnv[i].gate(false);
        }
    }
}

void SynthVoice::triggerModsWithSource(uint8_t src) {
    if (!m_instrument) return;
    for (int i = 0; i < 4; ++i) {
        const Modulator& mod = m_instrument->mods[i];
        if (mod.type == 4) {
            if (static_cast<uint8_t>(mod.p4) == src) {
                m_ahdEnv[i].trigger();
            }
        }
    }
}

void SynthVoice::setVolume(float v) { m_currentVolume = v; }

float SynthVoice::renderSample(const EnvContext& ctx) {
    if (!m_active && m_gate <= 0.0f) return 0.0f;

    if (m_gate < m_gateTarget)
        m_gate = std::min(m_gate + m_gateStep, m_gateTarget);
    else if (m_gate > m_gateTarget)
        m_gate = std::max(m_gate - m_gateStep, m_gateTarget);
    if (m_gate == 0.0f && m_gateTarget == 0.0f) { m_active = false; return 0.0f; }

    // Unimplemented instrument types (INST_NONE covers FMSynth/HyperSynth/
    // WavSynth/MIDIOut/External loaded from a song file, plus INST_MIDI) fall
    // through to the polyBLEP saw as a placeholder. Gate + volume at the end
    // of renderSample prevent droning.

    ModTargets mt{};
    float amtScale[4] = {1, 1, 1, 1};

    if (m_instrument) {
        bool noteOn = (m_gateTarget > 0.0f && m_gate < 1.0f && m_gate > 0.0f);
        for (int i = 0; i < 4; ++i) {
            const Modulator& mod = m_instrument->mods[i];
            if (static_cast<ModDest>(mod.dest) == ModDest::OFF) continue;

            float mod_val = 0.0f;
            switch (mod.type) {
            case 0: mod_val = m_ahdEnv[i].process(mod.p1, mod.p2, mod.p3, ctx); break;
            case 1: mod_val = m_adsrEnv[i].process(mod.p1, mod.p2, mod.p3, mod.p4, ctx); break;
            case 2: mod_val = m_drumEnv[i].process(mod.p1, mod.p2, mod.p3, ctx); break;
            case 3: mod_val = m_lfo[i].process(mod.p1, mod.p3, mod.p2, ctx, noteOn); break;
            case 4: mod_val = m_ahdEnv[i].process(mod.p1, mod.p2, mod.p3, ctx); break;
            case 5: {
                float src = 0.0f;
                switch (mod.p1) {
                case 0: src = m_frequency > 0.0f ? float(int(m_frequency * 1000.0f) % 128) / 127.0f : 0.0f; break;
                case 1: case 2: src = m_currentVolume; break;
                default: break;
                }
                float lo = mod.p3 / 255.0f, hi = mod.p4 / 255.0f;
                mod_val = lo + src * (hi - lo);
                if (mod.p1 == 2) m_velocityTakeover = true;
                break;
            }
            default: break;
            }

            float scaled = mod_val * amtScale[i] * bipolarAmt(mod.amt);

            auto dest = static_cast<ModDest>(mod.dest);
            switch (dest) {
            case ModDest::VOLUME: mt.volume += scaled; break;
            case ModDest::PITCH: mt.pitch += scaled; break;
            case ModDest::LOOP_ST: mt.loopSt += scaled; break;
            case ModDest::LENGTH: mt.length += scaled; break;
            case ModDest::DEGRADE: mt.degrade += scaled; break;
            case ModDest::CUTOFF: mt.cutoff += scaled; break;
            case ModDest::RES: mt.res += scaled; break;
            case ModDest::AMP: mt.amp += scaled; break;
            case ModDest::PAN: mt.pan += scaled; break;
            case ModDest::MOD_AMT: amtScale[(i + 1) & 3] = 1.0f + scaled; break;
            // MOD_RATE, and the rate half of MOD_BOTH/MOD_BINV, are not
            // implemented: there is no per-slot modulation rate to scale, so
            // only the amount half applies. Placeholder (AGENTS.md §8) --
            // do not "fix" this into a guessed rate-scaling behavior without
            // a hardware capture to verify against.
            case ModDest::MOD_RATE: break;
            case ModDest::MOD_BOTH: amtScale[(i + 1) & 3] = 1.0f + scaled; break;
            case ModDest::MOD_BINV: amtScale[(i + 1) & 3] = 1.0f + scaled; break;
            default: break;
            }
        }
    }

    if (m_instrument && m_instrument->type == InstType::INST_SAMPLER) {
        const SamplerState& s = m_instrument->sampler;
        const SampleData* sd = m_sampler.data();
        float dataSr = sd ? float(sd->sampleRate) : kSampleRate;
        float detuneSemis = (s.detune - 128) * kDetuneSemisPerStep;
        // Note tracking: the M8 sampler is chromatic with root C-4 (MIDI 60).
        // A note above the root plays the sample proportionally faster/higher;
        // the root note itself plays at natural pitch. m_frequency is the note's
        // frequency (0 only if never triggered). REPITCH/BPM play modes (09-0E,
        // not yet modeled) would derive pitch from tempo instead — see
        // M8_SAMPLER_COMPLETION_SPEC.md Phase 2.
        float noteSemis = 0.0f;
        if (m_frequency > 0.0f) {
            constexpr float kRootFreq = 440.0f * 0.5946035575f; // 440*2^((60-69)/12) = C-4
            noteSemis = 12.0f * std::log2(m_frequency / kRootFreq);
        }
        float semis = noteSemis + detuneSemis + mt.pitch * 12.0f;
        // Table transpose: add semitones from the table row
        semis += m_tableTranspose;
        float srRatio = dataSr / kSampleRate;
        float ratio = std::exp2(semis / 12.0f) * srRatio;
        ratio = std::clamp(ratio, 1e-4f, 32.0f);

        if (sd && sd->frames > 0 && !m_sampler.finished()) {
            int32_t frames = int32_t(sd->frames);
            int32_t ls = clampi(int32_t((s.loop_st / 255.0 + mt.loopSt / 255.0) * frames), 0, frames - 1);
            int32_t rawLen = int32_t((s.length / 255.0 + mt.length / 255.0) * frames);
            int32_t le = clampi(ls + rawLen, ls + 1, frames);
            m_sampler.setLoop(ls, le);
        }

        float sampOut[2] = {0.0f, 0.0f};
        m_sampler.render(ratio, sampOut);
        float sample = 0.5f * (sampOut[0] + sampOut[1]);

        m_samplePhase = float(m_sampler.phase());
        if (m_sampler.finished()) m_finished = true;

        if (s.degrade > 0 || mt.degrade > 0.0f) {
            float deg = std::clamp(s.degrade / 255.0f + mt.degrade, 0.0f, 1.0f);
            float step = 1.0f + deg * 63.0f;
            m_degradePhase += 1.0f;
            if (m_degradePhase >= step) { m_degradeHeld = sample; m_degradePhase -= step; }
            sample = m_degradeHeld;
        }

        float ampVal = std::clamp(1.0f + (s.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);

        int limMode = s.lim;
        int filterType = s.filter_type;
        float baseCutoff = 20.0f * std::pow(2.0f, (s.cutoff / 255.0f) * 10.0f);
        float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
        float finalRes = std::clamp(s.res / 255.0f + mt.res, 0.0f, 1.0f);

        // LIM 04-08 (POST modes) apply the AMP gain and its clipping AFTER the
        // filter stage (manual p.55: "amplification applied ... after the filter
        // stage"); LIM 00-03 amplify+shape first, then filter.
        if (limMode < 4) {
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
        } else {
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
        }

        float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
        float volMod = m_gate * (1.0f + mt.volume);
        if (volMod < 0.0f) volMod = 0.0f;
        return sample * effVol * volMod * m_tableVolume;
    }

    float sample = 0.0f;
    bool isBraids = false;
    bool isHyper = false;
    bool isFM = false;

    if (m_instrument && m_instrument->type == InstType::INST_MACROSYN) {
        const MacrosynState& s = m_instrument->macrosyn;
        if (s.shape >= 0 && s.shape <= 0x2B) {
            isBraids = true;
            m_braidsOsc.set_shape(static_cast<braids::MacroOscillatorShape>(s.shape));

            float f = m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);
            int16_t braids_pitch = 0;
            if (f > 0.0f) {
                braids_pitch = static_cast<int16_t>(std::clamp(1536.0f * std::log2(f) - 3120.1685f, 0.0f, 16383.0f));
            }
            m_braidsOsc.set_pitch(braids_pitch);

            int16_t timbre = static_cast<int16_t>((s.timbre / 255.0f) * 32767.0f);
            int16_t color = static_cast<int16_t>((s.color / 255.0f) * 32767.0f);
            m_braidsOsc.set_parameters(timbre, color);

            if (m_braidsReadIdx >= 24) {
                uint8_t sync_buffer[24];
                for (int i = 0; i < 24; ++i) sync_buffer[i] = 0;
                m_braidsOsc.Render(sync_buffer, m_braidsBuffer, 24);
                m_braidsReadIdx = 0;
            }
            int16_t braidsSample = m_braidsBuffer[m_braidsReadIdx++];
            sample = static_cast<float>(braidsSample) / 32768.0f;
        }
    }

    if (m_instrument && m_instrument->type == InstType::INST_HYPERSYN) {
        isHyper = true;
        const HyperState& h = m_instrument->hyper;

        float detuneSpread = (h.swarm / 255.0f) * 0.15f;
        float widthFactor = (h.width - 128) / 128.0f;

        float outL = 0.0f, outR = 0.0f;
        int activeNotes = 0;

        for (int n = 0; n < kHyperMaxNotes; ++n) {
            int midiNote = h.default_chord[n];
            if (midiNote <= 0 || midiNote >= 128) continue;
            midiNote += (h.shift - 128);
            midiNote = std::clamp(midiNote, 0, 127);
            float noteFreq = 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
            for (int v = 0; v < kHyperVoices; ++v) {
                float t = (kHyperVoices > 1) ? float(v) / float(kHyperVoices - 1) - 0.5f : 0.0f;
                float detuneSemi = t * detuneSpread * 2.0f;
                float freq = noteFreq * std::pow(2.0f, detuneSemi / 12.0f);
                float incF = freq / kSampleRate;
                uint32_t inc = static_cast<uint32_t>(incF * 4294967296.0f);

                m_hyperL[n][v].inc = inc;
                m_hyperL[n][v].phase += inc;
                float phaseL = static_cast<float>(m_hyperL[n][v].phase) / 4294967296.0f;
                float sawL = 2.0f * phaseL - 1.0f;
                sawL -= polyBLEP(phaseL, incF);

                float freqR = noteFreq * std::pow(2.0f, (detuneSemi + widthFactor * 0.3f) / 12.0f);
                float incRF = freqR / kSampleRate;
                uint32_t incR = static_cast<uint32_t>(incRF * 4294967296.0f);
                m_hyperR[n][v].inc = incR;
                m_hyperR[n][v].phase += incR;
                float phaseR = static_cast<float>(m_hyperR[n][v].phase) / 4294967296.0f;
                float sawR = 2.0f * phaseR - 1.0f;
                sawR -= polyBLEP(phaseR, incRF);

                outL += sawL;
                outR += sawR;
            }
            activeNotes++;
        }

        if (h.subosc > 0) {
            float subFreq = m_frequency * 0.5f;
            float subIncF = subFreq / kSampleRate;
            uint32_t subInc = static_cast<uint32_t>(subIncF * 4294967296.0f);
            m_hyperSub.inc = subInc;
            m_hyperSub.phase += subInc;
            float subPhase = static_cast<float>(m_hyperSub.phase) / 4294967296.0f;
            float subSaw = 2.0f * subPhase - 1.0f;
            subSaw -= polyBLEP(subPhase, subIncF);
            float subLevel = (h.subosc / 255.0f) * 0.7f;
            outL += subSaw * subLevel;
            outR += subSaw * subLevel;
        }

        float norm = (activeNotes > 0) ? 1.0f / float(activeNotes * kHyperVoices) : 1.0f;
        outL *= norm;
        outR *= norm;
        sample = 0.5f * (outL + outR);
    }

    if (m_instrument && m_instrument->type == InstType::INST_FMSYNTH) {
        isFM = true;
        if (!m_fmWavetableReady) initFMWavetables();

        const FMSynthState& fm = m_instrument->fm;
        float mod_values[4] = { float(fm.mod1), float(fm.mod2), float(fm.mod3), float(fm.mod4) };

        float noteFreq = m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);

        float opLevel[4] = {};
        float opFeedback[4] = {};
        int opShape[4] = {};
        float opFreq[4] = {};

        for (int i = 0; i < 4; ++i) {
            const auto& op = fm.ops[i];
            int dest_a = op.mod_a & 0x0F;
            int src_a = (op.mod_a >> 4) & 0x0F;
            float mod_val_a = (src_a < 4) ? (mod_values[src_a] - 128.0f) / 128.0f : 0.0f;

            int dest_b = op.mod_b & 0x0F;
            int src_b = (op.mod_b >> 4) & 0x0F;
            float mod_val_b = (src_b < 4) ? (mod_values[src_b] - 128.0f) / 128.0f : 0.0f;

            float effLevel = op.level / 255.0f;
            float effRatio = float(op.ratio) + float(op.ratio_fine) / 256.0f;
            float effFeedback = op.feedback / 255.0f;

            auto applyMod = [&](int dest, float mod_val) {
                switch (dest) {
                case 1: effLevel *= (mod_val + 1.0f) * 0.5f; break;
                case 2: effRatio += mod_val * 16.0f; break;
                case 4: effFeedback *= (mod_val + 1.0f) * 0.5f; break;
                default: break;
                }
            };
            applyMod(dest_a, mod_val_a);
            applyMod(dest_b, mod_val_b);

            opLevel[i] = std::clamp(effLevel, 0.0f, 1.0f);
            opFeedback[i] = std::clamp(effFeedback, 0.0f, 1.0f);
            opFreq[i] = noteFreq * std::clamp(effRatio, 0.0f, 32.0f);
            opShape[i] = std::clamp(op.shape, 0, kFMNumShapes - 1);
        }

        float opOut[4] = {};
        auto computeOp = [&](int idx, float modPhaseOffset) {
            float inc = opFreq[idx] / kSampleRate;
            m_fmPhase[idx] += inc;
            m_fmPhase[idx] -= std::floor(m_fmPhase[idx]);
            float fb = m_fmPrevOut[idx] * opFeedback[idx] * kFMFeedbackScale;
            float effectivePhase = m_fmPhase[idx] + modPhaseOffset + fb;
            effectivePhase -= std::floor(effectivePhase);
            opOut[idx] = readFMWavetable(m_fmWavetable[opShape[idx]], effectivePhase) * opLevel[idx];
        };

        switch (fm.algo) {
        case 0:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[1]); computeOp(3, opOut[2]); sample = opOut[3]; break;
        case 1:  computeOp(0, 0.0f); computeOp(1, 0.0f); computeOp(2, opOut[0] + opOut[1]); computeOp(3, opOut[2]); sample = opOut[3]; break;
        case 2:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, 0.0f); computeOp(3, opOut[1] + opOut[2]); sample = opOut[3]; break;
        case 3:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[0]); computeOp(3, opOut[1] + opOut[2]); sample = opOut[3]; break;
        case 4:  computeOp(0, 0.0f); computeOp(1, 0.0f); computeOp(2, 0.0f); computeOp(3, opOut[0] + opOut[1] + opOut[2]); sample = opOut[3]; break;
        case 5:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[1]); computeOp(3, 0.0f); sample = 0.5f * (opOut[2] + opOut[3]); break;
        case 6:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[1]); computeOp(3, opOut[1]); sample = 0.5f * (opOut[2] + opOut[3]); break;
        case 7:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, 0.0f); computeOp(3, 0.0f); sample = 0.5f * (opOut[1] + opOut[3]); break;
        case 8:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[0]); computeOp(3, opOut[0]); sample = (opOut[1] + opOut[2] + opOut[3]) / 3.0f; break;
        case 9:  computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[0]); computeOp(3, 0.0f); sample = (opOut[1] + opOut[2] + opOut[3]) / 3.0f; break;
        case 10: computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, 0.0f); computeOp(3, 0.0f); sample = (opOut[1] + opOut[2] + opOut[3]) / 3.0f; break;
        case 11: computeOp(0, 0.0f); computeOp(1, 0.0f); computeOp(2, 0.0f); computeOp(3, 0.0f); sample = (opOut[0] + opOut[1] + opOut[2] + opOut[3]) / 4.0f; break;
        default: computeOp(0, 0.0f); computeOp(1, opOut[0]); computeOp(2, opOut[1]); computeOp(3, opOut[2]); sample = opOut[3]; break;
        }

        for (int i = 0; i < 4; ++i) m_fmPrevOut[i] = opOut[i];
    }

    bool isWav = false;

    if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
        isWav = true;
        const WavSynthState& ws = m_instrument->wav;

        float noteFreq = m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);

        generateWavShape(ws, noteFreq);

        if (ws.filter_type >= 8 && ws.filter_type <= 11) {
            float baseCutoff = 20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f);
            float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
            float finalRes = std::clamp(ws.res / 255.0f + mt.res, 0.0f, 1.0f);
            m_filter.SetFreq(finalCutoff);
            m_filter.SetRes(finalRes);
            for (int i = 0; i < m_wavBufLen; ++i) {
                m_filter.Process(m_wavBuf[i]);
                switch (ws.filter_type) {
                case 8:  m_wavBuf[i] = m_filter.Low(); break;
                case 9:  m_wavBuf[i] = m_filter.High(); break;
                case 10: m_wavBuf[i] = m_filter.Band(); break;
                case 11: m_wavBuf[i] = m_wavBuf[i] - m_filter.Band(); break;
                }
            }
        }

        float incF = noteFreq / kSampleRate;
        m_wavPhase += static_cast<uint32_t>(incF * 4294967296.0f);
        float phase01 = static_cast<float>(m_wavPhase) / 4294967296.0f;

        sample = readWavBuf(m_wavBuf, m_wavBufLen, phase01);
    }

    if (!isBraids && !isHyper && !isFM && !isWav) {
        m_osc.SetFreq(m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f));
        sample = m_osc.Process();
    }

    if (m_instrument && m_instrument->type == InstType::INST_MACROSYN) {
        const MacrosynState& s = m_instrument->macrosyn;

        if (s.degrade > 0 || mt.degrade > 0.0f) {
            float deg = std::clamp(s.degrade / 255.0f + mt.degrade, 0.0f, 1.0f);
            float step = 1.0f + deg * 63.0f;
            m_degradePhase += 1.0f;
            if (m_degradePhase >= step) { m_degradeHeld = sample; m_degradePhase -= step; }
            sample = m_degradeHeld;
        }

        float ampVal = std::clamp(1.0f + (s.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
        sample *= ampVal;

        int limMode = s.lim;
        switch (limMode) {
        case 0: sample = std::clamp(sample, -1.0f, 1.0f); break;
        case 1: sample = std::sin(sample * 1.5707963f); break;
        case 2: sample = std::clamp(sample * 2.0f, -1.0f, 1.0f) - std::clamp(sample, -0.5f, 0.5f); break;
        case 3: { float x = sample - std::floor(sample); sample = x * 4.0f - 1.0f; } break;
        default: sample = std::clamp(sample, -1.0f, 1.0f); break;
        }

        int filterType = s.filter_type;
        float baseCutoff = 20.0f * std::pow(2.0f, (s.cutoff / 255.0f) * 10.0f);
        float baseRes = s.res / 255.0f;
        float finalCutoff = baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f);
        finalCutoff = std::clamp(finalCutoff, 20.0f, 20000.0f);
        float finalRes = std::clamp(baseRes + mt.res, 0.0f, 1.0f);

        if (filterType > 0) {
            m_filter.SetFreq(finalCutoff);
            m_filter.SetRes(finalRes);
            m_filter.Process(sample);
            if (filterType == 1) sample = m_filter.Low();
            else if (filterType == 2) sample = m_filter.High();
            else if (filterType == 3) sample = m_filter.Band();
            else if (filterType == 4) sample = sample - m_filter.Band();
        }
    }

    if (m_instrument && m_instrument->type == InstType::INST_HYPERSYN) {
        const HyperState& h = m_instrument->hyper;

        float ampVal = std::clamp(1.0f + (h.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
        int limMode = h.lim;
        int filterType = h.filter_type;
        float baseCutoff = 20.0f * std::pow(2.0f, (h.cutoff / 255.0f) * 10.0f);
        float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
        float finalRes = std::clamp(h.res / 255.0f + mt.res, 0.0f, 1.0f);

        if (limMode < 4) {
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
        } else {
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
        }
    }

    if (isFM) {
        const FMSynthState& fm = m_instrument->fm;
        float ampVal = std::clamp(1.0f + (fm.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
        int limMode = fm.lim;
        int filterType = fm.filter_type;
        float baseCutoff = 20.0f * std::pow(2.0f, (fm.cutoff / 255.0f) * 10.0f);
        float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
        float finalRes = std::clamp(fm.res / 255.0f + mt.res, 0.0f, 1.0f);

        if (limMode < 4) {
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
        } else {
            sample = applyFilter(sample, filterType, finalCutoff, finalRes);
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
        }
    }

    if (isWav) {
        const WavSynthState& ws = m_instrument->wav;
        float ampVal = std::clamp(1.0f + (ws.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
        int limMode = ws.lim;
        int filterType = ws.filter_type;
        float baseCutoff = 20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f);
        float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
        float finalRes = std::clamp(ws.res / 255.0f + mt.res, 0.0f, 1.0f);

        int stdFilter = (filterType >= 8) ? 0 : filterType;

        if (limMode < 4) {
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
            if (stdFilter > 0) sample = applyFilter(sample, stdFilter, finalCutoff, finalRes);
        } else {
            if (stdFilter > 0) sample = applyFilter(sample, stdFilter, finalCutoff, finalRes);
            sample *= ampVal;
            sample = applyLimiter(sample, limMode);
        }
    }

    float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
    float volMod = m_gate * (1.0f + mt.volume);
    if (volMod < 0.0f) volMod = 0.0f;
    return std::clamp(sample * effVol * volMod * m_tableVolume, -1.0f, 1.0f);
}

// FILTER dispatch. Types 1-4 use the DaisySP (non-ZDF) SVF; 6/7 use the ZDF SVF
// (ZdfFilter.h). Type 5 (LP>HP) is not modeled yet and passes through — see
// status.md Placeholders.
float SynthVoice::applyFilter(float in, int type, float cutoffHz, float res) {
    if (type <= 0) return in;
    if (type == 6 || type == 7) {              // ZDF LP / HP
        m_zdf.setParams(cutoffHz, res, kSampleRate);
        float hp = 0.0f;
        float lp = m_zdf.process(in, hp);
        return (type == 6) ? lp : hp;
    }
    if (type == 5) return in;                  // LP>HP: not modeled, pass through
    m_filter.SetFreq(cutoffHz);
    m_filter.SetRes(res);
    m_filter.Process(in);
    switch (type) {
    case 1: return m_filter.Low();
    case 2: return m_filter.High();
    case 3: return m_filter.Band();
    case 4: return in - m_filter.Band();       // band-stop (notch)
    default: return in;
    }
}

// LIM waveshaper / limiter. 00-03 are the pre-filter shapers; 04 (POST) and 05
// (POST:AD) are the post-filter hard/soft clippers. 06-08 (POST:W1-W3) are
// "folding distortions" whose exact transfer curves are not hardware-verified,
// so they fall back to hard clip rather than guess — see status.md.
float SynthVoice::applyLimiter(float x, int mode) {
    switch (mode) {
    case 0: return std::clamp(x, -1.0f, 1.0f);                                       // CLIP
    case 1: return std::sin(x * 1.5707963f);                                         // SIN
    case 2: return std::clamp(x * 2.0f, -1.0f, 1.0f) - std::clamp(x, -0.5f, 0.5f);   // FOLD
    case 3: { float f = x - std::floor(x); return f * 4.0f - 1.0f; }                 // WRAP
    case 4: return std::clamp(x, -1.0f, 1.0f);                                       // POST (hard)
    case 5: return std::tanh(x);                                                     // POST:AD (soft)
    default: return std::clamp(x, -1.0f, 1.0f);                                      // POST:W1-W3
    }
}

} // namespace engine
} // namespace m8
