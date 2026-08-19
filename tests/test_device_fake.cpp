// ===========================================================================
// test_device_fake.cpp — a scripted M8 on the other end of the wire.
//
// Why this exists
// ---------------
// Until now nothing in src/tools/m8/Primitives.cpp had any offline coverage.
// Every interesting function in that file is a closed loop -- press a key,
// re-read the screen, decide what to do next -- so exercising one needed a real
// headless on COM3. status.md called this the biggest testability gap in the
// driver, and it is why bugs #22-#27 all stayed invisible until somebody
// happened to hold a connection open against real hardware.
//
// FakeM8 implements ISerial, so it substitutes for the serial port and nothing
// above it knows the difference. It speaks the real display protocol: SLIP
// framing, 0xFD draw-char commands with absolute pixel coordinates, a 0xFF
// sysinfo frame, and it reads 'E'/'R'/'D' and 'C' <mask> key presses the same
// way the device does.
//
// Byte level on purpose. A fake that handed back a ready-made ScreenGrid would
// bypass SLIP framing, the settle loop and the key-press encoding -- and that is
// exactly where several of the real bugs lived (#24's ghost cells only appear
// across reads within one connection; #32 was a coordinate fault in the frames
// themselves).
//
// This file establishes the seam and proves it carries traffic in both
// directions. Driving the primitives themselves through it is the next step.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"

using namespace m8::dev;

namespace {

// One row of a form screen, as the device would draw it.
struct FakeRow {
    int         row;    // screen row (0-23); pixel y is row * 10
    std::string text;   // drawn from column 0
};

class FakeM8 : public ISerial {
public:
    // ---- what the test sets up --------------------------------------------
    std::vector<FakeRow> rows;
    int cursorRow = -1;         // screen row drawn in the accent colour, or -1

    // ---- what the test inspects afterwards --------------------------------
    std::vector<uint8_t> control;   // 'E', 'R', 'D' etc, in order
    std::vector<uint8_t> presses;   // every mask sent after a 'C'
    int  openCount  = 0;
    bool closed     = false;

    // Prime the screen without going through open(). open() sleeps 500ms
    // between 'E' and 'R' on the real device, and most of these tests are not
    // about the handshake -- paying it five times over would put 2.5s of
    // sleeping into every suite run for nothing.
    void powerOn() { queueFullRepaint(); }

    // ---- ISerial -----------------------------------------------------------
    bool open(const char*) override { ++openCount; queueFullRepaint(); return true; }
    void close() override { closed = true; }
    bool send(const void* data, size_t len) override {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) feed(p[i]);
        return true;
    }
    bool sendByte(uint8_t b) override { feed(b); return true; }

    size_t recv(uint8_t* buf, size_t cap) override {
        size_t n = 0;
        while (n < cap && !m_out.empty()) { buf[n++] = m_out.front(); m_out.pop_front(); }
        return n;
    }

private:
    std::deque<uint8_t> m_out;
    bool m_expectMask = false;      // the byte after 'C' is the key mask

    void feed(uint8_t b) {
        if (m_expectMask) {
            m_expectMask = false;
            presses.push_back(b);
            return;                 // a press changes nothing unless a test says so
        }
        if (b == 'C') { m_expectMask = true; return; }
        control.push_back(b);
        // 'R' is a full-framebuffer resend. Modelling it matters: it is what
        // repaints ghost cells away, and mis-reading that caused bug #24.
        if (b == 'R' || b == 'E') queueFullRepaint();
    }

    void emit(const std::vector<uint8_t>& frame) {
        for (uint8_t b : frame) {           // SLIP escaping
            if (b == 0xC0)      { m_out.push_back(0xDB); m_out.push_back(0xDC); }
            else if (b == 0xDB) { m_out.push_back(0xDB); m_out.push_back(0xDD); }
            else                  m_out.push_back(b);
        }
        m_out.push_back(0xC0);              // END
    }

    void drawChar(char ch, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        emit({0xFD, static_cast<uint8_t>(ch),
              static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
              static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
              r, g, b, 0, 0, 0});
    }

