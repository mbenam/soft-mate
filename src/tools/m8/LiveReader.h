#pragma once

// ===========================================================================
// LiveReader — a continuously-updated ScreenGrid, for reading during playback.
//
// Why this exists
// ---------------
// M8Device's read model is pull-based and settle-gated: readInto() drains the
// port until the screen goes quiet (`sinceData >= settleMs`) or it gives up
// (`sinceStart >= maxMs`). That is the right contract for navigation -- a
// press should be observed after the repaint finishes, not during it -- and it
// is what makes `settled` trustworthy.
//
// It also cannot watch a running transport. The M8 redraws the playhead row
// continuously while playing, so the stream never goes quiet, `sinceData`
// never reaches settleMs, and every read runs the full maxMs and comes back
// `timedOut`. No settleMs value fixes that: the screen genuinely is never
// quiet. Watching a field for drift across a 3-second audio capture -- the
// guard hw_measure.py has to approximate by re-reading before and after,
// because it cannot look during (see M8_DRIVER_BUGS.md #34, and the
// 2026-08-20 run where an inverted transport poisoned seventeen captures) --
// is therefore impossible through the pull path, not merely slow.
//
// LiveReader runs the drain on its own thread and keeps the grid current, so a
// caller can ask "what is on screen right now" for the cost of a copy.
//
// What it gives up
// ----------------
// The settle guarantee. A snapshot is the latest decode, mid-repaint or not.
// You cannot have both -- "fast" and "settled" are the same knob -- so the
// caller chooses: take snapshot() and accept whatever is there, or use
// waitQuiet() to recover the pull model's guarantee on top of the live one.
// Reading a snapshot straight after a press and expecting the press to be in
// it is the stale-read bug; waitChange() or waitQuiet() is how you avoid it.
//
// Concurrency contract
// --------------------
// - While started, this owns the port's read side. M8Device's pull reads
//   refuse (ReadStats::liveConflict) instead of interleaving bytes into the
//   one SlipDecoder and desyncing it.
// - press() and the other write-side calls remain safe from another thread:
//   the port is opened non-blocking for reads, so a concurrent WriteFile does
//   not contend with the drain.
// - Nothing else on M8Device is thread-safe. Reach the grid through
//   snapshot(), never through dev.grid(), while this is running.
// ===========================================================================

#include "M8Device.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace m8 {
namespace dev {

// A copy of the screen as of some instant, with enough telemetry for the
// caller to decide whether to believe it.
struct LiveSnapshot {
    ScreenGrid grid;
    // ms since the last byte arrived. The live equivalent of ReadStats::quietMs:
    // >= your settle threshold means the picture has stopped moving; a small
    // value during playback is normal, not a fault.
    int  quietMs = 0;
    // SLIP frames decoded since start(). Monotonic. Compare across snapshots
    // to tell "the screen changed" from "I read the same frame twice" --
    // comparing grids cannot do that, because a cursor move changes colour
    // only and a repaint of identical content changes nothing at all.
    long seq = 0;
    // False once stop() has run or the thread has exited.
    bool running = false;
};

class LiveReader {
public:
    explicit LiveReader(M8Device& dev) : m_dev(dev) {}
    ~LiveReader() { stop(); }

    LiveReader(const LiveReader&)            = delete;
    LiveReader& operator=(const LiveReader&) = delete;

    // Take over the read side and begin draining. Returns false if the device
    // is not open, or already has a live reader.
    bool start();

    // Stop draining, join the thread, and hand the port back to the pull path.
    // Idempotent; the destructor calls it.
    void stop();

    bool running() const { return m_running.load(std::memory_order_acquire); }

    // The screen as of now. Cheap enough to poll in a tight loop.
    LiveSnapshot snapshot() const;

    // Block until the screen has been quiet for quietMs, then return it. This
    // is the pull model's settled read, rebuilt on the live one. If timeoutMs
    // elapses first the snapshot is returned anyway -- check .quietMs against
    // what you asked for, exactly as you would check ReadStats::settled.
    LiveSnapshot waitQuiet(int quietMs, int timeoutMs) const;

    // Block until seq advances past sinceSeq -- i.e. the device drew
    // something new -- or timeoutMs elapses. The press-then-observe primitive:
    // note the seq, press, wait for it to move.
    LiveSnapshot waitChange(long sinceSeq, int timeoutMs) const;

private:
    void loop();
    LiveSnapshot snapLocked() const;   // caller holds m_mx

    M8Device& m_dev;
    std::thread m_thread;
    mutable std::mutex m_mx;
    mutable std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
    bool m_stop = false;
    long m_seq = 0;
    std::chrono::steady_clock::time_point m_lastData{};
};

} // namespace dev
} // namespace m8
