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
// way the device does. It also *responds*: arrows move a cursor, the pinned
// edit gestures change a value, and the screen is repainted -- which is what
// closes the loop the primitives are built around.
//
// Byte level on purpose. A fake that handed back a ready-made ScreenGrid would
// bypass SLIP framing, the settle loop and the key-press encoding -- and that is
// exactly where several of the real bugs lived (#24's ghost cells only appear
// across reads within one connection; #32 was a coordinate fault in the frames
// themselves).
//
// What it deliberately does NOT model: partial repaints. The real M8 sends only
// changed cells, which is what leaves the ghosts behind bug #24. That behaviour
// already has coverage at the ScreenGrid level, and modelling it here would make
// every test depend on it.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/Gestures.h"

using namespace m8::dev;

namespace {

// One row of a form screen. `valueCol` >= 0 gives it an editable two-hex-digit
// value, drawn at that column -- the shape every numeric field on PROJECT and
// INSTRUMENT actually has.
struct FakeRow {
    int         row       = 0;    // screen row (0-23); pixel y is row * 10
    std::string label;            // drawn from column 0
    int         valueCol  = -1;
    int         value     = 0;
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

    // Prime the screen without going through open(), which sleeps 500ms
    // between 'E' and 'R' on the real device. Most tests are not about the
    // handshake and paying it repeatedly would put seconds of sleeping into
    // every suite run for nothing.
    void powerOn() { queueFullRepaint(); }

    int valueAt(int row) const {
        for (const auto& r : rows) if (r.row == row) return r.value;
        return -1;
    }

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

