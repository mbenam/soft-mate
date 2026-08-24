#include "FlightRecorder.h"

#include <fstream>

namespace m8 {
namespace dev {

FlightRecorder& getFlightRecorder() {
    static FlightRecorder r;
    return r;
}

bool FlightRecorder::dump(const std::string& path, const std::string& reason) const {
    std::ofstream o(path);
    if (!o) return false;

    o << "{\n";
    o << "  \"reason\": \"" << reason << "\",\n";
    o << "  \"events\": [\n";

    // Oldest first. The ring may not have wrapped yet, in which case the oldest
    // entry is index 0 rather than m_head.
    const size_t start = (m_count < kCapacity) ? 0 : m_head;
    for (size_t i = 0; i < m_count; ++i) {
        const FlightEvent& e = m_events[(start + i) % kCapacity];
        if (i) o << ",\n";
        o << "    {\"t_ms\":" << e.tMs;
        switch (e.kind) {
        case FlightEvent::Kind::Press:
            o << ",\"kind\":\"press\",\"mask\":" << int(e.mask)
              << ",\"hold_ms\":" << e.holdMs;
            break;
        case FlightEvent::Kind::Read:
            o << ",\"kind\":\"read\",\"cursor_row\":" << e.cursorRow
              << ",\"cursor_col\":" << e.cursorCol
              << ",\"frames\":" << e.framesSeen
              << ",\"settled\":" << (e.settled ? "true" : "false");
            break;
        case FlightEvent::Kind::Note:
            o << ",\"kind\":\"note\",\"text\":\"" << e.note << "\"";
            break;
        }
        o << "}";
    }
    o << "\n  ],\n";

    // Raw bytes as hex, oldest first. This is the half that tells a bad mask
    // from a device that did something else from a decode fault -- see the
    // header, and #32 for why those three have to be separable.
    o << "  \"raw_hex\": \"";
    const size_t rstart = (m_rawCount < kRawBytes) ? 0 : m_rawHead;
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < m_rawCount; ++i) {
        const uint8_t b = m_raw[(rstart + i) % kRawBytes];
        o << kHex[(b >> 4) & 0xF] << kHex[b & 0xF];
    }
    o << "\",\n";
    o << "  \"event_count\": " << m_count << ",\n";
    o << "  \"raw_bytes\": " << m_rawCount << "\n";
    o << "}\n";
    return true;
}

} // namespace dev
} // namespace m8
