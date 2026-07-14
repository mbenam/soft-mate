#pragma once
#include "SamplePool.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace m8::engine {

inline constexpr int kSamplerRootMidi = 60;
inline constexpr float kDetuneSemisPerStep = 1.0f / 16.0f;

enum class SamplePlayMode : uint8_t {
    FWD = 0, REV, FWDLOOP, REVLOOP, FWD_PP, REV_PP,
    OSC, OSC_REV, OSC_PP,
    REPITCH, REP_REV, REP_PP,
    REP_BPM, BPM_REV, BPM_PP
};

inline bool isLoopMode(SamplePlayMode m) {
    auto v = static_cast<uint8_t>(m);
    return v >= 2 && v <= 8;
}
inline bool isPingPongMode(SamplePlayMode m) {
    auto v = static_cast<uint8_t>(m);
    return v == 4 || v == 5 || v == 8 || v == 11 || v == 14;
}
inline bool isOscMode(SamplePlayMode m) {
    auto v = static_cast<uint8_t>(m);
    return v >= 6 && v <= 8;
}
inline bool isReverseMode(SamplePlayMode m) {
    auto v = static_cast<uint8_t>(m);
    return v == 1 || v == 3 || v == 5 || v == 7 || v == 10 || v == 13;
}

inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float lerp(float a, float b, float t) { return a + t * (b - a); }

struct SamplerState;

class SamplerEngine {
public:
    void noteOn(const SamplerState& s, const SampleData* d);
    void setSample(const SampleData* d) { m_data = d; }
    void setLoop(int32_t loopStart, int32_t loopEnd);
    void render(float ratio, float out[2]);
    void computeRegion(const SamplerState& s);
    bool finished() const { return m_finished; }
    double phase() const { return m_phase; }
    const SampleData* data() const { return m_data; }

private:
    void readFrame(double phase, float out[2]) const;

    const SampleData* m_data = nullptr;
    double m_phase = 0.0;
    int32_t m_startFrame = 0;
    int32_t m_loopStart = 0;
    int32_t m_loopEnd = 0;
    SamplePlayMode m_mode = SamplePlayMode::FWD;
    bool m_reverse = false;
    bool m_finished = false;
};

} // namespace m8::engine
