#pragma once

// ===========================================================================
// M8Device — perception + transport layer for M8 headless serial control.
//
// Promotes the main_nav.cpp internals into a reusable library. Provides serial
// communication, SLIP decoding, screen grid parsing, and the single output
// primitive (button press). No engine, no SDL, no audio.
//
// Tier 0 of M8_DEVICE_CONTROL_SPEC.md.
// ===========================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <initializer_list>
#include <optional>
#include <functional>

namespace m8 {
namespace dev {

// ---- Key masks (hardware-verified on firmware 6.5.2) ----------------------

namespace Key {
    constexpr uint8_t LEFT   = 0x80;
    constexpr uint8_t UP     = 0x40;
    constexpr uint8_t DOWN   = 0x20;
    constexpr uint8_t SHIFT  = 0x10;
    constexpr uint8_t PLAY   = 0x08;
    constexpr uint8_t RIGHT  = 0x04;
    constexpr uint8_t OPT    = 0x02;
    constexpr uint8_t EDIT   = 0x01;
} // namespace Key

// ---- Data types -----------------------------------------------------------

struct Firmware {
    int hwType = -1;
    int major = 0, minor = 0, patch = 0;
    int fontMode = -1;

    bool operator>=(const Firmware& o) const {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        return patch >= o.patch;
    }
};

struct Cell {
    uint8_t ch = 0;
    uint8_t fg[3] = {0, 0, 0};
    uint8_t bg[3] = {0, 0, 0};
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t c[3] = {0, 0, 0};
};

struct ScreenId {
    std::string header;   // raw topHeader()
    std::string canon;    // normalized: alnum+upper (e.g. "SONG", "LOADPROJECT")
};

struct FieldRef {
    std::string name;     // canonical, e.g. "PLAY", "CUTOFF", "TEMPO"
    int col = -1;         // text grid column of the label
    int row = -1;         // text grid row
};

// ---- SLIP framing (RFC1055) -----------------------------------------------

struct SlipDecoder {
    std::vector<uint8_t> frame;
    bool inEsc = false;

    bool feed(uint8_t b, std::vector<uint8_t>& out);
    void reset();
};

// ---- Screen grid ----------------------------------------------------------
//
// Decodes the M8's SLIP-framed display protocol (0xFD draw-char, 0xFE rect,
// 0xFF sysinfo) into a text grid. Auto-detects cell pitch from the data.

class ScreenGrid {
public:
    // Pixel x below this is the "main" content area.
    static constexpr int MAIN_X_MAX = 260;

    std::map<std::pair<int,int>, Cell> cells;
    std::vector<Rect> highlights;
    int hwType = -1, fwMajor = 0, fwMinor = 0, fwPatch = 0, fontMode = -1;
    uint8_t lastColor[3] = {0, 0, 0};
    // M8 default theme accent (cyan). Measured off a real headless on fw 6.5.2
    // reporting theme_id "m8-default-6.5.2": the accent is [0,240,248], not the
    // [0,252,248] this defaulted to until 2026-08-18. The old value matched no
    // palette entry, so isCursor() returned false for every cell and every
    // cursor read came back -1 -- which presents as "the device is ignoring
    // keys" when the presses are landing fine. Evidence: `m8_nav --ui-capture`,
    // hw_findings.md "UI-13".
    //
    // The tolerance exists because this is a *theme* colour: a user-selected
    // theme moves it, and an exact equality test turns any such theme into a
    // driver that silently sees no cursor at all. 16 is far below the spacing
    // between the eight palette entries of the stock theme, so it cannot latch
    // onto a neighbouring colour. For a theme further from stock, override it
    // with `m8_nav --cursor-color R,G,B` rather than widening this.
    uint8_t cursorColor[3] = {0, 240, 248};
    int cursorColorTol = 16;

    // True if the cell carries the theme accent, as foreground OR background.
    //
    // Both channels matter. A form screen recolours the cursor's text, but a
    // tracker grid draws its cursor as inverse video -- accent as background
    // behind a dark glyph. This tested the foreground alone until 2026-08-18,
    // so the grid cursor was structurally invisible to it and to everything
    // built on it (cursorRowY, cursorField, moveCursorToGrid).
    // Frame-decode health. A desynchronised stream -- one lost or extra byte --
    // shifts every subsequent field boundary, so x/y decode to nonsense and the
    // grid fills with cells at impossible coordinates. Rendered, that is a
    // plausible-looking screen with the lower rows drifting, and until these
    // counters existed nothing anywhere noticed: `cells[{y,x}] = c` accepted
    // any coordinate, and a short frame was dropped by a silent `return`.
    //
    // Observed on fw 6.5.2 (M8_DRIVER_BUGS.md #32): rows 3-5 correct, rows 6+
    // cumulatively offset, a word wrapped mid-token across two rows -- and
    // `field not found` / `could not find enum` reported downstream, both of
    // which read as navigation bugs and were not.
    int offPanelCells = 0;   // wildly out of range -- a coordinate that is not one
    int offEdgeCells  = 0;   // one cell past the edge -- the device does this, benignly
    // The first offending coordinate, because the count alone cannot tell a
    // desynced stream (wild, varying values) from one legitimate off-panel
    // draw we are wrongly rejecting (the same sane-looking value every read).
    int firstOffPanelX = -1, firstOffPanelY = -1;
    uint8_t firstOffPanelCh = 0;
    int offPitchCells = 0;   // x not a multiple of 8 -- suspicious, not fatal
    int shortFrames   = 0;   // 0xFD too short to carry its documented payload

