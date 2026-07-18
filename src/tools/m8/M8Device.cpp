// ===========================================================================
// M8Device.cpp — perception + transport implementation.
// ===========================================================================

#include "M8Device.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace m8 {
namespace dev {

// ---- SLIPDecoder ----------------------------------------------------------

bool SlipDecoder::feed(uint8_t b, std::vector<uint8_t>& out) {
    static constexpr uint8_t SLIP_END     = 0xC0;
    static constexpr uint8_t SLIP_ESC     = 0xDB;
    static constexpr uint8_t SLIP_ESC_END = 0xDC;
    static constexpr uint8_t SLIP_ESC_ESC = 0xDD;

    if (inEsc) {
        frame.push_back(b == SLIP_ESC_END ? SLIP_END : (b == SLIP_ESC_ESC ? SLIP_ESC : b));
        inEsc = false;
        return false;
    }
    if (b == SLIP_ESC) { inEsc = true; return false; }
    if (b == SLIP_END) {
        if (frame.empty()) return false;
        out = frame;
        frame.clear();
        return true;
    }
    frame.push_back(b);
    return false;
}

void SlipDecoder::reset() {
    frame.clear();
    inEsc = false;
}

// ---- ScreenGrid -----------------------------------------------------------

std::string ScreenGrid::alnumUpper(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)))
            o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

bool ScreenGrid::isCursor(const Cell& c) const {
    return c.fg[0] == cursorColor[0] && c.fg[1] == cursorColor[1] && c.fg[2] == cursorColor[2];
}

bool ScreenGrid::isInHighlight(int pixelX, int pixelY) const {
    for (auto& r : highlights) {
        if (pixelX >= r.x && pixelX < r.x + r.w &&
            pixelY >= r.y && pixelY < r.y + r.h)
            return true;
    }
    return false;
}

std::string ScreenGrid::topHeader() const {
    std::map<int, std::map<int, char>> rows;
    for (auto& [pos, c] : cells) {
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        if (g != ' ') rows[pos.first][pos.second] = g;
    }
    for (auto& [y, cols] : rows) {
        std::string s;
        for (auto& [x, g] : cols) s += g;
        size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
        if (a != std::string::npos) return s.substr(a, b - a + 1);
    }
    return "";
}

std::string ScreenGrid::canon() const {
    return alnumUpper(topHeader());
}

std::string ScreenGrid::cursorMainText() const {
    int y = cursorRowY();
    if (y < 0) return {};
    // Check for '<' marker (grid screens like PHRASE, TABLE, INSTRUMENT).
    bool hasCursorMarker = false;
    for (auto& [pos, c] : cells) {
        if (c.ch == '<' && pos.first == y && pos.second < 16) {
            hasCursorMarker = true;
            break;
        }
    }
    if (hasCursorMarker) {
        // Grid screen: return full row text (callers parse field values from it).
        std::map<int, char> cols;
        for (auto& [pos, c] : cells) {
            if (pos.first != y || pos.second >= MAIN_X_MAX) continue;
            char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
            cols[pos.second] = g;
        }
        std::string out;
        for (auto& [x, g] : cols) out += g;
        return out;
    }
    // Non-grid screen: return only accent-colored cells (the cursor field value).
    // Do NOT exclude highlight-area cells: on form screens the value text IS inside
    // the selection highlight and must be included. Grid screens use the '<' glyph
    // as their cursor signal and never reach this branch.
    std::map<int, char> cols;
    for (auto& [pos, c] : cells) {
        if (!isCursor(c) || pos.first != y || pos.second >= MAIN_X_MAX) continue;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        cols[pos.second] = g;
    }
    std::string out;
    for (auto& [x, g] : cols) out += g;
    return out;
}

std::vector<std::pair<int, std::string>> ScreenGrid::mainRows() const {
    std::map<int, std::map<int, char>> rows;
    for (auto& [pos, c] : cells) {
        if (pos.second >= MAIN_X_MAX) continue;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        rows[pos.first][pos.second] = g;
    }
    std::vector<std::pair<int, std::string>> out;
    for (auto& [y, cols] : rows) {
        std::string s;
        int lastx = -1;
        for (auto& [x, g] : cols) { s += g; lastx = x; }
        size_t b = s.find_last_not_of(' ');
        out.push_back({y, b == std::string::npos ? "" : s.substr(0, b + 1)});
    }
    return out;
}

