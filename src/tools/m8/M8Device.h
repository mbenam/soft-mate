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
    uint8_t cursorColor[3] = {0, 252, 248};  // M8 default theme accent (cyan)

    bool isCursor(const Cell& c) const;
    bool isInHighlight(int pixelX, int pixelY) const;
    std::string topHeader() const;
    std::string cursorMainText() const;
    std::vector<std::pair<int,std::string>> mainRows() const;
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

// ---- Serial port (Win32) --------------------------------------------------

struct SerialPort {
#ifdef _WIN32
    void* h = nullptr;  // HANDLE, void* to avoid Win32 header in this header
#else
    int fd = -1;
#endif

    bool open(const char* port);
    bool send(const void* data, size_t len);
    bool sendByte(uint8_t b);
    size_t recv(uint8_t* buf, size_t cap);
    void close();
    ~SerialPort();
};

// ---- M8Device -------------------------------------------------------------

class M8Device {
public:
    M8Device() = default;
    ~M8Device();

    // Session control
    bool open(const char* port);           // serial + 'E' enable + 'R' reset
    bool openNoReset(const char* port);    // serial + 'E' enable only
    void close();
    bool isOpen() const { return m_open; }

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

    // Access the underlying grid.
    const ScreenGrid& grid() const { return m_grid; }

private:
    void readInto(int minMs, int settleMs, int maxMs);

    SerialPort m_port;
    SlipDecoder m_slip;
    ScreenGrid m_grid;
    bool m_open = false;
};

} // namespace dev
} // namespace m8
