#pragma once

// ===========================================================================
// ParamRange.h — legal value ranges for .m8s fields, as observed on real
// hardware. Source: docs/tools/hw_findings.md §R2, firmware 6.5.2.
//
// THE ONLY PLACE a range is encoded. Do not inline a bound anywhere else.
//
// `confirmed` distinguishes a ceiling observed by turning the parameter to
// its maximum on the device (trustworthy) from a value merely seen in a
// golden (may not be the true limit).
// ===========================================================================

#include <cstdint>
#include <string>

namespace m8 {

struct ParamRange {
    const char* name;
    uint8_t     min;
    uint8_t     max;
    bool        confirmed;
    const char* note;
};

// Instrument volume: hardware UI ceiling is 0x7F across all 5 instrument types.
// Writing > 0x7F (e.g. 0xE0) causes the device to treat volume as 0x00 (silence).
inline constexpr ParamRange kInstrumentVolume{
    "instrument.volume", 0x00, 0x7F, true,
    "Confirmed UI max ceiling 0x7F (127) across all 5 instrument types; >0x7F reads as 0x00"
};

inline constexpr ParamRange kMixerDry{
    "mixer.dry", 0x00, 0xE0, false,
    "Observed nominal dry mix 0xC0"
};

inline constexpr ParamRange kMixerPan{
    "mixer.pan", 0x00, 0xFF, false,
    "Observed neutral center pan 0x80"
};

inline constexpr ParamRange kMod0Amount{
    "mod0.amount", 0x00, 0xFF, false,
    "Observed full positive mod amount 0xFF"
};

inline constexpr ParamRange kMod0Attack{
    "mod0.attack", 0x00, 0xFF, false,
    "Observed fast attack 0x01"
};

inline constexpr ParamRange kMod0Hold{
    "mod0.hold", 0x00, 0xFF, false,
    "Observed envelope hold ticks"
};

inline constexpr ParamRange kMod0Decay{
    "mod0.decay", 0x00, 0xFF, false,
    "Observed decay ticks 0x80"
};

inline constexpr ParamRange kMasterVolume{
    "mixer.master_volume", 0x00, 0xE0, false,
    "Observed nominal master volume 0xE0"
};

inline constexpr ParamRange kTrackVolume{
    "mixer.track_volume", 0x00, 0xE0, false,
    "Observed nominal track volume 0xE0"
};

// Returns false and sets `err` if `v` is outside the range.
inline bool checkRange(const ParamRange& r, int v, std::string& err) {
    if (v < r.min || v > r.max) {
        err = std::string(r.name) + " value " + std::to_string(v)
            + " outside [" + std::to_string(r.min) + ","
            + std::to_string(r.max) + "]";
        return false;
    }
    return true;
}

} // namespace m8
