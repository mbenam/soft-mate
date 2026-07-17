// ===========================================================================
// src/tools/main_nav.cpp
//
// Decodes the M8 headless serial *display* stream (the same SLIP-framed draw
// protocol m8c speaks) into a text grid, so the harness can read the on-device
// screen — the foundation for framebuffer-verified file-browser navigation
// (M8_HARDWARE_TEST_SPEC.md §8.2b).
//
//   m8_nav --port COM3 --dump-screen        # decode one frame, print it
//   m8_nav --port COM3 --json screen.json   # + machine-readable cells w/ colors
//
// Phase 1: display decode + screen dump only. Navigation (--load-file) and the
// direction-mask self-test come next, built on the ScreenGrid below.
//
// Serial only. No engine, no SDL, no audio.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>
#include <cctype>

// ---- Win32 serial (same config as main_capture.cpp) -----------------------

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct SerialPort {
    HANDLE h = INVALID_HANDLE_VALUE;

    bool open(const char* port) {
        char path[64];
        std::snprintf(path, sizeof(path), "\\\\.\\%s", port);
        h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
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
            CloseHandle(h); h = INVALID_HANDLE_VALUE; return false;
        }
        // Non-blocking-ish reads: return immediately with whatever is buffered.
        COMMTIMEOUTS t = {};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = 0;
        t.ReadTotalTimeoutConstant = 0;
        SetCommTimeouts(h, &t);
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }
    bool send(const void* data, size_t len) {
        DWORD written = 0;
        if (!WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr)) {
            std::fprintf(stderr, "serial write failed (error %lu)\n", GetLastError());
            return false;
        }
        return written == len;
    }
    bool sendByte(uint8_t b) { return send(&b, 1); }
    // Returns bytes read into buf (0 if none available right now).
    size_t recv(uint8_t* buf, size_t cap) {
        DWORD got = 0;
        if (!ReadFile(h, buf, static_cast<DWORD>(cap), &got, nullptr)) return 0;
        return got;
    }
    void close() {
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    }
    ~SerialPort() { close(); }
};
#else
struct SerialPort {
    bool open(const char*) { std::fprintf(stderr, "serial not implemented on this platform\n"); return false; }
    bool send(const void*, size_t) { return false; }
    bool sendByte(uint8_t) { return false; }
    size_t recv(uint8_t*, size_t) { return 0; }
    void close() {}
};
#endif

// ---- M8 send commands (verified against m8c) ------------------------------

static void m8Enable(SerialPort& sp, bool reset) {
    sp.sendByte('E');
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (reset) sp.sendByte('R');   // reset display -> request a full redraw
}
static void m8Reset(SerialPort& sp)      { sp.sendByte('R'); }
static void m8Disconnect(SerialPort& sp) { sp.sendByte('D'); }