int ScreenGrid::cursorRowY() const {
    // Grid screens use '<' character in column 0 as the cursor marker.
    // Prefer this over accent-color matching when present.
    for (auto& [pos, c] : cells) {
        if (c.ch == 0x3C && pos.second < 16 && pos.first >= 0)
            return pos.first;
    }
    // Fallback: topmost accent-colored cell in main area (works for non-grid screens).
    //
    // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, PROJECT screen): when the
    // cursor moves off a row, the M8 does not always resend a color update for
    // that row's blank/space cells -- a cyan space looks pixel-identical to a
    // plain space, so the firmware skips the redundant redraw. Our decoder still
    // caches the old cyan color for those cells, leaving a "ghost" ahead of the
    // real cursor at its old row. A single DOWN press from TEMPO (row 2) to
    // TRANSPOSE (row 3) reproduced this exactly: after the press, y=50 (TEMPO's
    // old row) still had 8 cyan cells, but every one was ch==' ' -- the actual
    // "TEMPO" letters there had correctly recolored back to normal, only the
    // trailing padding remained a ghost. Requiring a non-space character here
    // is the fix: the real cursor row always has real (label) text colored
    // cyan, so this can't false-negative on it, and it can't false-positive on
    // a ghost since ghosts are, by construction, always blank padding.
    int best = -1;
    for (auto& [pos, c] : cells)
        if (isCursor(c) && c.ch != ' ' && pos.second < MAIN_X_MAX && !isInHighlight(pos.second, pos.first))
            if (best < 0 || pos.first < best) best = pos.first;
    return best;
}

void ScreenGrid::clear() {
    cells.clear();
    highlights.clear();
}

void ScreenGrid::eraseRegion(int x, int y, int w, int h) {
    for (auto it = cells.begin(); it != cells.end(); ) {
        int cy = it->first.first, cx = it->first.second;
        if (cx >= x && cx < x + w && cy >= y && cy < y + h)
            it = cells.erase(it);
        else
            ++it;
    }
}

void ScreenGrid::handleFrame(const std::vector<uint8_t>& f) {
    if (f.empty()) return;
    switch (f[0]) {
        case 0xFD: {  // draw character: char,xlo,xhi,ylo,yhi,fgRGB,bgRGB
            if (f.size() < 12) return;
            uint8_t ch = f[1];
            int x = f[2] | (f[3] << 8);
            int y = f[4] | (f[5] << 8);
            Cell c;
            c.ch = ch;
            c.fg[0] = f[6]; c.fg[1] = f[7]; c.fg[2] = f[8];
            c.bg[0] = f[9]; c.bg[1] = f[10]; c.bg[2] = f[11];
            cells[{y, x}] = c;
            break;
        }
        case 0xFE: {  // draw rectangle
            int x = f[1] | (f[2] << 8);
            int y = f[3] | (f[4] << 8);
            int w = 1, h = 1;
            uint8_t col[3] = { lastColor[0], lastColor[1], lastColor[2] };
            if (f.size() == 8)       { col[0]=f[5]; col[1]=f[6]; col[2]=f[7]; }
            else if (f.size() == 9)  { w=f[5]|(f[6]<<8); h=f[7]|(f[8]<<8); }
            else if (f.size() >= 12) { w=f[5]|(f[6]<<8); h=f[7]|(f[8]<<8);
                                       col[0]=f[9]; col[1]=f[10]; col[2]=f[11]; }
            lastColor[0]=col[0]; lastColor[1]=col[1]; lastColor[2]=col[2];
            const bool black = (col[0]==0 && col[1]==0 && col[2]==0);
            if (w >= 4 && h >= 4) {
                eraseRegion(x, y, w, h);
                if (black) {
                    highlights.erase(
                        std::remove_if(highlights.begin(), highlights.end(),
                            [&](const Rect& r){ return r.x>=x && r.y>=y && r.x<x+w && r.y<y+h; }),
                        highlights.end());
                } else {
                    highlights.push_back({x, y, w, h, {col[0], col[1], col[2]}});
                }
            }
            break;
        }
        case 0xFF: {  // system info
            if (f.size() >= 5) { hwType=f[1]; fwMajor=f[2]; fwMinor=f[3]; fwPatch=f[4]; }
            if (f.size() >= 6) fontMode=f[5];
            break;
        }
        default: break;
    }
}