    bool decodeLooksSane() const { return offPanelCells == 0 && shortFrames == 0; }

    bool isCursor(const Cell& c) const;
    bool isCursorFg(const Cell& c) const;
    bool isCursorBg(const Cell& c) const;

    // Does the accent appear anywhere on the screen as currently decoded?
    //
    // This is the "am I blind?" question, and it exists because the failure it
    // detects is otherwise indistinguishable from a device that is ignoring
    // keys: if the accent is wrong, isCursor() is false everywhere, every
    // cursor query returns -1, and the driver blames the hardware. A screen
    // with cells but no accent means the accent is wrong, not that the M8 has
    // stopped drawing a cursor. See hw_findings.md UI-14.
    bool accentPresent() const;
    bool isInHighlight(int pixelX, int pixelY) const;
    std::string topHeader() const;
    std::string cursorMainText() const;
    std::vector<std::pair<int,std::string>> mainRows() const;
    std::vector<std::pair<int,std::string>> listRows() const;
    int cursorRowY() const;
    void clear();
    void eraseRegion(int x, int y, int w, int h);
    void handleFrame(const std::vector<uint8_t>& f);

    // Normalized header (alnum+upper).
    std::string canon() const;

    // Find a field by label substring in the main area.
    std::optional<FieldRef> findField(const std::string& labelSubstring) const;

    // Read the value text adjacent to a field label (the cell(s) to its right).
    std::optional<std::string> valueAt(int col, int row) const;

    // Print to text file or JSON.
    void printText(FILE* out) const;
    void printJson(const std::string& path) const;

    Firmware firmware() const;

    static int detectPitch(const std::set<int>& coords, int fallback);

private:
    // Canon helper: alnum + toupper.
    static std::string alnumUpper(const std::string& s);
};

// ---- Serial transport -----------------------------------------------------

// The I/O seam. Every byte M8Device puts on or takes off the wire goes through
// this, which is what lets a test drive the primitives without hardware.
//
// It exists because Primitives.cpp had NO offline coverage at all: every closed
// loop in it -- moveCursorTo, editValue, dismissModal, loadFile -- presses a key
// and re-reads, so none of them could be exercised without a real M8 on COM3.
// That is how a whole family of bugs (#22-#27) stayed invisible until someone
// happened to hold a connection open on real hardware.
//
// Deliberately at the byte level rather than at the ScreenGrid level: a fake
// that hands back a ready-made grid would skip SLIP framing, the settle logic
// and the key-press encoding, which is where several of those bugs actually
// lived.
struct ISerial {
    virtual ~ISerial() = default;
    virtual bool   open(const char* port) = 0;
    virtual bool   send(const void* data, size_t len) = 0;
    virtual bool   sendByte(uint8_t b) = 0;
    virtual size_t recv(uint8_t* buf, size_t cap) = 0;
    virtual void   close() = 0;
};

// The real one (Win32 / POSIX).
struct SerialPort : ISerial {
#ifdef _WIN32
    void* h = nullptr;  // HANDLE, void* to avoid Win32 header in this header
#else
    int fd = -1;
#endif

    bool open(const char* port) override;
    bool send(const void* data, size_t len) override;
    bool sendByte(uint8_t b) override;
    size_t recv(uint8_t* buf, size_t cap) override;
    void close() override;
    ~SerialPort() override;
};

// ---- M8Device -------------------------------------------------------------

// Telemetry for the most recent read. Lets callers (and agents) distinguish
// "the screen went quiet and I read a settled frame" from "I gave up on a
// timeout and this grid may be mid-repaint".
struct ReadStats {
    int  elapsedMs   = 0;      // wall time spent in the read
    int  quietMs     = 0;      // ms since the last byte arrived, at exit
    int  framesSeen  = 0;      // complete SLIP frames decoded this read
    bool settled     = false;  // true = exited via the settle branch
    bool timedOut    = false;  // true = exited via the maxMs branch
    // True = the read never touched the port, because a LiveReader owns it.
    // A pull read draining the same port as the live thread would interleave
    // bytes into one SlipDecoder and desync it -- which is indistinguishable
    // from the garbled stream of #32, and would be blamed on the device. The
    // pull path refuses instead of racing.
    bool liveConflict = false;
    // Mirrors ScreenGrid's decode counters at the end of the read, so a caller
    // holding only the stats can still tell a clean read from a corrupt one.
    int  offPanelCells = 0;
    int  offPitchCells = 0;
    int  shortFrames   = 0;
};

// One turn of the drain loop: what came off the wire and what it decoded to.
// Returned by pumpOnce() so a caller running its own loop can track liveness
// (bytes) separately from progress (frames) -- during playback the M8 sends
// bytes constantly, so only the frame count says the picture actually moved.
struct PumpResult {
    size_t bytes  = 0;   // raw bytes taken off the port this turn
    int    frames = 0;   // complete SLIP frames decoded from them
};

class M8Device {
public:
    M8Device() = default;
    ~M8Device();