// Press and release a controller mask ('C' <mask>, then 'C' 0x00).
static void m8Press(SerialPort& sp, uint8_t mask, int holdMs) {
    sp.sendByte('C'); sp.sendByte(mask);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    sp.sendByte('C'); sp.sendByte(0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

// ---- SLIP framing (RFC1055, as m8c uses) ----------------------------------

static constexpr uint8_t SLIP_END     = 0xC0;
static constexpr uint8_t SLIP_ESC      = 0xDB;
static constexpr uint8_t SLIP_ESC_END = 0xDC;
static constexpr uint8_t SLIP_ESC_ESC = 0xDD;

struct SlipDecoder {
    std::vector<uint8_t> frame;
    bool inEsc = false;
    // Feeds one byte; returns true and fills `out` when a full frame completes.
    bool feed(uint8_t b, std::vector<uint8_t>& out) {
        if (inEsc) {
            frame.push_back(b == SLIP_ESC_END ? SLIP_END : (b == SLIP_ESC_ESC ? SLIP_ESC : b));
            inEsc = false;
            return false;
        }
        if (b == SLIP_ESC) { inEsc = true; return false; }
        if (b == SLIP_END) {
            if (frame.empty()) return false;   // collapse repeated ENDs
            out = frame;
            frame.clear();
            return true;
        }
        frame.push_back(b);
        return false;
    }
};

// ---- Screen model ---------------------------------------------------------
//
// The M8 sends chars at absolute pixel (x,y). We keep them keyed by pixel
// position (last write wins, matching the device's delta model) and quantise to
// a text grid only when printing, auto-detecting the cell pitch from the data so
// we don't hard-code a font size that differs across M8 hardware/font modes.

struct Cell { uint8_t ch; uint8_t fg[3]; uint8_t bg[3]; };
struct Rect { int x, y, w, h; uint8_t c[3]; };

struct ScreenGrid {
    std::map<std::pair<int,int>, Cell> cells;   // (y,x) -> cell
    std::vector<Rect> highlights;               // non-black fills (selection bars)
    int hwType = -1, fwMajor = 0, fwMinor = 0, fwPatch = 0, fontMode = -1;
    uint8_t lastColor[3] = {0,0,0};
    uint8_t cursorColor[3] = {0,252,248};       // M8 default-theme selection accent (cyan)

    bool isCursor(const Cell& c) const {
        return c.fg[0]==cursorColor[0] && c.fg[1]==cursorColor[1] && c.fg[2]==cursorColor[2];
    }

    // Pixel x below this is the "main" content area (the right panel — track
    // meters, "P" scope label — lives beyond it and carries stray cursor-colour
    // cells we must not mistake for the field/file cursor).
    static constexpr int MAIN_X_MAX = 260;

    // The title: text of the topmost non-empty row (rows above it are blank).
    std::string topHeader() const {
        std::map<int,std::map<int,char>> rows;
        for (auto& [pos,c] : cells) {
            char g = (c.ch>=32 && c.ch<127) ? (char)c.ch : ' ';
            if (g != ' ') rows[pos.first][pos.second] = g;
        }
        for (auto& [y,cols] : rows) {
            std::string s; for (auto& [x,g]:cols) s+=g;
            // trim
            size_t a=s.find_first_not_of(' '), b=s.find_last_not_of(' ');
            if (a!=std::string::npos) return s.substr(a,b-a+1);
        }
        return "";
    }

    // Cursor (selection) text within the main area, joined per row top-to-bottom.
    std::string cursorMainText() const {
        std::map<int,std::map<int,char>> rows;
        for (auto& [pos,c] : cells) {
            if (!isCursor(c) || pos.second >= MAIN_X_MAX) continue;
            char g = (c.ch>=32 && c.ch<127) ? (char)c.ch : ' ';
            rows[pos.first][pos.second] = g;
        }
        std::string out;
        for (auto& [y,cols] : rows) { for (auto& [x,g]:cols) out+=g; out+='\n'; }
        return out;
    }

    // All main-area text rows (y -> trimmed string). Used to read the file list.
    std::vector<std::pair<int,std::string>> mainRows() const {
        std::map<int,std::map<int,char>> rows;
        for (auto& [pos,c] : cells) {
            if (pos.second >= MAIN_X_MAX) continue;
            char g = (c.ch>=32 && c.ch<127) ? (char)c.ch : ' ';
            rows[pos.first][pos.second] = g;
        }
        std::vector<std::pair<int,std::string>> out;
        for (auto& [y,cols] : rows) {
            std::string s; int lastx=-1;
            for (auto& [x,g]:cols){ s+=g; lastx=x; }
            size_t b=s.find_last_not_of(' ');
            out.push_back({y, b==std::string::npos?"":s.substr(0,b+1)});
        }
        return out;
    }

    // Which main row currently holds the cursor (topmost cursor cell's y).
    int cursorRowY() const {
        int best = -1;
        for (auto& [pos,c] : cells)
            if (isCursor(c) && pos.second < MAIN_X_MAX)
                if (best < 0 || pos.first < best) best = pos.first;
        return best;
    }

    void clear() { cells.clear(); highlights.clear(); }

    // Remove char cells whose top-left pixel falls inside [x,x+w) x [y,y+h).
    void eraseRegion(int x, int y, int w, int h) {
        for (auto it = cells.begin(); it != cells.end(); ) {
            int cy = it->first.first, cx = it->first.second;
            if (cx >= x && cx < x + w && cy >= y && cy < y + h) it = cells.erase(it);
            else ++it;
        }
    }

    void handleFrame(const std::vector<uint8_t>& f) {
        if (f.empty()) return;
        switch (f[0]) {
            case 0xFD: {  // draw character: char,xlo,xhi,ylo,yhi,fgRGB,bgRGB
                if (f.size() < 12) return;
                uint8_t ch = f[1];
                int x = f[2] | (f[3] << 8);
                int y = f[4] | (f[5] << 8);
                Cell c;
                c.ch = ch;
                c.fg[0]=f[6];  c.fg[1]=f[7];  c.fg[2]=f[8];
                c.bg[0]=f[9];  c.bg[1]=f[10]; c.bg[2]=f[11];
                cells[{y,x}] = c;
                break;
            }
            case 0xFE: {  // draw rectangle (5/8/9/12): x,y[,w,h][,RGB]
                int x = f[1] | (f[2] << 8);
                int y = f[3] | (f[4] << 8);
                int w = 1, h = 1;
                uint8_t col[3] = { lastColor[0], lastColor[1], lastColor[2] };
                if (f.size() == 8)  { col[0]=f[5]; col[1]=f[6]; col[2]=f[7]; }
                else if (f.size() == 9)  { w=f[5]|(f[6]<<8); h=f[7]|(f[8]<<8); }
                else if (f.size() >= 12) { w=f[5]|(f[6]<<8); h=f[7]|(f[8]<<8);
                                           col[0]=f[9]; col[1]=f[10]; col[2]=f[11]; }
                lastColor[0]=col[0]; lastColor[1]=col[1]; lastColor[2]=col[2];
                const bool black = (col[0]==0 && col[1]==0 && col[2]==0);
                if (w >= 4 && h >= 4) {
                    eraseRegion(x, y, w, h);          // a fill paints over any chars there
                    if (black) {
                        // clearing region: drop highlights it covers (full-screen => all)
                        highlights.erase(std::remove_if(highlights.begin(), highlights.end(),
                            [&](const Rect& r){ return r.x>=x && r.y>=y && r.x<x+w && r.y<y+h; }),
                            highlights.end());
                    } else {
                        highlights.push_back({x, y, w, h, {col[0],col[1],col[2]}});
                    }
                }
                break;
            }
            case 0xFF: {  // system info: hwtype, fw major/minor/patch, font mode
                if (f.size() >= 5) { hwType=f[1]; fwMajor=f[2]; fwMinor=f[3]; fwPatch=f[4]; }
                if (f.size() >= 6) fontMode=f[5];
                break;
            }
            default: break;  // 0xFC scope, 0xFB joypad — ignored for text
        }
    }

    // Detect the smallest positive spacing between distinct coordinate values on
    // one axis (the cell pitch). Falls back to 8 (classic M8 small-font width).
    static int detectPitch(const std::set<int>& coords, int fallback) {
        int best = 1 << 30;
        int prev = -1;
        for (int v : coords) {
            if (prev >= 0) { int d = v - prev; if (d > 0 && d < best) best = d; }
            prev = v;
        }
        return (best == (1 << 30)) ? fallback : best;
    }

    void printText(FILE* out) const {
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
        // Rows covered by a highlight fill (selection bar) or containing cursor cells.
        std::set<int> hlRows;
        for (auto& r : highlights) {
            int rowLo = (r.y - minY) / ch, rowHi = (r.y + r.h - 1 - minY) / ch;
            for (int rr = std::max(0, rowLo); rr <= std::min(maxRow, rowHi); ++rr) hlRows.insert(rr);
        }
        // Cursor cells (selection-accent fg): collect their text per row.
        std::map<int,std::string> cursorText;
        for (auto& [pos, c] : cells) {
            if (!isCursor(c)) continue;
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

    void printJson(const std::string& path) const {
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
};

// Drains the serial stream into `grid` (accumulating deltas) until `settleMs` of
// quiet after an initial `minMs`, up to `maxMs` total — so we capture a complete
// redraw/transition without guessing a fixed delay.
static void readInto(SerialPort& sp, SlipDecoder& slip, ScreenGrid& grid,
                     int minMs, int settleMs, int maxMs) {
    uint8_t buf[4096];
    std::vector<uint8_t> frame;
    auto start = std::chrono::steady_clock::now();
    auto lastData = start;
    for (;;) {
        size_t n = sp.recv(buf, sizeof(buf));
        auto now = std::chrono::steady_clock::now();
        if (n > 0) {
            lastData = now;
            for (size_t i = 0; i < n; ++i)
                if (slip.feed(buf[i], frame)) grid.handleFrame(frame);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        int sinceStart = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        int sinceData  = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count());
        if (sinceStart >= maxMs) break;
        if (sinceStart >= minMs && sinceData >= settleMs) break;
    }
}

// ---- M8 controller masks (m8c mapping; PLAY=0x08 hardware-verified) --------
namespace Key {
    constexpr uint8_t LEFT=0x80, UP=0x40, DOWN=0x20, SHIFT=0x10,
                      PLAY=0x08, RIGHT=0x04, OPT=0x02, EDIT=0x01;
}

static std::string upper(std::string s){ for(char&c:s) c=(char)std::toupper((unsigned char)c); return s; }
static std::string alnum(const std::string& s){ std::string o; for(char c:s) if(std::isalnum((unsigned char)c)) o+=(char)std::toupper((unsigned char)c); return o; }

// Read a settled screen after an optional press.
static void step(SerialPort& sp, SlipDecoder& slip, ScreenGrid& g, int settleMs, int maxMs) {
    readInto(sp, slip, g, 120, settleMs, maxMs);
}

// PROJECT-screen field order (top to bottom), matched by a keyword substring of
// the cursor row text. Lets us steer to a field by index instead of blind counts.
static const std::vector<std::string> kProjectFields = {
    "TEMPO","TRANSPOSE","GROOVE","SCALE","QUANTIZ","MIDI","NAME",
    "PROJECT","EXPORT","CLEAR","INST","TIME","SYSTEM"
};
static int fieldIndex(const std::string& cursorTxt) {
    std::string u = upper(cursorTxt);
    for (size_t i = 0; i < kProjectFields.size(); ++i)
        if (u.find(kProjectFields[i]) != std::string::npos) return (int)i;
    return -1;
}

// Closed-loop: drive SONG -> PROJECT -> LOAD -> browser -> select `target` -> load.
// Returns 0 success, non-zero with a printed reason.
static int navLoadFile(SerialPort& sp, SlipDecoder& slip, ScreenGrid& grid,
                       const std::string& target, int holdMs, int settleMs, int maxMs) {
    const std::string want = alnum(target);   // e.g. PROBESHAPE00M8S
    auto isModal = [&]{ std::string h=upper(grid.topHeader());
        return h.find("LOSE CHANGES")!=std::string::npos || h.find("SONG?")!=std::string::npos
            || h.find("OVERWRITE")!=std::string::npos; };

    // "LOAD PROJECT" (browser) must be distinguished from "PROJECT" (settings) —
    // both contain the word PROJECT, and confusing them sends the field-steering
    // loop wandering through the file list.
    // topHeader() joins the title row's glyphs and drops spaces, so match the
    // space-free forms: browser = "LOADPROJECT", settings = "PROJECT" (no LOAD).
    auto header       = [&]{ return upper(grid.topHeader()); };
    auto inBrowser    = [&]{ return header().find("LOADPROJECT") != std::string::npos; };
    auto onProjSettings = [&]{ std::string h=header();
        return h.find("PROJECT")!=std::string::npos && h.find("LOAD")==std::string::npos; };

    std::printf("nav: start header=\"%s\"\n", grid.topHeader().c_str());

    // 1-3) Reach the LOAD PROJECT browser, unless we're already in it (the device
    // keeps whatever screen it was left on — including a browser from a prior run).
    if (!inBrowser()) {
        // Normalise to the PROJECT settings screen: cancel any stray modal (OPT),
        // else climb (SHIFT+UP), re-checking the header each time.
        bool onProject = false;
        for (int t = 0; t < 8; ++t) {
            if (onProjSettings()) { onProject = true; break; }
            if (isModal()) m8Press(sp, Key::OPT, holdMs);
            else           m8Press(sp, Key::SHIFT|Key::UP, holdMs);
            step(sp, slip, grid, settleMs, maxMs);
        }
        if (!onProject) {
            std::printf("nav: could not reach PROJECT (header=\"%s\")\n", grid.topHeader().c_str()); return 10;
        }

        // Steer the cursor to the PROJECT (LOAD) field by index.
        int projIdx = fieldIndex("PROJECT");
        bool onLoad = false;
        for (int i = 0; i < 30; ++i) {
            std::string ct = grid.cursorMainText();
            int idx = fieldIndex(ct);
            if (idx == projIdx) {
                if (alnum(ct).find("LOAD") != std::string::npos) { onLoad = true; break; }
                m8Press(sp, Key::LEFT, holdMs);                        // nudge to leftmost (LOAD)
            } else if (idx < 0 || idx < projIdx) {
                m8Press(sp, Key::DOWN, holdMs);
            } else {
                m8Press(sp, Key::UP, holdMs);
            }
            step(sp, slip, grid, settleMs, maxMs);
        }
        if (!onLoad) {
            std::printf("nav: could not reach PROJECT/LOAD (cursor=\"%s\")\n", grid.cursorMainText().c_str()); return 11;
        }
        std::printf("nav: cursor on PROJECT/LOAD, opening browser...\n");

        // EDIT -> (optional "LOSE CHANGES?" confirm -> OK) -> file browser
        m8Press(sp, Key::EDIT, holdMs); step(sp, slip, grid, settleMs, maxMs);
        if (isModal()) {
            std::printf("nav: confirming \"%s\" (OK = discard in-memory edits)\n", grid.topHeader().c_str());
            m8Press(sp, Key::EDIT, holdMs); step(sp, slip, grid, settleMs, maxMs);  // cursor is on OK
        }
    } else {
        std::printf("nav: already in LOAD PROJECT browser\n");
    }

    if (!inBrowser()) {
        std::printf("nav: expected LOAD PROJECT browser, got \"%s\"\n", grid.topHeader().c_str()); return 12;
    }

    // 4) select target file. Read the on-screen list and steer by row index so an
    // overshoot (key-repeat) self-corrects instead of running away.
    auto findTargetRow = [&](std::vector<std::pair<int,std::string>>& rows)->int{
        for (auto& [y,s] : rows) if (alnum(s).find(want) != std::string::npos) return y;
        return -1;
    };
    for (int i = 0; i < 40; ++i) {
        std::string sel = grid.cursorMainText();
        if (alnum(sel).find(want) != std::string::npos) {
            std::printf("nav: selected \"%s\", loading...\n", sel.c_str());
            m8Press(sp, Key::EDIT, holdMs); step(sp, slip, grid, settleMs, maxMs);
            if (isModal()) { m8Press(sp, Key::EDIT, holdMs); step(sp, slip, grid, settleMs, maxMs); }
            // Leave the device on the SONG screen so a subsequent PLAY starts from
            // row 0 (PLAY off SONG starts mid-song / silent). The screen grid is 2D:
            // top-row screens (PROJECT/settings) are above; SONG is the leftmost of
            // the SONG/CHAIN/PHRASE... row. Drop down off any top screen, then walk
            // left to SONG, verifying the header.
            for (int t = 0; t < 8; ++t) {
                std::string h = upper(grid.topHeader());
                if (h.rfind("SONG", 0) == 0) break;                 // header starts with SONG
                if (h.find("PROJECT")!=std::string::npos || h.find("SETTING")!=std::string::npos ||
                    h.find("MIDI")!=std::string::npos    || h.find("MIXER")!=std::string::npos)
                    m8Press(sp, Key::SHIFT|Key::DOWN, holdMs);
                else
                    m8Press(sp, Key::SHIFT|Key::LEFT, holdMs);      // CHAIN/PHRASE/... are right of SONG
                step(sp, slip, grid, settleMs, maxMs);
            }
            std::printf("nav: post-load header=\"%s\"\n", grid.topHeader().c_str());
            return 0;
        }
        auto rows = grid.mainRows();
        int targetY = findTargetRow(rows);
        int curY = grid.cursorRowY();
        // Not on screen: the list is longer than the viewport — scroll down to find
        // it (the M8 auto-scrolls to keep the cursor visible). The 40-iteration cap
        // bounds the search over the whole library.
        if (targetY < 0)         m8Press(sp, Key::DOWN, holdMs);
        else if (curY < 0)       m8Press(sp, Key::DOWN, holdMs); // no cursor seen: nudge in
        else if (targetY > curY) m8Press(sp, Key::DOWN, holdMs);
        else if (targetY < curY) m8Press(sp, Key::UP, holdMs);
        step(sp, slip, grid, settleMs, maxMs);
    }
    std::printf("nav: gave up selecting \"%s\"\n", target.c_str());
    return 14;
}

// ---- main -----------------------------------------------------------------

int main(int argc, char** argv) {
    std::string port, jsonPath, keysArg, loadFile;
    bool dumpScreen = false, noReset = false;
    int maxMs = 2000, settleMs = 250, minMs = 700;
    int holdMs = 40, gapMs = 120;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--port")        port = next();
        else if (a == "--dump-screen") dumpScreen = true;
        else if (a == "--json")        jsonPath = next();
        else if (a == "--keys")        keysArg = next();     // csv of hex masks, dump after each
        else if (a == "--load-file")   loadFile = next();    // closed-loop: load a project by name
        else if (a == "--hold-ms")     holdMs = std::atoi(next().c_str());
        else if (a == "--gap-ms")      gapMs = std::atoi(next().c_str());
        else if (a == "--no-reset")    noReset = true;       // keep current view (skip 'R')
        else if (a == "--max-ms")      maxMs = std::atoi(next().c_str());
        else if (a == "--settle-ms")   settleMs = std::atoi(next().c_str());
        else if (a == "--min-ms")      minMs = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }
    // For closed-loop nav, default to short key holds so the M8's ~150ms key
    // auto-repeat never fires and a press moves exactly one field/row.
    if (!loadFile.empty() && holdMs > 20) holdMs = 15;
    if (port.empty()) {
        std::fprintf(stderr,
            "usage: m8_nav --port COM3 [--dump-screen] [--json out.json]\n"
            "              [--keys 0x40,0x40,0x08] [--hold-ms 40] [--no-reset]\n");
        return 1;
    }
    if (!dumpScreen && jsonPath.empty() && keysArg.empty()) dumpScreen = true;  // default

    // Parse the key sequence up front so a typo fails before touching the device.
    std::vector<uint8_t> keys;
    if (!keysArg.empty()) {
        size_t pos = 0;
        while (pos < keysArg.size()) {
            size_t comma = keysArg.find(',', pos);
            std::string tok = keysArg.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!tok.empty()) keys.push_back(static_cast<uint8_t>(std::strtol(tok.c_str(), nullptr, 0)));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }

    SerialPort sp;
    if (!sp.open(port.c_str())) return 2;
    std::printf("serial: %s opened @115200\n", port.c_str());

    SlipDecoder slip;
    ScreenGrid grid;
    m8Enable(sp, !noReset);
    std::printf("serial: display %s, reading frame...\n", noReset ? "enabled" : "enabled + reset");
    readInto(sp, slip, grid, minMs, settleMs, maxMs);
    std::printf("device: hw_type=%d  firmware=%d.%d.%d  font_mode=%d\n",
                grid.hwType, grid.fwMajor, grid.fwMinor, grid.fwPatch, grid.fontMode);
    if (keys.empty() && loadFile.empty() && dumpScreen) grid.printText(stdout);

    int navRc = 0;
    if (!loadFile.empty()) {
        navRc = navLoadFile(sp, slip, grid, loadFile, holdMs, settleMs, maxMs);
        std::printf("nav: %s (rc=%d), final header=\"%s\"\n",
                    navRc == 0 ? "LOADED" : "FAILED", navRc, grid.topHeader().c_str());
        if (dumpScreen) grid.printText(stdout);
    }

    // Drive the key sequence, dumping the resulting screen after each press so a
    // whole navigation trace comes back in one invocation.
    for (size_t k = 0; k < keys.size(); ++k) {
        std::printf("\n=== press 0x%02X (%zu/%zu) ===\n", keys[k], k + 1, keys.size());
        m8Press(sp, keys[k], holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
        readInto(sp, slip, grid, 150, settleMs, maxMs);
        grid.printText(stdout);
    }

    m8Disconnect(sp);
    sp.close();

    if (!jsonPath.empty()) { grid.printJson(jsonPath); std::printf("wrote %s\n", jsonPath.c_str()); }
    if (grid.cells.empty()) {
        std::fprintf(stderr, "WARNING: no characters decoded — is the device connected and streaming?\n");
        return 3;
    }
    return navRc;   // 0 unless --load-file failed
}
