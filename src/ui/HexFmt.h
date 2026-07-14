#pragma once

#include "../engine/SeqTypes.h"
#include <string>
#include <iomanip>
#include <sstream>

namespace m8::ui {

inline std::string HexU8(uint8_t val, const std::string& emptyStr = "--") {
    if (val == 0xFF) return emptyStr;
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<int>(val);
    return ss.str();
}

inline std::string HexS8(int8_t val, const std::string& emptyStr = "00") {
    // Transpose is often shown as signed hex or just hex.
    // In original code it was handled as hex string directly.
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (static_cast<int>(val) & 0xFF);
    return ss.str();
}

inline std::string NoteName(uint8_t midi) {
    if (midi == m8::engine::NOTE_EMPTY) return "---";
    int octave = (midi / 12) - 1;
    int note = midi % 12;
    std::string notes[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    std::string res = notes[note];
    if (octave >= 0 && octave <= 9) res += std::to_string(octave);
    else res += "A";
    return res;
}

inline std::string FxName(m8::engine::FxCmd cmd) {
    switch (cmd) {
        case m8::engine::FxCmd::VOL: return "VOL";
        case m8::engine::FxCmd::PIT: return "PIT";
        case m8::engine::FxCmd::DEL: return "DEL";
        case m8::engine::FxCmd::REV: return "REV";
        case m8::engine::FxCmd::HOP: return "HOP";
        case m8::engine::FxCmd::KIL: return "KIL";
        default: return "---";
    }
}

inline std::string FormatFx(const m8::engine::FxSlot& fx) {
    if (fx.cmd == m8::engine::FxCmd::NONE) return "---00";
    return FxName(fx.cmd) + HexU8(fx.val, "00");
}

} // namespace m8::ui
