#pragma once

#include <cstdint>

namespace m8::engine {

inline constexpr float kSampleRate = 48000.0f;

inline constexpr uint8_t NOTE_EMPTY  = 0xFF;  // no note
inline constexpr uint8_t VOL_EMPTY   = 0xFF;
inline constexpr uint8_t INST_EMPTY  = 0xFF;
inline constexpr uint8_t CHAIN_EMPTY = 0xFF;
inline constexpr uint8_t PHRASE_EMPTY= 0xFF;

enum class FxCmd : uint8_t { NONE = 0, VOL, PIT, DEL, REV, HOP, KIL, TBL, GRV, TIC };

struct FxSlot {
    FxCmd   cmd = FxCmd::NONE;
    uint8_t val = 0;
};

struct Step {
    uint8_t note  = NOTE_EMPTY;   // MIDI 0..127
    uint8_t vol   = VOL_EMPTY;    // 0..0x7F
    uint8_t instr = INST_EMPTY;   // 0..0x7F
    FxSlot  fx[3];
};

struct ChainStep {
    uint8_t phrase = PHRASE_EMPTY;
    int8_t  tsp    = 0;       // signed transpose
};

struct SongStep {
    uint8_t tracks[8] = { CHAIN_EMPTY, CHAIN_EMPTY, CHAIN_EMPTY, CHAIN_EMPTY,
                          CHAIN_EMPTY, CHAIN_EMPTY, CHAIN_EMPTY, CHAIN_EMPTY };
};

struct TableStep {
    int8_t transp = 0;
    uint8_t vol = VOL_EMPTY;
    FxSlot fx[3] = {};
};

struct Groove {
    uint8_t steps[16] = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6}; // ticks per row, 1..255
    uint8_t length = 16;      // active steps
};

} // namespace m8::engine
