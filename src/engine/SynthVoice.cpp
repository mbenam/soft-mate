#include "SynthVoice.h"
#include "Engine.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace m8 {
namespace engine {

SynthVoice::SynthVoice() {
    m_osc.Init(kSampleRate);
    m_osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    m_osc.SetAmp(1.0f);
    m_filter.Init(kSampleRate);
    m_gateStep = 1.0f / (kGateTime * kSampleRate);
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

    // Instrument types the engine doesn't synthesize (INST_NONE covers
    // FMSynth/HyperSynth/WavSynth/MIDIOut/External loaded from a song file,
    // plus INST_MIDI) must not fall through to the default oscillator below —
    // that path has no volume envelope applied by the file conversion, so an
    // un-gated note would drone at full amplitude for as long as it's held.
    if (m_instrument) {
        const InstType it = m_instrument->type;
        if (it != InstType::INST_SAMPLER && it != InstType::INST_MACROSYN) return 0.0f;
    }

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
        float semis = detuneSemis + mt.pitch * 12.0f;
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

        bool postFilter = (limMode >= 4);
        if (postFilter && filterType > 0) {
            m_filter.SetFreq(finalCutoff);
            m_filter.SetRes(finalRes);
            m_filter.Process(sample);
            if (filterType == 1) sample = m_filter.Low();
            else if (filterType == 2) sample = m_filter.High();
            else if (filterType == 3) sample = m_filter.Band();
            else if (filterType == 4) sample = sample - m_filter.Band();
        } else if (!postFilter && filterType > 0) {
            m_filter.SetFreq(finalCutoff);
            m_filter.SetRes(finalRes);
            m_filter.Process(sample);
            if (filterType == 1) sample = m_filter.Low();
            else if (filterType == 2) sample = m_filter.High();
            else if (filterType == 3) sample = m_filter.Band();
            else if (filterType == 4) sample = sample - m_filter.Band();
        }

        float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
        float volMod = m_gate * (1.0f + mt.volume);
        if (volMod < 0.0f) volMod = 0.0f;
        return sample * effVol * volMod;
    }

    m_osc.SetFreq(m_frequency * std::pow(2.0f, mt.pitch / 12.0f));
    float sample = m_osc.Process();

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

    float effVol = m_velocityTakeover ? 1.0f : m_currentVolume;
    float volMod = m_gate * (1.0f + mt.volume);
    if (volMod < 0.0f) volMod = 0.0f;
    return std::clamp(sample * effVol * volMod, -1.0f, 1.0f);
}

} // namespace engine
} // namespace m8
