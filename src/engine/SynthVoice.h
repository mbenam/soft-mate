#pragma once

#include "SamplePool.h"
#include "Envelopes.h"
#include "Lfo.h"
#include "Modulation.h"
#include "SamplerEngine.h"
#include "daisysp.h"

namespace m8 {
namespace engine {

struct Instrument;

class SynthVoice {
public:
    SynthVoice();
    ~SynthVoice() = default;

    void setInstrument(const Instrument* inst) { m_instrument = inst; }
    void noteOn(float frequency, float volume, const Instrument* inst = nullptr);
    void noteOff();
    void setSample(const SampleData* s) { m_sampler.setSample(s); }
    void setVolume(float v);
    void setLoop(int32_t loopStart, int32_t loopEnd) { m_sampler.setLoop(loopStart, loopEnd); }

    float renderSample(const EnvContext& ctx);
    bool isActive() const { return m_active; }
    float getSamplePhase() const { return m_samplePhase; }

    void triggerModsWithSource(uint8_t src);

private:
    const Instrument* m_instrument = nullptr;
    bool m_active = false;
    bool m_finished = false;

    float m_frequency = 0.0f;
    float m_currentVolume = 0.0f;
    float m_samplePhase = 0.0f;

    float m_gate = 0.0f;
    float m_gateTarget = 0.0f;
    float m_gateStep = 0.0f;

    float m_degradeHeld = 0.0f;
    float m_degradePhase = 0.0f;

    bool m_velocityTakeover = false;

    SamplerEngine m_sampler;
    daisysp::Oscillator m_osc;
    daisysp::Svf m_filter;

    AhdEnv  m_ahdEnv[4];
    AdsrEnv m_adsrEnv[4];
    DrumEnv m_drumEnv[4];
    Lfo     m_lfo[4];

    static constexpr float kGateTime = 0.003f;
};

} // namespace engine
} // namespace m8
