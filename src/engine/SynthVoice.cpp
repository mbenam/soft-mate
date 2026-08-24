#include "SynthVoice.h"
#include "Engine.h"
#include "data/WavetableBank.h"
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

float SynthVoice::wavBaseShape(int shape, float u) {
    switch (shape) {
    case 0: return (u < 0.12f) ? 1.0f : -1.0f;   // PULSE 12%
    case 1: return (u < 0.25f) ? 1.0f : -1.0f;   // PULSE 25%
    case 2: return (u < 0.50f) ? 1.0f : -1.0f;   // PULSE 50%
    case 3: return (u < 0.75f) ? 1.0f : -1.0f;   // PULSE 75%
    case 4: return 2.0f * u - 1.0f;              // SAW
    case 5: return (u < 0.5f) ? (4.0f * u - 1.0f)
                              : (3.0f - 4.0f * u);  // TRIANGLE
    case 7: {                                    // NOISE PITCHED
        // A deterministic hash of the slot position, NOT a running LFSR: the
        // value must depend only on u, so the table is stable and the noise
        // repeats at the note pitch. That periodicity is what makes this shape
        // "in-tune tonal" rather than hiss.
        uint32_t s = static_cast<uint32_t>(u * 4294967296.0f);
        s ^= s >> 16; s *= 2246822519u;
        s ^= s >> 13; s *= 3266489917u;
        s ^= s >> 16;
        return static_cast<float>(s >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
    case 6:
    default: return std::sin(u * 6.2831853f);    // SINE (and shapes >= 9)
    }
}

float SynthVoice::wavWarpPhase(float u, float warp01) {
    if (warp01 <= 0.0f) return u;
    // Hardware-measured (fw 6.5.2): WARP follows a quartic response curve (1 - warp01)^4,
    // shifting the midpoint zero-crossing from 0.50 (at WARP 00) down to ~0.012 (at WARP FF).
    const float w = 1.0f - std::clamp(warp01, 0.0f, 1.0f);
    const float w2 = w * w;
    const float w4 = w2 * w2;
    constexpr float kMinPivot = 0.012f;
    const float pivot = kMinPivot + (0.5f - kMinPivot) * w4;
    if (u < pivot) return 0.5f * u / pivot;
    return 0.5f + 0.5f * (u - pivot) / (1.0f - pivot);
}

float SynthVoice::wavMirrorPhase(float u, float mirror) {
    if (mirror <= 0.0f || u <= mirror) return u;
    float v = 2.0f * mirror - u;
    v = std::fabs(v);
    v = std::fmod(v, 2.0f);
    if (v > 1.0f) v = 2.0f - v;
    return v;
}

bool SynthVoice::wavTableStale(const WavSynthState& ws) const {
    return m_wavKeyShape  != ws.shape  || m_wavKeySize   != ws.size
        || m_wavKeyMult   != ws.mult   || m_wavKeyWarp   != ws.warp
        || m_wavKeyScan   != ws.scan   || m_wavKeyFilter != ws.filter_type
        || m_wavKeyCutoff != ws.cutoff || m_wavKeyRes    != ws.res;
}

// One sample of wave table `wt`, frame `f`, at position u in [0,1).
static float wtFrameSample(int wt, int f, float u) {
    const float idx = u * kWavetableLength;
    int i = static_cast<int>(idx);
    if (i >= kWavetableLength) i = kWavetableLength - 1;
    const int j = (i + 1) % kWavetableLength;      // wraps: frames are cycles
    const float frac = idx - static_cast<float>(i);
    const float a = kWavetableData[wt][f][i] * (1.0f / 127.0f);
    const float b = kWavetableData[wt][f][j] * (1.0f / 127.0f);
    return a + (b - a) * frac;
}

void SynthVoice::regenerateWavTable(const WavSynthState& ws) {
    // SIZE is literally the number of samples in the wave table (manual:
    // "Horizontal size of the waveform (number of samples)"). It changes
    // resolution -- the lo-fi stepping that is the WavSynth's character --
    // NOT pitch: phase is normalised over the table whatever its length.
    const int len = std::clamp(ws.size, 2, kWavTableMax - 1);

    // MULT: 1.0 .. 16.9375 repeats of the shape inside the table.
    // Verified against hardware captures (fw 6.5.2): multFactor = 1.0f + mult / 16.0f,
    // providing continuous phase multiplication (cross-correlation > 0.96 with M8).
    const float multFactor = 1.0f + std::clamp(ws.mult, 0, 255) / 16.0f;
    const float warp01     = std::clamp(ws.warp, 0, 255) / 255.0f;

    const bool isWt  = (ws.shape >= 9 && ws.shape < 9 + kWavetableCount);
    const int  wt    = isWt ? (ws.shape - 9) : 0;
    const int  shape = isWt ? 0 : std::clamp(ws.shape, 0, 8);

    // Wave table shapes use SCAN as the frame morph, so there is no mirror.
    const float mirror = isWt ? 0.0f
                              : std::clamp(ws.scan, 0, 255) / 255.0f * 2.0f;

    float pos = 0.0f; int f0 = 0, f1 = 0; float mix = 0.0f;
    if (isWt) {
        pos = std::clamp(ws.scan, 0, 255) * (kWavetableFrames - 1) / 255.0f;
        f0  = static_cast<int>(pos);
        f1  = std::min(f0 + 1, kWavetableFrames - 1);
        mix = pos - static_cast<float>(f0);
    }

    for (int i = 0; i < len; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(len);
        u = wavMirrorPhase(u, mirror);          // SCAN, base shapes only
        u = wavWarpPhase(u, warp01);            // WARP
        u = u * multFactor;                     // MULT
        u = u - std::floor(u);

        float v;
        if (isWt) {
            v = wtFrameSample(wt, f0, u) * (1.0f - mix)
              + wtFrameSample(wt, f1, u) * mix;
        } else {
            v = wavBaseShape(shape, u);
        }
        m_wavTable[i] = std::clamp(v, -1.0f, 1.0f);
    }

    // FILTER 08-0B ("WAV LP/HP/BP/BS"): the manual says these apply the filter
    // *into the waveform*, so they are a buffer operation here, not an
    // output-stage one. The filter is configured at kSampleRate rather than at
    // the table's playback rate on purpose: the wave table is a buffer, and
    // filtering a buffer is pitch-independent -- which also keeps note pitch
    // out of the cache key, so pitch modulation cannot trigger per-sample
    // regeneration.
    if (ws.filter_type >= 8 && ws.filter_type <= 11) {
        const float cutoffHz = std::clamp(
            20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f), 20.0f, 20000.0f);
        m_wavShaper.SetFreq(cutoffHz);
        m_wavShaper.SetRes(std::clamp(ws.res / 255.0f, 0.0f, 1.0f));

        // Pass 1 warms the filter state over the loop so pass 2 comes out
        // periodic; without it the table starts from silence and the loop point
        // steps. Pass 1's output is deliberately discarded. No scratch buffer
        // is needed: Process() is called before each in-place write.
        for (int i = 0; i < len; ++i) m_wavShaper.Process(m_wavTable[i]);
        for (int i = 0; i < len; ++i) {
            m_wavShaper.Process(m_wavTable[i]);
            switch (ws.filter_type) {
            case 8:  m_wavTable[i] = m_wavShaper.Low();  break;
            case 9:  m_wavTable[i] = m_wavShaper.High(); break;
            case 10: m_wavTable[i] = m_wavShaper.Band(); break;
            case 11: m_wavTable[i] = m_wavTable[i] - m_wavShaper.Band(); break;
            }
        }
    }

    m_wavTable[len] = m_wavTable[0];   // guard sample -- fixes defect D2
    m_wavTableLen = len;

    m_wavKeyShape  = ws.shape;  m_wavKeySize   = ws.size;
    m_wavKeyMult   = ws.mult;   m_wavKeyWarp   = ws.warp;
    m_wavKeyScan   = ws.scan;   m_wavKeyFilter = ws.filter_type;
    m_wavKeyCutoff = ws.cutoff; m_wavKeyRes    = ws.res;
}

float SynthVoice::readWavTable(uint32_t phase) const {
    const float idx = (static_cast<float>(phase) * (1.0f / 4294967296.0f))
                    * static_cast<float>(m_wavTableLen);
    int i = static_cast<int>(idx);
    if (i >= m_wavTableLen) i = m_wavTableLen - 1;   // float-edge guard only
    const float frac = idx - static_cast<float>(i);
    return m_wavTable[i] + (m_wavTable[i + 1] - m_wavTable[i]) * frac;
}

// 32-bit Galois LFSR. Returns -1..+1. State must never be zero.
float SynthVoice::wavLfsrNext(uint32_t& state) {
    const uint32_t lsb = state & 1u;
    state >>= 1;
    if (lsb) state ^= 0xD0000001u;
    return static_cast<float>(state >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

SynthVoice::SynthVoice() {
    m_osc.Init(kSampleRate);
    m_osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    m_osc.SetAmp(1.0f);
    m_braidsOsc.Init();
    m_filter.Init(kSampleRate);
    m_filterR.Init(kSampleRate);   // right channel, stereo sampler path
    m_wavShaper.Init(kSampleRate);
    m_gateStep = 1.0f / (kGateTime * kSampleRate);
    m_braidsReadIdx = 24;
    for (int i = 0; i < 24; ++i) m_braidsBuffer[i] = 0;
}

void SynthVoice::noteOn(float frequency, float volume, const Instrument* inst, int noteMidi) {
    if (inst) m_instrument = inst;
    m_frequency = frequency;
    m_currentVolume = volume;
    m_active = true;
    m_samplePhase = 0.0f;
    m_gateTarget = 1.0f;
    m_velocityTakeover = false;

    if (m_instrument && m_instrument->type == InstType::INST_SAMPLER) {
        m_sampler.computeRegion(m_instrument->sampler, noteMidi);
    }
    m_zdf.reset();
    m_zdfR.reset();
    m_degradeHeldR = 0.0f;
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
        m_wavNoiseLfsr = 1u;                       // determinism, see below
        regenerateWavTable(m_instrument->wav);
    }

    for (int i = 0; i < 4; ++i) {
        m_modP1Offset[i] = 0;
        m_modP2Offset[i] = 0;
        m_modP3Offset[i] = 0;
        m_modAmtOffset[i] = 0;
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

void SynthVoice::retriggerEnv(int slot) {
    if (slot < 0 || slot >= 4) return;
    m_ahdEnv[slot].trigger();
    m_adsrEnv[slot].retrigger();
    m_drumEnv[slot].trigger();
}

void SynthVoice::retriggerLfo(int slot, float phaseOffset) {
    if (slot < 0 || slot >= 4) return;
    m_lfo[slot].setPhase(std::clamp(static_cast<double>(phaseOffset), 0.0, 1.0));
}

void SynthVoice::setModOffset(int slot, int p1, int p2, int p3, int amt) {
    if (slot < 0 || slot >= 4) return;
    m_modP1Offset[slot] = static_cast<int8_t>(std::clamp(p1, -128, 127));
    m_modP2Offset[slot] = static_cast<int8_t>(std::clamp(p2, -128, 127));
    m_modP3Offset[slot] = static_cast<int8_t>(std::clamp(p3, -128, 127));
    m_modAmtOffset[slot] = static_cast<int8_t>(std::clamp(amt, -128, 127));
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
    // Rate scaling for MOD_RATE / MOD_BOTH / MOD_BINV, written ahead to slot
    // i+1 the same way amtScale is. See the destination switch for the law.
    float rateScale[4] = {1, 1, 1, 1};

    if (m_instrument) {
        bool noteOn = (m_gateTarget > 0.0f && m_gate < 1.0f && m_gate > 0.0f);
        for (int i = 0; i < 4; ++i) {
            const Modulator& mod = m_instrument->mods[i];
            if (static_cast<ModDest>(mod.dest) == ModDest::OFF) continue;

            uint8_t p1 = static_cast<uint8_t>(std::clamp(static_cast<int>(mod.p1) + m_modP1Offset[i], 0, 255));
            uint8_t p2 = static_cast<uint8_t>(std::clamp(static_cast<int>(mod.p2) + m_modP2Offset[i], 0, 255));
            uint8_t p3 = static_cast<uint8_t>(std::clamp(static_cast<int>(mod.p3) + m_modP3Offset[i], 0, 255));
            uint8_t amt = static_cast<uint8_t>(std::clamp(static_cast<int>(mod.amt) + m_modAmtOffset[i], 0, 255));

            float mod_val = 0.0f;
            switch (mod.type) {
            case 0: mod_val = m_ahdEnv[i].process(p1, p2, p3, ctx); break;
            case 1: mod_val = m_adsrEnv[i].process(p1, p2, p3, mod.p4, ctx); break;
            case 2: mod_val = m_drumEnv[i].process(p1, p2, p3, ctx); break;
            case 3: {
                // Lfo::process takes a PERIOD byte, not a frequency: period =
                // freq * samplesPerStep, so a bigger byte is a slower LFO.
                // Scaling the rate therefore divides it.
                uint8_t rateByte = p3;
                if (rateScale[i] > 0.0f && rateScale[i] != 1.0f) {
                    const float scaledByte = float(p3) / rateScale[i];
                    rateByte = static_cast<uint8_t>(std::clamp(scaledByte + 0.5f, 1.0f, 255.0f));
                }
                mod_val = m_lfo[i].process(p1, rateByte, p2, ctx, noteOn);
                break;
            }
            case 4: mod_val = m_ahdEnv[i].process(p1, p2, p3, ctx); break;
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

            float scaled = mod_val * amtScale[i] * bipolarAmt(amt);

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
            // The rate half, implemented 2026-08-24. It was a placeholder on the
            // grounds that there was no per-slot rate to scale -- but there is:
            // an LFO slot's p3 is its period byte, and dividing it speeds the
            // slot up.
            //
            // The LAW is chosen, not measured. MOD_AMT multiplies the target's
            // amount by (1 + scaled), so MOD_RATE multiplies its rate by the
            // same factor, which divides the period. That is symmetry with the
            // neighbour rather than a captured curve, and the M8's actual
            // scaling could differ in shape. It is written down here instead of
            // in a comment claiming measurement.
            //
            // MOD_BINV differs from MOD_BOTH only in the rate half's sign --
            // "both, inverted" -- so it speeds up where BOTH slows down. Also an
            // assumption, and the one most worth checking first if these sound
            // wrong.
            case ModDest::MOD_RATE: rateScale[(i + 1) & 3] = 1.0f + scaled; break;
            case ModDest::MOD_BOTH:
                amtScale[(i + 1) & 3]  = 1.0f + scaled;
                rateScale[(i + 1) & 3] = 1.0f + scaled;
                break;
            case ModDest::MOD_BINV:
                amtScale[(i + 1) & 3]  = 1.0f + scaled;
                rateScale[(i + 1) & 3] = 1.0f - scaled;
                break;
            default: break;
            }
        }
    }

    if (m_instrument && m_instrument->type == InstType::INST_SAMPLER) {
        const SamplerState& s = m_instrument->sampler;
        const SampleData* sd = m_sampler.data();
        float dataSr = sd ? float(sd->sampleRate) : kSampleRate;
        float ratio = 1.0f;
        float srRatio = dataSr / kSampleRate;
        if (s.play >= 9 && s.play <= 11) {
            // REPITCH modes (09 REPITCH, 0A REP.REV, 0B REP.PP):
            // s.detune stores STEPS (1..255, default 0x80 = 128).
            // Loop period scales proportionally with STEPS and inversely with tempo.
            float steps = (s.detune > 0) ? float(s.detune) : 128.0f;
            // MEASURED on hardware 2026-08-24 (fw 6.5.2, COM3, instrument 09
            // in REPITCH, keyjazz C-4, period averaged over ~20 repeats):
            //
            //   STEPS  BPM   period       in beats
            //   0x40   140   5140.4 smp   0.2499
            //   0x40    90   7996.0 smp   0.2499
            //   0x80   140  10157.8 smp   0.4938
            //
            // Linear in STEPS, inversely proportional to BPM, and the loop is
            // STEPS/256 of a BEAT -- so 0x80, the default, is an eighth note.
            // At 48 kHz a beat is 24 * samplesPerTick, so the constant is
            // 24/256 = 3/32. It was 0.25, which ran 2.67x long; predictions with
            // 3/32 land within 0.05% of the two clean measurements.
            constexpr float kRepitchStepsPerTick = 3.0f / 32.0f;
            float loopSamples = steps * (float(ctx.samplesPerTick) * kRepitchStepsPerTick);
            if (loopSamples < 1.0f) loopSamples = 1.0f;
            float sampleFrames = sd ? float(sd->frames) : 1.0f;
            ratio = (sampleFrames / loopSamples) * srRatio;
            float modSemis = mt.pitch * 12.0f + m_tableTranspose;
            if (std::abs(modSemis) > 0.001f) ratio *= std::exp2(modSemis / 12.0f);
        } else if (s.play >= 12 && s.play <= 14) {
            // BPM modes (0C REP.BPM, 0D BPM.REV, 0E BPM.PP):
            // s.detune stores sample base BPM (default 0x78 = 120 or 0x80 = 128).
            float sampleBpm = (s.detune > 0) ? float(s.detune) : 120.0f;
            float songBpm = float(120000.0 / ctx.samplesPerTick);
            ratio = (songBpm / sampleBpm) * srRatio;
            float modSemis = mt.pitch * 12.0f + m_tableTranspose;
            if (std::abs(modSemis) > 0.001f) ratio *= std::exp2(modSemis / 12.0f);
        } else {
            // Chromatic note tracking: root C-4 (MIDI 60)
            float detuneSemis = (s.detune - 128) * kDetuneSemisPerStep;
            float noteSemis = 0.0f;
            if (m_frequency > 0.0f) {
                constexpr float kRootFreq = 440.0f * 0.5946035575f; // 440*2^((60-69)/12) = C-4
                noteSemis = 12.0f * std::log2(m_frequency / kRootFreq);
            }
            float semis = noteSemis + detuneSemis + mt.pitch * 12.0f + m_tableTranspose;
            ratio = std::exp2(semis / 12.0f) * srRatio;
        }
        ratio = std::clamp(ratio, 1e-4f, 32.0f);

        if (sd && sd->frames > 0 && !m_sampler.finished() && s.slice == 0) {
            int32_t frames = int32_t(sd->frames);
            int32_t ls = clampi(int32_t((s.loop_st / 255.0 + mt.loopSt / 255.0) * frames), 0, frames - 1);
            int32_t rawLen = int32_t((s.length / 255.0 + mt.length / 255.0) * frames);
            int32_t le = clampi(ls + rawLen, ls + 1, frames);
            m_sampler.setLoop(ls, le);
        }

        float sampOut[2] = {0.0f, 0.0f};
        m_sampler.render(ratio, sampOut);
        // Mono form: kept for renderSample's contract. The stereo path in
        // renderFrame carries both channels through instead -- see
        // hw_findings.md §UI-12 for why that matters.
        float sample = 0.5f * (sampOut[0] + sampOut[1]);

        m_samplePhase = float(m_sampler.phase());

        float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
        float volMod = m_gate * (1.0f + mt.volume);
        if (volMod < 0.0f) volMod = 0.0f;

        if (m_frameStereo) {
            // Stereo: carry both channels through the whole output stage. The M8
            // reproduces a stereo sample's image intact (hw_findings.md §UI-12),
            // so summing here threw away information the hardware keeps.
            float l = sampOut[0], r = sampOut[1];
            applyDegradeStereo(l, r, s.degrade, mt.degrade);
            applyAmpLimFilterStereo(l, r, s.volume, s.lim, s.filter_type,
                                    s.cutoff, s.res, mt);
            const float g = effVol * volMod * m_tableVolume;
            m_frameOut[0] = l * g;
            m_frameOut[1] = r * g;
            m_frameFilled = true;
            // Return the mono mix too, so a direct renderSample caller that
            // somehow saw m_frameStereo raised still gets a sane value.
            return 0.5f * (m_frameOut[0] + m_frameOut[1]);
        }

        sample = applyDegrade(sample, s.degrade, mt.degrade);
        sample = applyAmpLimFilter(sample, s.volume, s.lim, s.filter_type,
                                   s.cutoff, s.res, mt);
        return sample * effVol * volMod * m_tableVolume;
    }

    float sample = 0.0f;
    bool isBraids = false;
    bool isHyper = false;
    bool isFM = false;

    if (m_instrument && m_instrument->type == InstType::INST_MACROSYN) {
        const MacrosynState& s = m_instrument->macrosyn;
        // 0x2F, not 0x2B. Read off the device 2026-08-24: stepping SHAPE to its
        // ceiling on a real M8 (fw 6.5.2) stops at 0x2F, which it names "MORSE
        // NOISE". The vendored Braids enum ends at the same place --
        // MACRO_OSC_SHAPE_QUESTION_MARK is 0x2F and LAST is 0x30 -- so the two
        // agree exactly and the old cap was simply four models short:
        // GRANULAR_CLOUD, PARTICLE_NOISE, DIGITAL_MODULATION and QUESTION_MARK
        // were vendored, compiled and unreachable.
        if (s.shape >= 0 && s.shape <= 0x2F) {
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
        int bank = std::clamp(h.chord_bank, 0, 15);

        float baseFreq = m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);

        float gainLower = 1.0f;
        float gainUpper = 1.0f;
        if (h.shift < 0x80) {
            gainUpper = h.shift / 128.0f;
        } else if (h.shift > 0x80) {
            gainLower = (255 - h.shift) / 127.0f;
        }

        float detuneSpread = (h.swarm / 255.0f) * 0.25f;
        // 0.000754 semitones at FF, not 0.05. Calibrated 2026-08-24 against the
        // device: hw_findings.md UI-11 measured side/mid = 0.029 at maximum
        // WIDTH, and a sweep of this engine put 0.029 at byte 4 of 255 on the
        // old constant -- i.e. the spread was ~65x too wide, and FF sounded
        // near-decorrelated where the M8 is subtly wide. Scaled so FF lands on
        // the measured ratio. `[hypersynth]` asserts it.
        float widthSpread = (h.width / 255.0f) * 0.000754f;

        float outL = 0.0f, outR = 0.0f;
        int activeNotes = 0;

        for (int n = 0; n < 6; ++n) {
            float noteGain = (n < 3) ? gainLower : gainUpper;
            if (noteGain <= 0.0001f) continue;

            int interval = h.chords[bank][n];
            float noteFreq = baseFreq * std::pow(2.0f, interval / 12.0f);

            for (int v = 0; v < 2; ++v) {
                float detuneSemi = (v == 0) ? -detuneSpread : detuneSpread;
                float freqL = noteFreq * std::pow(2.0f, (detuneSemi - widthSpread) / 12.0f);
                float incLF = freqL / kSampleRate;
                uint32_t incL = static_cast<uint32_t>(incLF * 4294967296.0f);

                m_hyperL[n][v].inc = incL;
                m_hyperL[n][v].phase += incL;
                float phaseL = static_cast<float>(m_hyperL[n][v].phase) / 4294967296.0f;
                float sawL = 2.0f * phaseL - 1.0f;
                sawL -= polyBLEP(phaseL, incLF);

                float freqR = noteFreq * std::pow(2.0f, (detuneSemi + widthSpread) / 12.0f);
                float incRF = freqR / kSampleRate;
                uint32_t incR = static_cast<uint32_t>(incRF * 4294967296.0f);

                m_hyperR[n][v].inc = incR;
                m_hyperR[n][v].phase += incR;
                float phaseR = static_cast<float>(m_hyperR[n][v].phase) / 4294967296.0f;
                float sawR = 2.0f * phaseR - 1.0f;
                sawR -= polyBLEP(phaseR, incRF);

                outL += sawL * noteGain;
                outR += sawR * noteGain;
            }
            activeNotes++;
        }

        if (h.subosc > 0) {
            float subRatio = (h.subosc < 0x80) ? 0.25f : 0.5f;
            float subFreq = baseFreq * subRatio;
            float subIncF = subFreq / kSampleRate;
            uint32_t subInc = static_cast<uint32_t>(subIncF * 4294967296.0f);
            m_hyperSub.inc = subInc;
            m_hyperSub.phase += subInc;
            float subPhase = static_cast<float>(m_hyperSub.phase) / 4294967296.0f;
            float subSq = (subPhase < 0.5f) ? 1.0f : -1.0f;
            subSq += polyBLEP(subPhase, subIncF);
            subSq -= polyBLEP(std::fmod(subPhase + 0.5f, 1.0f), subIncF);

            float subVol = (h.subosc == 0x80) ? 0.5f : ((h.subosc & 0x7F) / 127.0f) * 0.5f;
            outL += subSq * subVol;
            outR += subSq * subVol;
        }

        float norm = (activeNotes > 0) ? 1.0f / float(activeNotes * 2) : 1.0f;
        outL *= norm;
        outR *= norm;
        sample = 0.5f * (outL + outR);
        // WIDTH detunes the left and right stacks in opposite directions, so
        // outL and outR genuinely differ -- and collapsing them here threw that
        // away. Measured 2026-08-14 (hw_findings.md UI-11): two probes differing
        // only in WIDTH captured off the device as exactly mono at 00 (side RMS
        // 0.000000, corr 1.0000) and genuinely stereo at FF (side RMS 0.002136,
        // corr 0.9984). Through m8_render both read 0.000086 -- no response at
        // all. Keep them for the stereo path; `sample` stays the mono mix so
        // renderSample's contract is unchanged.
        m_hyperStereo = true;
        m_hyperL_out = outL;
        m_hyperR_out = outR;
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
            float opPitchOffset = 0.0f;

            auto applyMod = [&](int dest, float mod_val) {
                switch (dest) {
                case 1: effLevel *= (mod_val + 1.0f) * 0.5f; break;
                case 2: effRatio += mod_val * 16.0f; break;
                case 3: opPitchOffset += mod_val * 24.0f; break;
                case 4: effFeedback *= (mod_val + 1.0f) * 0.5f; break;
                default: break;
                }
            };
            applyMod(dest_a, mod_val_a);
            applyMod(dest_b, mod_val_b);

            opLevel[i] = std::clamp(effLevel, 0.0f, 1.0f);
            opFeedback[i] = std::clamp(effFeedback, 0.0f, 1.0f);
            opFreq[i] = noteFreq * std::pow(2.0f, opPitchOffset / 12.0f) * std::clamp(effRatio, 0.0f, 32.0f);
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

        if (ws.shape == 8) {
            // NOISE: classic LFSR noise, clocked once per output sample and so
            // independent of note pitch. Shape 07 (NOISE PITCHED) is the tonal
            // one and goes through the table like every other shape.
            sample = wavLfsrNext(m_wavNoiseLfsr);
        } else {
            if (wavTableStale(ws)) regenerateWavTable(ws);

            const float noteFreq = m_frequency
                * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);
            m_wavPhase += static_cast<uint32_t>(
                (noteFreq / kSampleRate) * 4294967296.0f);
            sample = readWavTable(m_wavPhase);
        }
    }

    if (!isBraids && !isHyper && !isFM && !isWav) {
        m_osc.SetFreq(m_frequency * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f));
        sample = m_osc.Process();
    }

    if (m_instrument && m_instrument->type == InstType::INST_MACROSYN) {
        const MacrosynState& s = m_instrument->macrosyn;

        sample = applyDegrade(sample, s.degrade, mt.degrade);

        if (s.redux > 0) {
            float bits = 16.0f - (s.redux / 255.0f) * 14.0f;
            float steps = std::pow(2.0f, bits);
            sample = std::round(sample * steps) / steps;
        }

        sample = applyAmpLimFilter(sample, s.volume, s.lim, s.filter_type, s.cutoff, s.res, mt);
    }

    if (m_instrument && m_instrument->type == InstType::INST_HYPERSYN) {
        const HyperState& h = m_instrument->hyper;
        if (m_frameStereo && m_hyperStereo) {
            float l = m_hyperL_out, r = m_hyperR_out;
            applyAmpLimFilterStereo(l, r, h.volume, h.lim, h.filter_type,
                                    h.cutoff, h.res, mt);
            const float effVolS = m_velocityTakeover ? 1.0f : m_currentVolume;
            float volModS = m_gate * (1.0f + mt.volume);
            if (volModS < 0.0f) volModS = 0.0f;
            const float g = effVolS * volModS * m_tableVolume;
            m_frameOut[0] = std::clamp(l * g, -1.0f, 1.0f);
            m_frameOut[1] = std::clamp(r * g, -1.0f, 1.0f);
            m_frameFilled = true;
            return 0.5f * (m_frameOut[0] + m_frameOut[1]);
        }
        sample = applyAmpLimFilter(sample, h.volume, h.lim, h.filter_type,
                                   h.cutoff, h.res, mt);
    }

    if (isFM) {
        const FMSynthState& fm = m_instrument->fm;
        sample = applyAmpLimFilter(sample, fm.volume, fm.lim, fm.filter_type,
                                   fm.cutoff, fm.res, mt);
    }

    if (isWav) {
        const WavSynthState& ws = m_instrument->wav;
        // WAV filter modes 8-11 are applied into the wavetable buffer above,
        // not here, so they map to "no output-stage filter". applyFilter
        // returns its input untouched for type <= 0 (and touches no filter
        // state), which is exactly what the previous `if (stdFilter > 0)`
        // guard did.
        int stdFilter = (ws.filter_type >= 8) ? 0 : ws.filter_type;
        sample = applyAmpLimFilter(sample, ws.volume, ws.lim, stdFilter,
                                   ws.cutoff, ws.res, mt);
    }

    float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
    float volMod = m_gate * (1.0f + mt.volume);
    if (volMod < 0.0f) volMod = 0.0f;
    return std::clamp(sample * effVol * volMod * m_tableVolume, -1.0f, 1.0f);
}

// DEGRADE: sample-and-hold decimator, up to 1/64 rate. Extracted verbatim from
// the byte-identical copies that lived in the sampler and macrosyn render paths
// (ARCHITECTURE.md §5.2 #8) -- same expressions in the same order, so the
// output is unchanged for both callers.
void SynthVoice::renderFrame(const EnvContext& ctx, float out[2]) {
    m_frameStereo = true;
    m_frameFilled = false;
    const float mono = renderSample(ctx);
    m_frameStereo = false;
    if (m_frameFilled) {
        out[0] = m_frameOut[0];
        out[1] = m_frameOut[1];
    } else {
        out[0] = out[1] = mono;
    }
}

void SynthVoice::applyDegradeStereo(float& l, float& r, int degradeByte, float degradeMod) {
    if (degradeByte > 0 || degradeMod > 0.0f) {
        float deg = std::clamp(degradeByte / 255.0f + degradeMod, 0.0f, 1.0f);
        float step = 1.0f + deg * 63.0f;
        // ONE phase for both channels: the decimator's clock is shared. Giving
        // each channel its own phase would latch L and R at different instants
        // and invent stereo from a mono source.
        m_degradePhase += 1.0f;
        if (m_degradePhase >= step) {
            m_degradeHeld  = l;
            m_degradeHeldR = r;
            m_degradePhase -= step;
        }
        l = m_degradeHeld;
        r = m_degradeHeldR;
    }
}

float SynthVoice::applyFilterR(float in, int type, float cutoffHz, float res) {
    // Mirror of applyFilter against the right channel's own filter state. Kept as
    // a twin rather than parameterised so the left/mono path's instruction stream
    // is untouched.
    if (type <= 0) return in;
    if (type == 6 || type == 7) {
        m_zdfR.setParams(cutoffHz, res, kSampleRate);
        float hp = 0.0f;
        float lp = m_zdfR.process(in, hp);
        return (type == 6) ? lp : hp;
    }
    if (type == 5) return in;
    m_filterR.SetFreq(cutoffHz);
    m_filterR.SetRes(res);
    m_filterR.Process(in);
    switch (type) {
    case 1: return m_filterR.Low();
    case 2: return m_filterR.High();
    case 3: return m_filterR.Band();
    case 4: return in - m_filterR.Band();
    default: return in;
    }
}

void SynthVoice::applyAmpLimFilterStereo(float& l, float& r, int volumeByte, int limMode,
                                         int filterType, int cutoffByte, int resByte,
                                         const ModTargets& mt) {
    // Same maths as applyAmpLimFilter, including the LIM 04-08 ordering flip,
    // applied to each channel with its own filter state.
    float ampVal = std::clamp(1.0f + (volumeByte / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
    float baseCutoff = 20.0f * std::pow(2.0f, (cutoffByte / 255.0f) * 10.0f);
    float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
    float finalRes = std::clamp(resByte / 255.0f + mt.res, 0.0f, 1.0f);

    if (limMode < 4) {
        l *= ampVal;
        r *= ampVal;
        l = applyLimiter(l, limMode);
        r = applyLimiter(r, limMode);
        l = applyFilter(l, filterType, finalCutoff, finalRes);
        r = applyFilterR(r, filterType, finalCutoff, finalRes);
        return;
    }
    l = applyFilter(l, filterType, finalCutoff, finalRes);
    r = applyFilterR(r, filterType, finalCutoff, finalRes);
    l *= ampVal;
    r *= ampVal;
    l = applyLimiter(l, limMode);
    r = applyLimiter(r, limMode);
}

float SynthVoice::applyDegrade(float in, int degradeByte, float degradeMod) {
    if (degradeByte > 0 || degradeMod > 0.0f) {
        float deg = std::clamp(degradeByte / 255.0f + degradeMod, 0.0f, 1.0f);
        float step = 1.0f + deg * 63.0f;
        m_degradePhase += 1.0f;
        if (m_degradePhase >= step) { m_degradeHeld = in; m_degradePhase -= step; }
        in = m_degradeHeld;
    }
    return in;
}

// VOLUME -> LIM -> FILTER output stage, used by the sampler, MACROSYN, hyper,
// FM and wav paths -- all five (ARCHITECTURE.md 5.2 #8).
//
// The gain here is driven by the instrument's VOLUME byte, not by AMP.
// Until 2026-08-19 it was passed `amp`, but SongIO was loading `amp` FROM the
// volume byte -- so this curve has always been fed volume, and the parameter was
// merely misnamed all the way down. With the byte map corrected (AGENTS.md 7,
// AMP is `amp_type` and LIM is `amp_limit`), passing `amp` here would have
// silently dropped the volume control and made every loaded song far quieter.
// So the pairing is kept and the name fixed: same audible behaviour as before,
// with the limiter mode now coming from the right byte.
//
// AMP itself is therefore NOT applied. That is closer to the device than what
// this did before: sweeping AMP on hardware moved the output -0.02 dB at
// LIM 00 CLIP (i.e. nothing) and -23 dB at LIM 08 POST:W3, which is a drive
// into the saturator, not an output gain. Modelling it as up to +18 dB of gain
// was the wrong shape in every mode. Its real curve is unmeasured -- status.md.
//
// This block used to end by saying the macrosyn path was deliberately excluded.
// It is NOT: the INST_MACROSYN branch calls this helper like every other type.
// Verified against the call site before deleting the claim, 2026-08-19.
//
// LIM 04-08 (POST modes) apply the AMP gain and its clipping AFTER the filter
// stage (manual p.55: "amplification applied ... after the filter stage");
// LIM 00-03 amplify+shape first, then filter.
float SynthVoice::applyAmpLimFilter(float in, int volumeByte, int limMode, int filterType,
                                    int cutoffByte, int resByte, const ModTargets& mt) {
    float ampVal = std::clamp(1.0f + (volumeByte / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
    float baseCutoff = 20.0f * std::pow(2.0f, (cutoffByte / 255.0f) * 10.0f);
    float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
    float finalRes = std::clamp(resByte / 255.0f + mt.res, 0.0f, 1.0f);

    if (limMode < 4) {
        in *= ampVal;
        in = applyLimiter(in, limMode);
        return applyFilter(in, filterType, finalCutoff, finalRes);
    }
    in = applyFilter(in, filterType, finalCutoff, finalRes);
    in *= ampVal;
    return applyLimiter(in, limMode);
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