    int cursorIndex() const {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].row == cursorRow) return static_cast<int>(i);
        return -1;
    }

    void moveCursor(int delta) {
        int i = cursorIndex();
        if (i < 0) return;
        i = std::clamp(i + delta, 0, static_cast<int>(rows.size()) - 1);
        cursorRow = rows[static_cast<size_t>(i)].row;
    }

    void bumpValue(int delta) {
        int i = cursorIndex();
        if (i < 0) return;
        FakeRow& r = rows[static_cast<size_t>(i)];
        if (r.valueCol < 0) return;             // not an editable field
        // Clamps rather than wraps, which is what the device does and what
        // editValue's re-read-every-iteration loop relies on.
        r.value = std::clamp(r.value + delta, 0, 255);
    }

    void applyPress(uint8_t mask) {
        if (mask == 0) return;                  // release
        const auto& g = getGestures();
        // Gestures first: EDIT|<arrow> must not be read as a bare arrow.
        if (g.isReady()) {
            if (mask == g.valueInc)   { bumpValue(+1);  queueFullRepaint(); return; }
            if (mask == g.valueDec)   { bumpValue(-1);  queueFullRepaint(); return; }
            if (mask == g.valueInc16) { bumpValue(+16); queueFullRepaint(); return; }
            if (mask == g.valueDec16) { bumpValue(-16); queueFullRepaint(); return; }
        }
        if (mask == Key::DOWN) { moveCursor(+1); queueFullRepaint(); return; }
        if (mask == Key::UP)   { moveCursor(-1); queueFullRepaint(); return; }
        // Anything else lands but changes nothing, which is a real M8 behaviour
        // and the reason `probe`'s "nothing moved" verdict is a heuristic.
    }

    void feed(uint8_t b) {
        if (m_expectMask) {
            m_expectMask = false;
            presses.push_back(b);
            applyPress(b);
            return;
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

    void drawChar(char ch, int col, int row, uint8_t r, uint8_t g, uint8_t b) {
        const int x = col * 8, y = row * 10;
        emit({0xFD, static_cast<uint8_t>(ch),
              static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
              static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
              r, g, b, 0, 0, 0});
    }

    void queueFullRepaint() {
        emit({0xFF, 0, 6, 5, 2, 0});        // sysinfo: hw 0, fw 6.5.2, font 0
        for (const auto& fr : rows) {
            const bool cur = (fr.row == cursorRow);
            // The accent is ScreenGrid::cursorColor's default, measured off a
            // real headless on fw 6.5.2 -- so no theme setup is needed.
            const uint8_t r = cur ? 0   : 200;
            const uint8_t g = cur ? 240 : 200;
            const uint8_t b = cur ? 248 : 200;

            // Labels start at COLUMN 1, and the accent run is CONTIGUOUS from
            // there through the end of the value -- interior padding included.
            // Both details are measured, not invented (artifacts/scope_PROJECT.json:
            // the cursor row's accent covers cols 1-16, "TEMPO        120"), and
            // getting them wrong is not cosmetic. readCursorValue strips the
            // field label with a strict PREFIX compare, so a label drawn at
            // column 0 makes cursorMainText() return " TRANSPOSE00", the compare
            // misses, nothing is stripped, and the leading hex run of
            // "TRANSPOSE00" parses as 0. editValue then reads 0 forever, steps
            // up 256 times and clamps at 0xFF -- which is exactly bug #13's
            // symptom, reproduced here by an unfaithful fake rather than by a
            // real defect.
            const int labelCol = 1;
            for (size_t i = 0; i < fr.label.size(); ++i)
                drawChar(fr.label[i], labelCol + static_cast<int>(i), fr.row, r, g, b);
            if (fr.valueCol >= 0) {
                for (int c = labelCol + static_cast<int>(fr.label.size());
                     c < fr.valueCol; ++c)
                    drawChar(' ', c, fr.row, r, g, b);
                char buf[3];
                std::snprintf(buf, sizeof(buf), "%02X", fr.value & 0xFF);
                drawChar(buf[0], fr.valueCol,     fr.row, r, g, b);
                drawChar(buf[1], fr.valueCol + 1, fr.row, r, g, b);
            }
        }
    }
};

// The PROJECT screen, transcribed from artifacts/scope_PROJECT.json (fw 6.5.2).
// Labels start at column 1 and values at column 14, matching the real screen --
// which matters, because kProjectFields' columns are asserted against exactly
// those positions in test_device_decode.cpp.
std::vector<FakeRow> projectRows() {
    return {
        { 1, "PROJECT"},
        { 5, "TEMPO"},
        { 6, "TRANSPOSE",   14, 0x00},
        { 7, "GROOVE",      14, 0x00},
        { 8, "SCALE",       14, 0x00},
        { 9, "LIVE QUANTIZ",14, 0x00},
        {11, "MIDI"},
        {13, "NAME"},
        {14, "PROJECT"},
        {17, "INST. POOL"},
        {20, "SYSTEM"},
    };
}

// Populate the global gesture table for the duration of a test, then put back
// whatever was there. editValue refuses to run without pinned gestures, and the
// table is a process-wide global, so leaving it set would leak into other tests.
struct ScopedGestures {
    GestureTable saved;
    ScopedGestures() {
        saved = getGestures();
        GestureTable& g = getGestures();
        g.populated  = true;
        g.pinnedFwMajor = 6; g.pinnedFwMinor = 5; g.pinnedFwPatch = 2;
        // The masks actually pinned in hw_buttons.json on this device (fw 6.5.2),
        // read out of the file rather than assumed:
        //   value_inc   0x05  EDIT+RIGHT  (+1)
        //   value_dec   0x81  EDIT+LEFT   (-1)
        //   value_inc16 0x41  EDIT+UP     (+16)
        //   value_dec16 0x21  EDIT+DOWN   (-16)
        // This fixture had the fine and coarse pairs SWAPPED until 2026-08-19.
        // Nothing went red, because the fake and the fixture agreed with each
        // other -- they were just both wrong about the device, so the
        // coarse-step test was not exercising the gesture the M8 uses as coarse.
        // A fake is only worth what its constants are worth.
        g.valueInc   = Key::EDIT | Key::RIGHT;
        g.valueDec   = Key::EDIT | Key::LEFT;
        g.valueInc16 = Key::EDIT | Key::UP;
        g.valueDec16 = Key::EDIT | Key::DOWN;
        g.enumNext   = g.valueInc;
        g.enumPrev   = g.valueDec;
    }
    ~ScopedGestures() { getGestures() = saved; }
};

// ---- a grid screen ---------------------------------------------------------
//
// SONG, modelled from a real capture (hwtest_out/caps/chk1.json, fw 6.5.2).
// Grid screens are addressed and read completely differently from form screens
// -- (step, col) instead of a field name, with the column read off the HEADER
// row rather than the cursor row -- so none of the form fake above exercises
// any of it. Bugs #22, #23 and #27 all lived in exactly that path.
//
// The transport plumbing below is duplicated from FakeM8 rather than hoisted
// into a shared base. That is a deliberate trade: FakeM8's tests pass, and
// restructuring a working class to save thirty lines in a test file is the more
// expensive mistake. If a third screen ever needs one, extract it then.
class FakeSongGrid : public ISerial {
public:
    // Measured from the capture: track headers at columns 4,7,...,25 (pitch 3),
    // data rows 6-21, row label in columns 1-2.
    static constexpr int kHeaderRow   = 5;
    static constexpr int kFirstRow    = 6;
    static constexpr int kSteps       = 16;
    static constexpr int kCols        = 8;
    static int colX(int c) { return 4 + c * 3; }

    int step = 0, col = 0;
    std::vector<uint8_t> presses;

    void powerOn() { repaint(); }

    bool open(const char*) override { repaint(); return true; }
    void close() override {}
    bool send(const void* d, size_t n) override {
        const auto* p = static_cast<const uint8_t*>(d);
        for (size_t i = 0; i < n; ++i) feed(p[i]);
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
    bool m_expectMask = false;

    void feed(uint8_t b) {
        if (m_expectMask) {
            m_expectMask = false;
            presses.push_back(b);
            if      (b == Key::DOWN)  { step = std::min(step + 1, kSteps - 1); repaint(); }
            else if (b == Key::UP)    { step = std::max(step - 1, 0);          repaint(); }
            else if (b == Key::RIGHT) { col  = std::min(col + 1, kCols - 1);   repaint(); }
            else if (b == Key::LEFT)  { col  = std::max(col - 1, 0);           repaint(); }
            return;
        }
        if (b == 'C') { m_expectMask = true; return; }
        if (b == 'R' || b == 'E') repaint();
    }

    void emit(const std::vector<uint8_t>& f) {
        for (uint8_t b : f) {
            if (b == 0xC0)      { m_out.push_back(0xDB); m_out.push_back(0xDC); }
            else if (b == 0xDB) { m_out.push_back(0xDB); m_out.push_back(0xDD); }
            else                  m_out.push_back(b);
        }
        m_out.push_back(0xC0);
    }

    void ch(char c, int cx, int cy, uint8_t r, uint8_t g, uint8_t b) {
        const int x = cx * 8, y = cy * 10;
        emit({0xFD, static_cast<uint8_t>(c),
              static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
              static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
              r, g, b, 0, 0, 0});
    }

    void repaint() {
        emit({0xFF, 0, 6, 5, 2, 0});
        const uint8_t nr = 200, ng = 200, nb = 200;   // normal
        const uint8_t ar = 0,   ag = 240, ab = 248;   // accent

        const char* hdr = "SONG";
        for (int i = 0; hdr[i]; ++i) ch(hdr[i], 1 + i, 3, 248, 32, 64);

        // Column header. The cursor's OWN column header cell is accented -- the
        // M8 really does this, and bug #23's fix reads the current column
        // straight off it instead of measuring pixel pitch.
        for (int c = 0; c < kCols; ++c) {
            const bool cur = (c == col);
            ch(static_cast<char>('1' + c), colX(c), kHeaderRow,
               cur ? ar : nr, cur ? ag : ng, cur ? ab : nb);
        }

        for (int sfor = 0; sfor < kSteps; ++sfor) {
            const int row = kFirstRow + sfor;
            const bool cur = (sfor == step);
            char lbl[3];
            std::snprintf(lbl, sizeof(lbl), "%02X", sfor);
            // The row label carries the accent on the cursor's row. cursorRowY()
            // looks for exactly this in the left label columns (x < 24) -- bug
            // #22 was it taking the topmost accent cell instead, which on a grid
            // screen is always the column header.
            ch(lbl[0], 1, row, cur ? ar : nr, cur ? ag : ng, cur ? ab : nb);
            ch(lbl[1], 2, row, cur ? ar : nr, cur ? ag : ng, cur ? ab : nb);
            for (int c = 0; c < kCols; ++c) {
                const bool onCell = cur && (c == col);
                const uint8_t r = onCell ? ar : nr, g = onCell ? ag : ng,
                              b = onCell ? ab : nb;
                if (onCell) ch(' ', colX(c) - 1, row, r, g, b);   // matches the capture
                ch('-', colX(c),     row, r, g, b);
                ch('-', colX(c) + 1, row, r, g, b);
            }
        }
    }
};

// readSettled waits for a quiet window; keep it short so the loops stay fast.
constexpr int kMin = 0, kSettle = 20, kMax = 400;
constexpr int kHold = 1;

} // namespace

// ---- the seam itself -------------------------------------------------------

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
    CHECK(dev.grid().cells.size() > 50);

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

    dev.press(Key::DOWN, kHold);

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
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    // This is the read that bugs #5, #22 and #24 all corrupted in different
    // ways, and none of them could be reproduced without a device until now.
    CHECK(dev.grid().cursorRowY() == 60);
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

// ---- the closed loops, which is the point ----------------------------------

TEST_CASE("FakeM8: arrows move the cursor and the driver follows", "[hwdecode]") {
    FakeM8 fake;
    fake.rows = projectRows();
    fake.cursorRow = 5;                    // TEMPO
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);
    REQUIRE(dev.grid().cursorRowY() == 50);

    dev.press(Key::DOWN, kHold);
    dev.readSettled(kMin, kSettle, kMax);
    CHECK(dev.grid().cursorRowY() == 60);  // TRANSPOSE

    dev.press(Key::DOWN, kHold);
    dev.readSettled(kMin, kSettle, kMax);
    CHECK(dev.grid().cursorRowY() == 70);  // GROOVE

    dev.press(Key::UP, kHold);
    dev.readSettled(kMin, kSettle, kMax);
    CHECK(dev.grid().cursorRowY() == 60);
}