Firmware ScreenGrid::firmware() const {
    return {hwType, fwMajor, fwMinor, fwPatch, fontMode};
}

std::optional<FieldRef> ScreenGrid::findField(const std::string& labelSubstring) const {
    std::string want = alnumUpper(labelSubstring);
    // Build text rows from cells.
    std::map<int, std::map<int, char>> rowChars;
    for (auto& [pos, c] : cells) {
        if (pos.second >= MAIN_X_MAX) continue;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        rowChars[pos.first][pos.second] = g;
    }
    for (auto& [y, cols] : rowChars) {
        std::string rowText;
        for (auto& [x, g] : cols) rowText += g;
        std::string rowCanon = alnumUpper(rowText);
        if (rowCanon.find(want) != std::string::npos) {
            // Find the x position of the first character of the match.
            size_t pos = rowCanon.find(want);
            // Map back to pixel x: count 'pos' alnum chars in the row.
            int alnumCount = 0;
            int fieldX = -1;
            for (auto& [x, g] : cols) {
                if (std::isalnum(static_cast<unsigned char>(g))) {
                    if (alnumCount == static_cast<int>(pos)) { fieldX = x; break; }
                    ++alnumCount;
                }
            }
            if (fieldX >= 0) return FieldRef{labelSubstring, fieldX, y};
        }
    }
    return std::nullopt;
}

std::optional<std::string> ScreenGrid::valueAt(int col, int row) const {
    // Collect all cells on this row, sorted by x position.
    std::vector<std::pair<int, char>> rowCells;
    for (auto& [pos, c] : cells) {
        if (pos.first != row) continue;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        rowCells.push_back({pos.second, g});
    }
    std::sort(rowCells.begin(), rowCells.end());

    // Find the first cell at or after `col`.
    std::string val;
    bool started = false;
    int prevX = -100;
    for (auto& [x, g] : rowCells) {
        if (x < col) continue;
        if (!started) {
            // Allow a small gap (up to 2x cell pitch) before the value starts.
            if (prevX >= 0 && x - prevX > 16) break;
            started = true;
        }
        if (started && prevX >= 0 && x - prevX > 16) break;  // gap = end of value
        if (g == ' ' || g == '\0') {
            if (!val.empty()) break;  // trailing space = end of value
            prevX = x;
            continue;
        }
        val += g;
        prevX = x;
    }
    return val.empty() ? std::nullopt : std::optional<std::string>(val);
}

int ScreenGrid::detectPitch(const std::set<int>& coords, int fallback) {
    int best = 1 << 30;
    int prev = -1;
    for (int v : coords) {
        if (prev >= 0) { int d = v - prev; if (d > 0 && d < best) best = d; }
        prev = v;
    }
    return (best == (1 << 30)) ? fallback : best;
}

