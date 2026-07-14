#pragma once
#include <cstdint>

namespace m8::engine {

enum class ModDest : uint8_t {
    OFF = 0, VOLUME, PITCH, LOOP_ST, LENGTH, DEGRADE,
    CUTOFF, RES, AMP, PAN, MOD_AMT, MOD_RATE, MOD_BOTH, MOD_BINV
};

struct ModTargets {
    float volume = 0.0f;
    float pitch = 0.0f;
    float loopSt = 0.0f;
    float length = 0.0f;
    float degrade = 0.0f;
    float cutoff = 0.0f;
    float res = 0.0f;
    float amp = 0.0f;
    float pan = 0.0f;
};

inline float bipolarAmt(uint8_t amt) {
    return (int(amt) - 128) / 128.0f;
}

} // namespace m8::engine