    void queueFullRepaint() {
        emit({0xFF, 0, 6, 5, 2, 0});        // sysinfo: hw 0, fw 6.5.2, font 0
        for (const auto& fr : rows) {
            const bool cur = (fr.row == cursorRow);
            // The accent is ScreenGrid::cursorColor's default, which was measured
            // off a real headless on fw 6.5.2 -- so no theme setup is needed.
            const uint8_t r = cur ? 0   : 200;
            const uint8_t g = cur ? 240 : 200;
            const uint8_t b = cur ? 248 : 200;
            for (size_t i = 0; i < fr.text.size(); ++i)
                drawChar(fr.text[i], static_cast<int>(i) * 8, fr.row * 10, r, g, b);
        }
    }
};

// The PROJECT screen, transcribed from artifacts/scope_PROJECT.json (fw 6.5.2).
std::vector<FakeRow> projectRows() {
    return {
        { 1, "PROJECT"},
        { 5, " TEMPO        120.00 <>"},
        { 6, " TRANSPOSE    00"},
        { 7, " GROOVE       00DEFAULT"},
        { 8, " SCALE        00C CHROMATIC"},
        { 9, " LIVE QUANTIZ 00CHAIN LEN"},
        {11, " MIDI         SETTINGS MAPPINGS"},
        {13, " NAME         SCALEPROBE--"},
        {14, " PROJECT      LOAD SAVE NEW"},
        {17, " INST. POOL   VIEW INST.POOL"},
        {20, " SYSTEM       SETTINGS"},
    };
}

// Reads are cheap here but not free: readSettled waits for a quiet window, so
// keep the settle short. These are the smallest values the loop honours.
constexpr int kMin = 0, kSettle = 20, kMax = 400;

} // namespace

TEST_CASE("FakeM8: the transport seam carries a screen", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();

    M8Device dev;
    dev.setSerial(&fake);
    REQUIRE(dev.openNoReset("FAKE"));      // openNoReset: skips open()'s 'R'
    dev.readSettled(kMin, kSettle, kMax);

    // The bytes really went through SLIP and the 0xFD decoder, not around them.
    CHECK(fake.openCount == 1);
    CHECK(identifyScreen(dev.grid()) == Screen::PROJECT);
    CHECK(dev.grid().cells.size() > 100);

    // And the decoded text is the text the fake drew.
    bool sawTranspose = false;
    for (const auto& [y, text] : dev.grid().mainRows())
        if (text.find("TRANSPOSE") != std::string::npos) sawTranspose = true;
    CHECK(sawTranspose);
}

TEST_CASE("FakeM8: the firmware frame is decoded", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();

    fake.powerOn();
    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    auto fw = dev.firmware();
    CHECK(fw.major == 6);
    CHECK(fw.minor == 5);
    CHECK(fw.patch == 2);
    CHECK(fw.fontMode == 0);
}

TEST_CASE("FakeM8: key presses reach the wire as 'C' mask then release", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();

    fake.powerOn();
    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    dev.press(Key::DOWN, 5);

    // A press is mask-down then mask-up. The release matters: a kill landing
    // between the two leaves a key held auto-repeating, which is why m8drv
    // sends a bare release after every restart.
    REQUIRE(fake.presses.size() == 2);
    CHECK(fake.presses[0] == Key::DOWN);
    CHECK(fake.presses[1] == 0x00);
}

TEST_CASE("FakeM8: the accent row is what the driver calls the cursor", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();
    fake.cursorRow = 6;                    // TRANSPOSE
    const int expectedY = 6 * 10;

    fake.powerOn();
    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    // This is the read that bugs #5, #22 and #24 all corrupted in different
    // ways, and none of them could be reproduced without a device until now.
    CHECK(dev.grid().cursorRowY() == expectedY);
}

TEST_CASE("FakeM8: 'D' is sent on close", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();
    {
        M8Device dev;
        dev.setSerial(&fake);
        REQUIRE(dev.openNoReset("FAKE"));
        dev.readSettled(kMin, kSettle, kMax);
    }   // destructor closes
    bool sawDisconnect = false;
    for (uint8_t b : fake.control) if (b == 'D') sawDisconnect = true;
    CHECK(sawDisconnect);
    CHECK(fake.closed);
}