void ScreenGrid::printText(FILE* out) const {
    if (cells.empty()) { std::fprintf(out, "(no characters decoded)\n"); return; }
    std::set<int> xs, ys;
    for (auto& [pos, c] : cells) { ys.insert(pos.first); xs.insert(pos.second); }
    const int minX = *xs.begin(), minY = *ys.begin();
    const int cw = detectPitch(xs, 8);
    const int ch = detectPitch(ys, 10);
    int maxCol = 0, maxRow = 0;
    for (int x : xs) maxCol = std::max(maxCol, (x - minX) / cw);
    for (int y : ys) maxRow = std::max(maxRow, (y - minY) / ch);

    std::vector<std::string> grid(maxRow + 1, std::string(maxCol + 1, ' '));
    for (auto& [pos, c] : cells) {
        int col = (pos.second - minX) / cw;
        int row = (pos.first  - minY) / ch;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        grid[row][col] = g;
    }
    std::set<int> hlRows;
    for (auto& r : highlights) {
        int rowLo = (r.y - minY) / ch, rowHi = (r.y + r.h - 1 - minY) / ch;
        for (int rr = std::max(0, rowLo); rr <= std::min(maxRow, rowHi); ++rr) hlRows.insert(rr);
    }
    std::map<int, std::string> cursorText;
    // Detect cursor by '<' character (grid screens) or accent color (non-grid screens).
    for (auto& [pos, c] : cells) {
        bool isC = (c.ch == 0x3C && pos.second < 16) || isCursor(c);
        if (!isC) continue;
        int row = (pos.first - minY) / ch;
        hlRows.insert(row);
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        cursorText[row] += g;
    }
    std::fprintf(out, "+%s+\n", std::string(maxCol + 1, '-').c_str());
    for (int r = 0; r <= maxRow; ++r) {
        std::string l = grid[r];
        size_t end = l.find_last_not_of(' ');
        const char* gutter = hlRows.count(r) ? ">" : "|";
        std::fprintf(out, "%s%s\n", gutter, (end == std::string::npos ? "" : l.substr(0, end + 1)).c_str());
    }
    std::fprintf(out, "+%s+\n", std::string(maxCol + 1, '-').c_str());
    std::fprintf(out, "cells=%zu  grid=%dx%d  pitch=%dx%d px  highlights=%zu\n",
                 cells.size(), maxCol + 1, maxRow + 1, cw, ch, highlights.size());
    for (auto& [row, txt] : cursorText)
        std::fprintf(out, "cursor: row %d  \"%s\"\n", row, txt.c_str());
}

void ScreenGrid::printJson(const std::string& path) const {
    std::ofstream o(path);
    if (!o) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    o << "{\n";
    o << "  \"hw_type\": " << hwType
      << ", \"fw\": \"" << fwMajor << "." << fwMinor << "." << fwPatch << "\""
      << ", \"font_mode\": " << fontMode << ",\n";
    o << "  \"cells\": [\n";
    bool first = true;
    for (auto& [pos, c] : cells) {
        if (!first) o << ",\n";
        first = false;
        char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "    {\"x\":%d,\"y\":%d,\"ch\":\"%s%c\",\"fg\":[%d,%d,%d],\"bg\":[%d,%d,%d]}",
            pos.second, pos.first, (g=='"'||g=='\\')?"\\":"", g,
            c.fg[0],c.fg[1],c.fg[2], c.bg[0],c.bg[1],c.bg[2]);
        o << buf;
    }
    o << "\n  ]\n}\n";
}

// ---- SerialPort -----------------------------------------------------------

bool SerialPort::open(const char* port) {
#ifdef _WIN32
    char path[64];
    std::snprintf(path, sizeof(path), "\\\\.\\%s", port);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, 0, nullptr);
    if (h == nullptr) {
        std::fprintf(stderr, "cannot open %s (error %lu)\n", port, GetLastError());
        return false;
    }
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    if (!SetCommState(h, &dcb)) {
        std::fprintf(stderr, "SetCommState failed (error %lu)\n", GetLastError());
        CloseHandle(h); h = nullptr; return false;
    }
    COMMTIMEOUTS t = {};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutMultiplier = 0;
    t.ReadTotalTimeoutConstant = 0;
    SetCommTimeouts(h, &t);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
#else
    (void)port;
    std::fprintf(stderr, "serial not implemented on this platform\n");
    return false;
#endif
}

bool SerialPort::send(const void* data, size_t len) {
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr)) {
        std::fprintf(stderr, "serial write failed (error %lu)\n", GetLastError());
        return false;
    }
    return written == len;
#else
    (void)data; (void)len;
    return false;
#endif
}

bool SerialPort::sendByte(uint8_t b) { return send(&b, 1); }

size_t SerialPort::recv(uint8_t* buf, size_t cap) {
#ifdef _WIN32
    DWORD got = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(cap), &got, nullptr)) return 0;
    return got;
#else
    (void)buf; (void)cap;
    return 0;
#endif
}

void SerialPort::close() {
#ifdef _WIN32
    if (h) { CloseHandle(h); h = nullptr; }
#endif
}

SerialPort::~SerialPort() { close(); }

// ---- M8Device -------------------------------------------------------------

M8Device::~M8Device() { close(); }

bool M8Device::open(const char* port) {
    if (!m_port.open(port)) return false;
    m_port.sendByte('E');
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    m_port.sendByte('R');
    m_open = true;
    return true;
}