    // Session control
    bool open(const char* port);           // serial + 'E' enable + 'R' reset
    bool openNoReset(const char* port);    // serial + 'E' enable only
    void close();
    bool isOpen() const { return m_open; }

    // Substitute the transport. For tests only -- see tests/test_device_fake.cpp.
    // Passing nullptr restores the real port. Must be called before open().
    void setSerial(ISerial* s) { m_port = s ? s : &m_ownPort; }

    // ---- Perception -------------------------------------------------------

    // Drain serial into the grid until settled, then return the grid.
    const ScreenGrid& read(int settleMs = 250, int maxMs = 2000);

    // Identity of what's shown now.
    ScreenId screen();

    // Firmware from 0xFF sysinfo.
    Firmware firmware() const;

    // The highlighted field (cursor = accent-cyan fg in main area).
    std::optional<FieldRef> cursorField();

    // All main-area text rows.
    std::vector<std::pair<int,std::string>> rows();
    std::vector<std::pair<int,std::string>> listRows();

    // Read a field's current value text (cells to the right of the label).
    std::optional<std::string> valueOf(const FieldRef& field);

    // ---- Output (the single primitive) ------------------------------------

    void press(uint8_t mask, int holdMs = 40);
    void chord(std::initializer_list<uint8_t> keys, int holdMs = 40);
    void playToggle();
    void keyjazz(uint8_t note, uint8_t vel);

    // ---- Convenience ------------------------------------------------------

    // Wait for a settled screen after a press.
    void step(int settleMs = 250, int maxMs = 2000);

    // Read a settled screen without pressing anything.
    void readScreen(int settleMs = 250, int maxMs = 2000);

    // Explicit 3-arg read. Prefer this over readScreen() in new code: the
    // 2-arg form's (settleMs, maxMs) order has been misread at call sites.
    // If maxMs <= settleMs the settle branch can never fire and the read
    // degenerates to a fixed maxMs delay.
    void readSettled(int minMs, int settleMs, int maxMs);

    // Access the underlying grid.
    const ScreenGrid& grid() const { return m_grid; }

    // Override the theme accent used to locate the cursor. See the note on
    // ScreenGrid::cursorColor -- a device on a non-stock theme needs this, and
    // without it every cursor read silently returns -1.
    void setCursorColor(uint8_t r, uint8_t g, uint8_t b) {
        m_grid.cursorColor[0] = r;
        m_grid.cursorColor[1] = g;
        m_grid.cursorColor[2] = b;
    }
    void setCursorTolerance(int tol) { m_grid.cursorColorTol = tol; }

    // Tap every byte read from the port, BEFORE SLIP decoding.
    //
    // --record-frames writes the decoded grid, which is the wrong side of the
    // question when the decode itself is suspect -- its own comment says as
    // much. Diagnosing #32 needs the bytes as they arrived: a mis-framed stream
    // and a device sending something we do not parse look identical once
    // decoded, and only the raw form tells them apart.
    void setRawTap(std::vector<uint8_t>* sink) { m_rawTap = sink; }
    const ReadStats& lastRead() const { return m_lastRead; }

    // ---- Live (continuous) reading ----------------------------------------
    //
    // The pull model above answers "show me a settled screen" and pays 250 ms+
    // to do it. It cannot answer "what is on screen right now, while the
    // transport runs", because during playback the M8 redraws the playhead
    // continuously: sinceData never reaches settleMs, so every read burns to
    // maxMs and returns timedOut. That is correct behaviour for a settle-based
    // read and it makes watching playback structurally impossible.
    //
    // LiveReader (LiveReader.h) drives these two on its own thread to keep a
    // grid continuously up to date. They are public only for its benefit --
    // prefer LiveReader over calling them by hand, and see its header for the
    // concurrency contract. Nothing else in this class is thread-safe.

    // Drain whatever bytes are already buffered into the grid and return
    // immediately. Never blocks: SerialPort::recv is opened with
    // ReadIntervalTimeout = MAXDWORD and zero total timeout, the Win32 idiom
    // for "return what you have".
    PumpResult pumpOnce();

    // While a LiveReader owns the port, pull reads refuse rather than race.
    void setLiveOwned(bool owned) { m_liveOwned = owned; }
    bool liveOwned() const { return m_liveOwned; }

private:
    void readInto(int minMs, int settleMs, int maxMs);

    SerialPort m_ownPort;              // the real port, owned
    ISerial*   m_port = &m_ownPort;    // what we actually talk through
    SlipDecoder m_slip;
    ScreenGrid m_grid;
    std::vector<uint8_t>* m_rawTap = nullptr;
    ReadStats m_lastRead;
    bool m_open = false;
    bool m_liveOwned = false;
};

} // namespace dev
} // namespace m8
