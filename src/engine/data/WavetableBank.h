#pragma once
#include <cstdint>

namespace m8::engine {

// The 61 built-in wave tables, digitised from the manual's Wave Table Index
// (WAVSYNTH_PHASE3_SPEC.md). Bank index i is WavSynth shape 0x09 + i.
inline constexpr int kWavetableCount  = 61;
inline constexpr int kWavetableFrames = 64;
inline constexpr int kWavetableLength = 200;   // the manual's plot resolution

extern const char* const kWavetableNames[kWavetableCount];
extern const int8_t kWavetableData[kWavetableCount]
                                  [kWavetableFrames][kWavetableLength];

} // namespace m8::engine