bool M8Device::openNoReset(const char* port) {
    if (!m_port.open(port)) return false;
    m_port.sendByte('E');
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    m_open = true;
    return true;
}

void M8Device::close() {
    if (m_open) {
        m_port.sendByte('D');
        m_port.close();
        m_open = false;
    }
}

void M8Device::readInto(int minMs, int settleMs, int maxMs) {
    uint8_t buf[4096];
    std::vector<uint8_t> frame;
    auto start = std::chrono::steady_clock::now();
    auto lastData = start;
    for (;;) {
        size_t n = m_port.recv(buf, sizeof(buf));
        auto now = std::chrono::steady_clock::now();
        if (n > 0) {
            lastData = now;
            for (size_t i = 0; i < n; ++i)
                if (m_slip.feed(buf[i], frame)) m_grid.handleFrame(frame);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        int sinceStart = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        int sinceData  = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count());
        if (sinceStart >= maxMs) break;
        if (sinceStart >= minMs && sinceData >= settleMs) break;
    }
}

const ScreenGrid& M8Device::read(int settleMs, int maxMs) {
    readInto(120, settleMs, maxMs);
    return m_grid;
}

ScreenId M8Device::screen() {
    return {m_grid.topHeader(), m_grid.canon()};
}

Firmware M8Device::firmware() const {
    return m_grid.firmware();
}

std::optional<FieldRef> M8Device::cursorField() {
    std::string txt = m_grid.cursorMainText();
    // Trim trailing newline.
    while (!txt.empty() && txt.back() == '\n') txt.pop_back();
    if (txt.empty()) return std::nullopt;
    int y = m_grid.cursorRowY();
    if (y < 0) return std::nullopt;
    // Find the x of the first cursor cell on this row (excluding highlights).
    //
    // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, EFFECTS screen): the
    // same ghost-cyan-blank-cell effect fixed in cursorRowY() (see the
    // comment there) also hits this X lookup, and on a DIFFERENT row than
    // the real cursor's -- moving UP from CHO_MOD_DEP to the "MOD TYPE" row
    // left the row BELOW it (the vacated "INPUT EQ" row) with ghost cyan
    // blanks at its leading columns (col 0-7) even though "INPUT EQ" itself
    // had correctly recolored back to normal. cursorRowY() already picks the
    // right ROW via its own non-space guard, but this loop was still
    // scanning that correct row left-to-right and stopping at the first
    // ghost blank, reporting a column far to the left of any real field.
    // Requiring non-space here for the same reason as cursorRowY(): a
    // genuine cursor's leading cell is always real label text, never blank
    // padding.
    int x = -1;
    for (auto& [pos, c] : m_grid.cells) {
        if (m_grid.isCursor(c) && c.ch != ' ' && pos.first == y && pos.second < ScreenGrid::MAIN_X_MAX
            && !m_grid.isInHighlight(pos.second, pos.first)) {
            x = pos.second;
            break;
        }
    }
    return FieldRef{txt, x, y};
}

std::vector<std::pair<int, std::string>> M8Device::rows() {
    return m_grid.mainRows();
}

std::optional<std::string> M8Device::valueOf(const FieldRef& field) {
    return m_grid.valueAt(field.col, field.row);
}

void M8Device::press(uint8_t mask, int holdMs) {
    m_port.sendByte('C');
    m_port.sendByte(mask);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    m_port.sendByte('C');
    m_port.sendByte(0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

void M8Device::chord(std::initializer_list<uint8_t> keys, int holdMs) {
    uint8_t mask = 0;
    for (uint8_t k : keys) mask |= k;
    press(mask, holdMs);
}

void M8Device::playToggle() {
    press(Key::PLAY);
}

void M8Device::keyjazz(uint8_t note, uint8_t vel) {
    m_port.sendByte('K');
    m_port.sendByte(note);
    m_port.sendByte(vel);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    m_port.sendByte('K');
    m_port.sendByte(0xFF);
}

void M8Device::step(int settleMs, int maxMs) {
    read(settleMs, maxMs);
}

void M8Device::readScreen(int settleMs, int maxMs) {
    read(settleMs, maxMs);
}

} // namespace dev
} // namespace m8
