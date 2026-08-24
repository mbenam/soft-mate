#pragma once

// ===========================================================================
// FlightRecorder.h — the last few seconds before something went wrong.
//
// Why this exists
// ---------------
// M8_DRIVER_BUGS.md #34 -- `set AMP FF` left AMP at FD and moved LIM from 04 to
// 08 -- happened ONCE, in a long session, and has not been reproduced since. A
// deliberate sweep on 2026-08-24 sent 288 coarse presses across nine hold/gap
// combinations without a single slip, which rules out the suspected mechanism
// as stated but does not find the real one.
//
// For a fault that rare, watching is the wrong strategy: by the time a human or
// a guard notices, the evidence is gone. Record continuously into a ring, and
// dump it when the guard trips.
//
// What it records, and why each part earns its place:
//
//   presses   what we asked for, with timestamps. Without this you cannot tell
//             a mistimed press from a mis-decoded screen.
//   raw bytes what actually came back, BEFORE SLIP decoding. This is the one
//             that separates the three candidate causes -- we sent a bad mask,
//             the device did something else, or our decode is wrong -- which
//             are indistinguishable once decoded. #32 is the precedent: a
//             desynced stream and a device fault look identical downstream.
//   cursor    where the driver believed it was, per read.
//
// Deliberately fixed-capacity and allocation-free once started: this runs
// inside the driver's read loop, and a recorder that allocates would change the
// timing of the very thing it is trying to catch.
// ===========================================================================

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace m8 {
namespace dev {

struct FlightEvent {
    enum class Kind : uint8_t { Press, Read, Note };
    Kind kind = Kind::Note;
    int  tMs = 0;          // ms since the recorder started
    uint8_t mask = 0;      // Press: the key mask
    int  holdMs = 0;       // Press: how long it was held
    int  cursorRow = -1;   // Read: where the driver thought it was
    int  cursorCol = -1;
    int  framesSeen = 0;   // Read: SLIP frames decoded
    bool settled = false;  // Read: did it exit via the settle branch
    char note[48] = {0};   // Note: free text
};

class FlightRecorder {
public:
    // Capacity is generous enough to cover several seconds of a walk that
    // re-reads every iteration (~3 events per press) without wrapping past the
    // interesting part.
    static constexpr size_t kCapacity = 4096;
    static constexpr size_t kRawBytes = 64 * 1024;

    void start() {
        m_start = std::chrono::steady_clock::now();
        m_events.assign(kCapacity, FlightEvent{});
        m_raw.assign(kRawBytes, 0);
        m_head = 0; m_count = 0;
        m_rawHead = 0; m_rawCount = 0;
        m_on = true;
    }
    void stop() { m_on = false; }
    bool running() const { return m_on; }

    int nowMs() const {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count());
    }

    void recordPress(uint8_t mask, int holdMs) {
        if (!m_on) return;
        FlightEvent e;
        e.kind = FlightEvent::Kind::Press;
        e.tMs = nowMs(); e.mask = mask; e.holdMs = holdMs;
        push(e);
    }

    void recordRead(int cursorRow, int cursorCol, int framesSeen, bool settled) {
        if (!m_on) return;
        FlightEvent e;
        e.kind = FlightEvent::Kind::Read;
        e.tMs = nowMs();
        e.cursorRow = cursorRow; e.cursorCol = cursorCol;
        e.framesSeen = framesSeen; e.settled = settled;
        push(e);
    }

    void recordNote(const char* text) {
        if (!m_on) return;
        FlightEvent e;
        e.kind = FlightEvent::Kind::Note;
        e.tMs = nowMs();
        std::snprintf(e.note, sizeof(e.note), "%s", text ? text : "");
        push(e);
    }

    void recordRaw(const uint8_t* data, size_t n) {
        if (!m_on || !data) return;
        for (size_t i = 0; i < n; ++i) {
            m_raw[m_rawHead] = data[i];
            m_rawHead = (m_rawHead + 1) % kRawBytes;
            if (m_rawCount < kRawBytes) ++m_rawCount;
        }
    }

    // Write the ring out, oldest first. Returns false if the file cannot be
    // opened -- a recorder that silently drops its only artifact is worse than
    // no recorder.
    bool dump(const std::string& path, const std::string& reason) const;

    size_t eventCount() const { return m_count; }
    size_t rawCount() const { return m_rawCount; }

private:
    void push(const FlightEvent& e) {
        m_events[m_head] = e;
        m_head = (m_head + 1) % kCapacity;
        if (m_count < kCapacity) ++m_count;
    }

    std::vector<FlightEvent> m_events;
    std::vector<uint8_t>     m_raw;
    size_t m_head = 0, m_count = 0;
    size_t m_rawHead = 0, m_rawCount = 0;
    bool   m_on = false;
    std::chrono::steady_clock::time_point m_start{};
};

// One recorder per process. The driver is single-device by construction, and a
// per-instance one would have to be threaded through every call site that might
// want to record -- which is most of Primitives.cpp.
FlightRecorder& getFlightRecorder();

} // namespace dev
} // namespace m8