TEST_CASE("moveCursorTo reaches a named field", "[hwdecode]") {
    ScopedGestures gestures;
    FakeM8 fake;
    fake.rows = projectRows();
    fake.cursorRow = 5;                    // start on TEMPO
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    auto res = moveCursorTo(dev, "TRANSPOSE", kHold);
    INFO("moveCursorTo: " << res.error);
    CHECK(res.ok);
    CHECK(fake.cursorRow == 6);
}

TEST_CASE("editValue converges upward using coarse then fine steps", "[hwdecode]") {
    ScopedGestures gestures;
    FakeM8 fake;
    fake.rows = projectRows();
    fake.cursorRow = 6;                    // already on TRANSPOSE
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    // 0x00 -> 0x20. Bug #25: this used to single-step up to 256 times at a
    // ~320ms floor, which presented as a hang rather than as slow work.
    //
    // The gap is deliberately small. editValue hardcodes readSettled(120, 200,
    // 1200), so every iteration costs ~320ms whatever the transport is -- a
    // 0x40 gap made this test alone take tens of seconds. 0x20 exercises the
    // same coarse-then-fine path in a fraction of the time. Changing the
    // primitive's timings to suit a test would be the wrong trade.
    auto res = editValue(dev, "TRANSPOSE", "20", kHold);
    INFO("editValue: " << res.error);
    CHECK(res.ok);
    CHECK(fake.valueAt(6) == 0x20);

    // The coarse gesture must actually have been used: single-stepping a gap of
    // 32 needs 32 presses, coarse-then-fine needs a handful. Each press logs a
    // mask and a release, so 20 entries is roughly 10 presses.
    CHECK(fake.presses.size() < 20);
}

