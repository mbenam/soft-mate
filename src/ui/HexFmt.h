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
        case m8::engine::FxCmd::TBL: return "TBL";
        case m8::engine::FxCmd::GRV: return "GRV";
        case m8::engine::FxCmd::TIC: return "TIC";
        case m8::engine::FxCmd::SCA: return "SCA";
        case m8::engine::FxCmd::SCG: return "SCG";
        case m8::engine::FxCmd::ARP: return "ARP";
        case m8::engine::FxCmd::ARC: return "ARC";
        case m8::engine::FxCmd::CHA: return "CHA";
        case m8::engine::FxCmd::GGR: return "GGR";
        case m8::engine::FxCmd::INS: return "INS";
        case m8::engine::FxCmd::RND: return "RND";
        case m8::engine::FxCmd::RNL: return "RNL";
        case m8::engine::FxCmd::RET: return "RET";
        case m8::engine::FxCmd::REP: return "REP";
        case m8::engine::FxCmd::RTO: return "RTO";
        case m8::engine::FxCmd::RMX: return "RMX";
        case m8::engine::FxCmd::NTH: return "NTH";
        case m8::engine::FxCmd::PSL: return "PSL";
        case m8::engine::FxCmd::PBN: return "PBN";
        case m8::engine::FxCmd::PVB: return "PVB";
        case m8::engine::FxCmd::PVX: return "PVX";
        case m8::engine::FxCmd::SNG: return "SNG";
        case m8::engine::FxCmd::SED: return "SED";
        case m8::engine::FxCmd::THO: return "THO";
        case m8::engine::FxCmd::TBX: return "TBX";
        case m8::engine::FxCmd::TPO: return "TPO";
        case m8::engine::FxCmd::TSP: return "TSP";
        case m8::engine::FxCmd::NXT: return "NXT";
        case m8::engine::FxCmd::OFF: return "OFF";
        case m8::engine::FxCmd::MTT: return "MTT";
        case m8::engine::FxCmd::FIN: return "FIN";
        case m8::engine::FxCmd::EA1: return "EA1";
        case m8::engine::FxCmd::EA2: return "EA2";
        case m8::engine::FxCmd::AT1: return "AT1";
        case m8::engine::FxCmd::AT2: return "AT2";
        case m8::engine::FxCmd::HO1: return "HO1";
        case m8::engine::FxCmd::HO2: return "HO2";
        case m8::engine::FxCmd::DE1: return "DE1";
        case m8::engine::FxCmd::DE2: return "DE2";
        case m8::engine::FxCmd::ET1: return "ET1";
        case m8::engine::FxCmd::ET2: return "ET2";
        case m8::engine::FxCmd::LA1: return "LA1";
        case m8::engine::FxCmd::LA2: return "LA2";
        case m8::engine::FxCmd::LF1: return "LF1";
        case m8::engine::FxCmd::LF2: return "LF2";
        case m8::engine::FxCmd::LT1: return "LT1";
        case m8::engine::FxCmd::LT2: return "LT2";
        case m8::engine::FxCmd::EQM: return "EQM";
        case m8::engine::FxCmd::EQI: return "EQI";
        case m8::engine::FxCmd::VMV: return "VMV";
        case m8::engine::FxCmd::VMX: return "VMX";
        case m8::engine::FxCmd::VDE: return "VDE";
        case m8::engine::FxCmd::VRE: return "VRE";
        case m8::engine::FxCmd::VT1: return "VT1";
        case m8::engine::FxCmd::VT2: return "VT2";
        case m8::engine::FxCmd::VT3: return "VT3";
        case m8::engine::FxCmd::VT4: return "VT4";
        case m8::engine::FxCmd::VT5: return "VT5";
        case m8::engine::FxCmd::VT6: return "VT6";
        case m8::engine::FxCmd::VT7: return "VT7";
        case m8::engine::FxCmd::VT8: return "VT8";
        case m8::engine::FxCmd::DJC: return "DJC";
        case m8::engine::FxCmd::DJR: return "DJR";
        case m8::engine::FxCmd::DJT: return "DJT";
        case m8::engine::FxCmd::XMT: return "XMT";
        case m8::engine::FxCmd::XMM: return "XMM";
        case m8::engine::FxCmd::XMF: return "XMF";
        case m8::engine::FxCmd::XMW: return "XMW";
        case m8::engine::FxCmd::XMR: return "XMR";
        case m8::engine::FxCmd::XDT: return "XDT";
        case m8::engine::FxCmd::XDF: return "XDF";
        case m8::engine::FxCmd::XDW: return "XDW";
        case m8::engine::FxCmd::XDR: return "XDR";
        case m8::engine::FxCmd::XRS: return "XRS";
        case m8::engine::FxCmd::XRD: return "XRD";
        case m8::engine::FxCmd::XRM: return "XRM";
        case m8::engine::FxCmd::XRF: return "XRF";
        case m8::engine::FxCmd::XRW: return "XRW";
        case m8::engine::FxCmd::XRZ: return "XRZ";
        case m8::engine::FxCmd::IVO: return "IVO";
        case m8::engine::FxCmd::IMX: return "IMX";
        case m8::engine::FxCmd::IDE: return "IDE";
        case m8::engine::FxCmd::IRV: return "IRV";
        case m8::engine::FxCmd::IV2: return "IV2";
        case m8::engine::FxCmd::IM2: return "IM2";
        case m8::engine::FxCmd::ID2: return "ID2";
        case m8::engine::FxCmd::IR2: return "IR2";
        case m8::engine::FxCmd::USB: return "USB";
        case m8::engine::FxCmd::UNKNOWN: return "???"; // unmodeled command, preserved on save
        default: return "---";
    }
}

inline std::string FormatFx(const m8::engine::FxSlot& fx) {
    if (fx.cmd == m8::engine::FxCmd::NONE) return "---00";
    return FxName(fx.cmd) + HexU8(fx.val, "00");
}

} // namespace m8::ui
