#pragma once

namespace m8 {
namespace engine {
namespace instrument {

// Maps the UI fields from the Sampler Instrument to the 64 generic 
// floating-point parameter slots in the SynthVoice engine.
enum class SamplerParam {
    TRANSP = 0,
    TBL_TIC = 1,
    EQ = 2,
    SLICE = 3,
    AMP = 4,
    PLAY = 5,
    LIM = 6,
    START = 7,
    PAN = 8,
    LOOP_ST = 9,
    DRY = 10,
    LENGTH = 11,
    CHO = 12,
    DETUNE = 13,
    DEL = 14,
    DEGRADE = 15,
    REV = 16,
    FILTER = 17,
    CUTOFF = 18,
    RES = 19,
    
    // Track total number of mapped params
    COUNT
};

} // namespace instrument
} // namespace engine
} // namespace m8