TEST_CASE("editValue converges downward", "[hwdecode]") {
    ScopedGestures gestures;
    FakeM8 fake;
    fake.rows = projectRows();
    fake.rows[2].value = 0xC0;             // TRANSPOSE starts high
    fake.cursorRow = 6;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    // Bug #14: the stepping loop only ever pressed value_inc, so a target BELOW
    // the current value was structurally unreachable -- values clamp at 0xFF
    // rather than wrapping, so it just pinned there.
    auto res = editValue(dev, "TRANSPOSE", "A0", kHold);
    INFO("editValue: " << res.error);
    CHECK(res.ok);
    CHECK(fake.valueAt(6) == 0xA0);
}

TEST_CASE("editValue parses its target as hex, not decimal", "[hwdecode]") {
    ScopedGestures gestures;
    FakeM8 fake;
    fake.rows = projectRows();
    fake.rows[2].value = 0x18;             // 24 decimal -- between the two readings
    fake.cursorRow = 6;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    // Bug #26, and the worst failure mode in the whole list: with base-0 parsing
    // `set PAN 80` converged correctly and silently on 80 DECIMAL = 0x50, and
    // nothing in the result said anything was off.
    //
    // Starting at 0x18 (24) makes the two readings differ in DIRECTION as well
    // as value: "20" as hex is 32 and walks UP, as decimal it is 20 and walks
    // DOWN. So a regression cannot pass by landing near enough -- it ends up on
    // the wrong side. It also converges in a couple of steps, which keeps the
    // ~320ms-per-iteration cost down.
    auto res = editValue(dev, "TRANSPOSE", "20", kHold);
    INFO("editValue: " << res.error);
    CHECK(res.ok);
    CHECK(fake.valueAt(6) == 0x20);        // 32, not 20
    CHECK(fake.valueAt(6) != 20);
}

