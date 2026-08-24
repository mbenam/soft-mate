#include "LiveReader.h"

namespace m8 {
namespace dev {

using clk = std::chrono::steady_clock;

static int msSince(clk::time_point t) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count());
}

bool LiveReader::start() {
    if (m_running.load(std::memory_order_acquire)) return false;
    if (!m_dev.isOpen()) return false;
    if (m_dev.liveOwned()) return false;   // another reader already has it

    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_stop = false;
        m_seq  = 0;
        // Start the quiet clock at "now" rather than at the epoch: a device
        // sitting on a static screen sends nothing, and a zero-initialised
        // time_point would report a quietMs of decades on the first snapshot.
        m_lastData = clk::now();
    }
    m_dev.setLiveOwned(true);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&LiveReader::loop, this);
    return true;
}

void LiveReader::stop() {
    if (!m_running.load(std::memory_order_acquire) && !m_thread.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false, std::memory_order_release);
    m_dev.setLiveOwned(false);
    m_cv.notify_all();   // wake any waiter blocked on a now-dead reader
}

void LiveReader::loop() {
    for (;;) {
        PumpResult r;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            if (m_stop) return;
            r = m_dev.pumpOnce();
            if (r.bytes  > 0) m_lastData = clk::now();
            if (r.frames > 0) m_seq += r.frames;
        }
        // Notify only on a decoded frame. Bytes alone can be a partial frame,
        // and waking every waiter for half a repaint is how waitChange() would
        // come to mean "something arrived" instead of "the picture moved".
        if (r.frames > 0) m_cv.notify_all();

        // Idle only when the wire is empty. Matching readInto's 2 ms keeps the
        // spin off a core while staying far under the ~10 ms it takes the M8 to
        // paint a frame, so nothing is missed.
        if (r.bytes == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

LiveSnapshot LiveReader::snapLocked() const {
    LiveSnapshot s;
    s.grid    = m_dev.grid();
    s.quietMs = msSince(m_lastData);
    s.seq     = m_seq;
    s.running = m_running.load(std::memory_order_acquire) && !m_stop;
    return s;
}

LiveSnapshot LiveReader::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mx);
    return snapLocked();
}

LiveSnapshot LiveReader::waitQuiet(int quietMs, int timeoutMs) const {
    const auto deadline = clk::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lk(m_mx);
    for (;;) {
        if (m_stop || msSince(m_lastData) >= quietMs) return snapLocked();
        if (clk::now() >= deadline) return snapLocked();
        // Bounded wait, not a predicate wait: quiet is the *absence* of frames,
        // so nothing will ever notify us into it. The wake has to be a timeout.
        // Cap it so we notice quiet promptly without spinning.
        m_cv.wait_for(lk, std::chrono::milliseconds(2));
    }
}

LiveSnapshot LiveReader::waitChange(long sinceSeq, int timeoutMs) const {
    std::unique_lock<std::mutex> lk(m_mx);
    m_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                  [&] { return m_stop || m_seq > sinceSeq; });
    return snapLocked();
}

} // namespace dev
} // namespace m8
