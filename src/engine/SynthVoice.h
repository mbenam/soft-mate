#pragma once

#include "SamplePool.h"
#include "Envelopes.h"
#include "Lfo.h"
#include "Modulation.h"
#include "SamplerEngine.h"
#include "ZdfFilter.h"
#include "daisysp.h"
#include "braids/macro_oscillator.h"

namespace m8 {
namespace engine {

struct Instrument;
struct WavSynthState;

class SynthVoice {
public:
    SynthVoice();
    ~SynthVoice() = default;

    void setInstrument(const Instrument* inst) { m_instrument = inst; }
    void noteOn(float frequency, float volume, const Instrument* inst = nullptr);
    void noteOff();
    void resetOscillator() {
        m_braidsOsc.Init();
        m_braidsReadIdx = 24;
        for (int i = 0; i < 24; ++i) m_braidsBuffer[i] = 0;
    }
    void setSample(const SampleData* s) { m_sampler.setSample(s); }
    void setVolume(float v);
    void setLoop(int32_t loopStart, int32_t loopEnd) { m_sampler.setLoop(loopStart, loopEnd); }

    // Table-applied modulation (set by Engine each tick, read by renderSample)
    float m_tableTranspose = 0.0f;   // semitones, added to pitch
    float m_tableVolume = 1.0f;      // multiplier (1.0 = no change)

    float getTableTranspose() const { return m_tableTranspose; }
    float getTableVolume() const { return m_tableVolume; }
    void setTableModulation(float transpose, float volume) {
        m_tableTranspose = transpose;
        m_tableVolume = volume;
    }

    float renderSample(const EnvContext& ctx);
    bool isActive() const { return m_active; }
    float getSamplePhase() const { return m_samplePhase; }

    void triggerModsWithSource(uint8_t src);

private:
    // Sampler amp/filter stage helpers. applyFilter dispatches FILTER types
    // (1-4 via the non-ZDF SVF, 6/7 via the ZDF SVF; 5 passes through). the
    // limiter/waveshaper implements the LIM modes. Both are used by the sampler
    // render path; the ordering relative to the filter depends on POST mode.
    float applyFilter(float in, int type, float cutoffHz, float res);
    static float applyLimiter(float x, int mode);

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
    braids::MacroOscillator m_braidsOsc;
    int16_t m_braidsBuffer[24];
    uint8_t m_braidsReadIdx = 24;
    daisysp::Svf m_filter;
    ZdfSvf m_zdf;   // FILTER 06/07 (ZDF LP/HP); m_filter serves the non-ZDF types

    AhdEnv  m_ahdEnv[4];
    AdsrEnv m_adsrEnv[4];
    DrumEnv m_drumEnv[4];
    Lfo     m_lfo[4];

    // HyperSynth supersaw state: up to 7 chord notes, 5 detuned voices each
    static constexpr int kHyperMaxNotes = 7;
    static constexpr int kHyperVoices = 5;
    struct HyperVoice {
        uint32_t phase = 0;
        uint32_t inc = 0;
    };
    HyperVoice m_hyperL[kHyperMaxNotes][kHyperVoices] = {};
    HyperVoice m_hyperR[kHyperMaxNotes][kHyperVoices] = {};
    HyperVoice m_hyperSub = {};

    static constexpr int kFMWavetableSize = 2048;
    static constexpr int kFMNumShapes = 12;
    float m_fmWavetable[kFMNumShapes][kFMWavetableSize] = {};
    bool m_fmWavetableReady = false;
    float m_fmPhase[4] = {};
    float m_fmPrevOut[4] = {};
    void initFMWavetables();
    static float readFMWavetable(const float* table, float phase);

    // WavSynth state
    static constexpr int kWavBufSize = 2048;
    float m_wavBuf[kWavBufSize] = {};
    int m_wavBufLen = 0;
    uint32_t m_wavPhase = 0;
    void generateWavShape(const WavSynthState& ws, float noteFreq);
    static float readWavBuf(const float* buf, int len, float phase);

    static constexpr float kGateTime = 0.003f;
};

} // namespace engine
} // namespace m8