// ---- grid-screen loops -----------------------------------------------------

TEST_CASE("FakeSong: a grid screen decodes with its column header", "[hwdecode]") {
    FakeSongGrid fake;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    CHECK(identifyScreen(dev.grid()) == Screen::SONG);
    // Row 0 of the grid sits at pixel y 60, and the cursor starts there.
    CHECK(dev.grid().cursorRowY() == FakeSongGrid::kFirstRow * 10);
}

TEST_CASE("cursorRowY prefers the row label over the column header on a grid",
          "[hwdecode]") {
    // Bug #22, live: the column header is accented too and sits ABOVE the data,
    // so a scan that takes the topmost accent cell reports the header row and
    // the cursor never appears to move. Put the cursor low so the two rows are
    // far apart and the wrong answer is unmistakable.
    FakeSongGrid fake;
    fake.step = 9;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    CHECK(dev.grid().cursorRowY() == (FakeSongGrid::kFirstRow + 9) * 10);
    CHECK(dev.grid().cursorRowY() != FakeSongGrid::kHeaderRow * 10);
}

TEST_CASE("moveCursorToGrid reaches a (step, col)", "[hwdecode]") {
    // Bug #23: every call failed rc=7 because the column axis was wrong three
    // ways at once. The fix reads the current column off the accented HEADER
    // cell, which is what this fake draws.
    FakeSongGrid fake;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    auto res = moveCursorToGrid(dev, 7, 5, kHold);
    INFO("moveCursorToGrid: " << res.error);
    CHECK(res.ok);
    CHECK(fake.step == 7);
    CHECK(fake.col == 5);
}

TEST_CASE("moveCursorToGrid moves both axes backwards too", "[hwdecode]") {
    FakeSongGrid fake;
    fake.step = 12;
    fake.col  = 6;
    fake.powerOn();

    M8Device dev;
    dev.setSerial(&fake);
    dev.readSettled(kMin, kSettle, kMax);

    auto res = moveCursorToGrid(dev, 3, 1, kHold);
    INFO("moveCursorToGrid: " << res.error);
    CHECK(res.ok);
    CHECK(fake.step == 3);
    CHECK(fake.col == 1);
}
