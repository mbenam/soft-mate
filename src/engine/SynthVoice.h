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
    void noteOn(float frequency, float volume, const Instrument* inst = nullptr, int noteMidi = 60);
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

    // Stereo entry point. Engine calls this; renderSample stays as-is.
    //
    // Only the SAMPLER path is genuinely stereo: everything else fills both
    // channels from renderSample, so the synth engines are bit-identical to
    // before. That split is deliberate rather than lazy -- MEASURED on hardware
    // 2026-08-14 (hw_findings.md §UI-12), the M8 reproduces a stereo sample's
    // image intact, whereas HyperSynth's WIDTH at maximum is only about -31 dB
    // of side content (§UI-11). Samples are what justify a stereo voice path;
    // the synths can follow later, and each will need its own audio A/B.
    void renderFrame(const EnvContext& ctx, float out[2]);

    bool isActive() const { return m_active; }
    float getSamplePhase() const { return m_samplePhase; }

    void triggerModsWithSource(uint8_t src);

private:
    // Per-voice output-stage helpers. applyFilter dispatches FILTER types
    // (1-4 via the non-ZDF SVF, 6/7 via the ZDF SVF; 5 passes through). the
    // limiter/waveshaper implements the LIM modes. The ordering of the two
    // relative to the AMP gain depends on POST mode -- see applyAmpLimFilter.
    float applyFilter(float in, int type, float cutoffHz, float res);
    static float applyLimiter(float x, int mode);

    // DEGRADE: sample-and-hold decimator. Shared by the sampler and macrosyn
    // render paths, which carried byte-identical copies of it
    // (ARCHITECTURE.md §5.2 #8). Stateful -- owns m_degradePhase/m_degradeHeld.
    float applyDegrade(float in, int degradeByte, float degradeMod);

    // AMP drive -> LIM waveshaper -> FILTER, including the LIM 04-08 (POST)
    // ordering flip: POST modes apply the AMP gain and its clipping AFTER the
    // filter (manual p.55), 00-03 shape first and filter after. Byte values are
    // the raw 0-255 instrument fields; `mt` supplies the modulation offsets.
    // Shared by the sampler, hyper, FM and wav paths, which carried four
    // identical copies (ARCHITECTURE.md §5.2 #8). The macrosyn path
    // deliberately does NOT use this -- see the comment at its call site.
    float applyAmpLimFilter(float in, int ampByte, int limMode, int filterType,
                            int cutoffByte, int resByte, const ModTargets& mt);

    // Stereo forms of the two stateful stages, for the sampler path.
    //
    // These exist as separate functions rather than replacing the mono ones so
    // the hyper/FM/wav paths keep calling the mono versions and stay
    // bit-identical. The right channel needs its OWN filter state (m_filterR,
    // m_zdfR) -- sharing one filter across two channels would cross-feed them
    // and is not the same operation twice.
    //
    // DEGRADE is different: it is a sample-and-hold whose clock is shared, so
    // m_degradePhase stays single and only the held VALUE is per-channel.
    // Two independent phases would decimate L and R at different instants and
    // manufacture stereo out of a mono source.
    void applyDegradeStereo(float& l, float& r, int degradeByte, float degradeMod);
    void applyAmpLimFilterStereo(float& l, float& r, int ampByte, int limMode,
                                 int filterType, int cutoffByte, int resByte,
                                 const ModTargets& mt);
    float applyFilterR(float in, int type, float cutoffHz, float res);

    const Instrument* m_instrument = nullptr;
    bool m_active = false;
    // NOTE: SynthVoice had its own `m_finished` here, set on note-on and when
    // the sampler ran out, and never read by anything (ARCHITECTURE.md §5.2
    // #8). Removed rather than left looking like state. Voice liveness is
    // `m_active`/`isActive()`; sample exhaustion is `SamplerEngine::finished()`,
    // which IS read (SamplerEngine.cpp) -- don't confuse the two.

    float m_frequency = 0.0f;
    float m_currentVolume = 0.0f;
    float m_samplePhase = 0.0f;

    float m_gate = 0.0f;
    float m_gateTarget = 0.0f;
    float m_gateStep = 0.0f;

    float m_degradeHeld = 0.0f;
    float m_degradeHeldR = 0.0f;   // held value for the right channel; phase is shared
    float m_degradePhase = 0.0f;

    // renderFrame <-> sampler-path handshake. renderFrame raises m_frameStereo,
    // and the sampler branch -- the only stereo-capable one -- runs its output
    // stage per channel, writes m_frameOut and raises m_frameFilled. Anything
    // else leaves m_frameFilled low and renderFrame duplicates the mono return.
    //
    // Done with members rather than an out-parameter because the stereo decision
    // has to be made INSIDE the branch: the output stage is stateful, so
    // summing first and splitting afterwards would run the filter on the wrong
    // signal, and running it twice would advance the filter state twice.
    bool  m_frameStereo = false;
    bool  m_frameFilled = false;
    float m_frameOut[2] = {0.0f, 0.0f};

    bool m_velocityTakeover = false;

    SamplerEngine m_sampler;
    daisysp::Oscillator m_osc;
    braids::MacroOscillator m_braidsOsc;
    int16_t m_braidsBuffer[24];
    uint8_t m_braidsReadIdx = 24;
    daisysp::Svf m_filter;
    ZdfSvf m_zdf;   // FILTER 06/07 (ZDF LP/HP); m_filter serves the non-ZDF types
    // Right-channel duplicates, used only by the stereo sampler path. Left keeps
    // m_filter/m_zdf, so a mono render touches exactly the state it always did.
    daisysp::Svf m_filterR;
    ZdfSvf m_zdfR;

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

    // WavSynth state.
    // The table is at most 256 samples because SIZE is a byte and the manual
    // defines it as the sample count of the wave table. The +1 slot is a guard
    // holding a copy of [0], so linear interpolation at the loop point needs no
    // wrap branch and cannot read stale data (Phase 1 defect D2).
    static constexpr int kWavTableMax = 256;
    float    m_wavTable[kWavTableMax + 1] = {};
    int      m_wavTableLen = 0;
    uint32_t m_wavPhase = 0;
    uint32_t m_wavNoiseLfsr = 1u;   // shape 08 (NOISE); reset on note-on
    daisysp::Svf m_wavShaper;       // FILTER 08-0B only -- must NOT be m_filter,
                                    // which is the output-stage filter

    // Cache key: the parameters the current m_wavTable was generated from.
    // -1 means "nothing generated yet". WARP and SCAN are in the key because
    // they are baked into the table (§3.1); cutoff/res only matter when
    // filter_type is 08-0B but are always compared, which is harmless.
    int m_wavKeyShape  = -1;
    int m_wavKeySize   = -1;
    int m_wavKeyMult   = -1;
    int m_wavKeyWarp   = -1;
    int m_wavKeyScan   = -1;
    int m_wavKeyFilter = -1;
    int m_wavKeyCutoff = -1;
    int m_wavKeyRes    = -1;

    bool  wavTableStale(const WavSynthState& ws) const;
    void  regenerateWavTable(const WavSynthState& ws);
    float readWavTable(uint32_t phase) const;
public:
    static float wavBaseShape(int shape, float u);
    static float wavWarpPhase(float u, float warp01);
    static float wavMirrorPhase(float u, float mirror);
    static float wavLfsrNext(uint32_t& state);
private:

    static constexpr float kGateTime = 0.003f;
};

} // namespace engine
} // namespace m8
