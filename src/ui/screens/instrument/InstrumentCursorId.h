#pragma once
#include <cstdint>

namespace m8 {
namespace ui {
namespace instrument {

// Shared between the Sampler and Macrosyn layouts -- most fields are common
// to both instrument types, so one enum covers the union of both variants'
// field sets rather than having two incompatible cursor types per screen.
enum class CursorId : uint8_t {
    NONE = 0,
    TYPE, CMD_LOAD, CMD_SAVE, NAME, TRANSP, TBL_TIC, EQ,
    FILTER, CUTOFF, RES, AMP, LIM, PAN, DRY, CHO, DEL, REV, DEGRADE,
    // Sampler-only
    SAMPLE_LOAD, SAMPLE_REC, SLICE, PLAY, START, LOOP_ST, LENGTH, DETUNE,
    // Macrosyn-only
    SHAPE, TIMBRE, COLOR, REDUX,
};

} // namespace instrument
} // namespace ui
} // namespace m8
