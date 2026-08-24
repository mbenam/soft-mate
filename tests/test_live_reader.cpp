// ===========================================================================
// test_live_reader.cpp — the screen that never goes quiet.
//
// Why this exists
// ---------------
// The claim LiveReader is built on is not "the pull path is slow during
// playback", it is "the pull path cannot read during playback at all". That is
// a statement about readInto's exit conditions, so it can be proved without
// hardware: a fake that keeps drawing is indistinguishable, to the settle
// logic, from a real M8 redrawing its playhead row.
//
// ChatteringM8 does exactly that and nothing else -- it is not FakeM8's
// interactive model, because none of these tests press anything. It draws one
// moving marker on a timer, and can be told to stop, which is the whole
// experiment: settle-gated reads must fail while it draws and succeed once it
// stops, and LiveReader must stay current throughout.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "m8/M8Device.h"
#include "m8/LiveReader.h"

using namespace m8::dev;
using clk = std::chrono::steady_clock;

namespace {

// A device that repaints on a timer, the way the M8 repaints its playhead row
// while the transport runs.
class ChatteringM8 : public ISerial {
public:
    std::atomic<bool> chattering{true};
    int intervalMs = 20;

    bool open(const char*) override { queueRepaint(); return true; }
    bool send(const void*, size_t) override { return true; }
    bool sendByte(uint8_t b) override {
        if (b == 'E' || b == 'R') queueRepaint();
        return true;
    }
    void close() override {}

    size_t recv(uint8_t* buf, size_t cap) override {
        if (chattering.load() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                clk::now() - m_lastDraw).count() >= intervalMs) {
            m_lastDraw = clk::now();
            // Move the marker so the frames are not identical -- a repaint of
            // identical content is still a repaint, but a moving one also
            // proves the grid is being updated and not merely re-fed.
            m_row = (m_row + 1) % 8;
            drawChar('>', 2, 6 + m_row, 200, 200, 200);
        }
        size_t n = 0;
        while (n < cap && !m_out.empty()) { buf[n++] = m_out.front(); m_out.pop_front(); }
        return n;
    }

    // Draw a full screen's worth of content, once, off the timer.
    void queueRepaint() {
        emit({0xFF, 0, 6, 5, 2, 0});                 // sysinfo: fw 6.5.2
        const std::string label = "TEMPO";
        for (size_t i = 0; i < label.size(); ++i)
            drawChar(label[i], static_cast<int>(i), 3, 200, 200, 200);
    }

private:
    void emit(const std::vector<uint8_t>& frame) {
        for (uint8_t b : frame) {                    // SLIP escaping
            if (b == 0xC0)      { m_out.push_back(0xDB); m_out.push_back(0xDC); }
            else if (b == 0xDB) { m_out.push_back(0xDB); m_out.push_back(0xDD); }
            else                  m_out.push_back(b);
        }
        m_out.push_back(0xC0);                       // END
    }

    void drawChar(char ch, int col, int row, uint8_t r, uint8_t g, uint8_t b) {
        const int x = col * 8, y = row * 10;
        emit({0xFD, static_cast<uint8_t>(ch),
              static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
              static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
              r, g, b, 0, 0, 0});
    }

    std::deque<uint8_t> m_out;
    clk::time_point m_lastDraw = clk::now();
    int m_row = 0;
};

// mainRows() keys on pixel y, not row index, so scan rather than index --
// the same idiom test_device_fake.cpp uses.
bool screenHas(const ScreenGrid& g, const std::string& text) {
    for (const auto& entry : g.mainRows())
        if (entry.second.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

TEST_CASE("a settle-gated read cannot complete while the screen keeps drawing",
          "[hwdecode][live]") {
    ChatteringM8 fake;
    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));

    // 20 ms between repaints, so 200 ms of quiet is never available. This is
    // the pull model behaving correctly, not a bug -- and it is exactly why
    // watching a running transport through it is impossible.
    dev.readSettled(0, 200, 600);
    CHECK(dev.lastRead().timedOut);
    CHECK_FALSE(dev.lastRead().settled);
    CHECK(dev.lastRead().framesSeen > 0);      // bytes were arriving the whole time

    // Same device, same chatter, once it stops: the settle branch fires.
    fake.chattering = false;
    dev.readSettled(0, 200, 1500);
    CHECK(dev.lastRead().settled);
    CHECK_FALSE(dev.lastRead().timedOut);
}

TEST_CASE("LiveReader keeps the grid current while the screen never settles",
          "[hwdecode][live]") {
    ChatteringM8 fake;
    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));

    LiveReader live(dev);
    REQUIRE(live.start());

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const LiveSnapshot a = live.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const LiveSnapshot b = live.snapshot();

    CHECK(a.running);
    CHECK(b.seq > a.seq);          // the picture moved, and we saw it move
    CHECK(b.quietMs < 200);        // never quiet -- the pull path's failure case
    // The bytes went through SLIP and the 0xFD decoder, not around them.
    CHECK(screenHas(b.grid, "TEMPO"));

    live.stop();
    CHECK_FALSE(live.running());
}

TEST_CASE("waitQuiet recovers the settled read on top of the live one",
          "[hwdecode][live]") {
    ChatteringM8 fake;
    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));

    LiveReader live(dev);
    REQUIRE(live.start());

    // While drawing: returns on the timeout, and says so via quietMs -- the
    // caller checks it exactly as it would check ReadStats::settled.
    const LiveSnapshot busy = live.waitQuiet(150, 400);
    CHECK(busy.quietMs < 150);

    fake.chattering = false;
    const LiveSnapshot calm = live.waitQuiet(150, 2000);
    CHECK(calm.quietMs >= 150);
}

TEST_CASE("waitChange observes a repaint and returns as soon as it lands",
          "[hwdecode][live]") {
    ChatteringM8 fake;
    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));
    fake.chattering = false;                  // quiet, so nothing moves on its own

    LiveReader live(dev);
    REQUIRE(live.start());
    const LiveSnapshot before = live.waitQuiet(60, 1000);

    // Nothing is drawing: waitChange must time out rather than report progress.
    const LiveSnapshot idle = live.waitChange(before.seq, 120);
    CHECK(idle.seq == before.seq);

    const auto t0 = clk::now();
    fake.queueRepaint();
    const LiveSnapshot after = live.waitChange(before.seq, 2000);
    const auto waitedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();

    CHECK(after.seq > before.seq);
    // The point of the live model: observed in milliseconds, not after a
    // settle window. Generous bound -- this asserts "not settle-gated", not a
    // latency figure.
    CHECK(waitedMs < 150);
}

TEST_CASE("a pull read refuses rather than racing the live reader for the port",
          "[hwdecode][live]") {
    ChatteringM8 fake;
    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));

    LiveReader live(dev);
    REQUIRE(live.start());

    const auto t0 = clk::now();
    dev.readSettled(0, 200, 1500);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();

    CHECK(dev.lastRead().liveConflict);
    CHECK_FALSE(dev.lastRead().settled);
    CHECK(dev.lastRead().framesSeen == 0);   // it never touched the wire
    CHECK(elapsed < 100);                    // and did not sit out its timeout

    // A second reader cannot take a port that is already owned.
    LiveReader second(dev);
    CHECK_FALSE(second.start());

    // Ownership returns when the reader stops.
    live.stop();
    fake.chattering = false;
    dev.readSettled(0, 200, 1500);
    CHECK_FALSE(dev.lastRead().liveConflict);
    CHECK(dev.lastRead().settled);
}
