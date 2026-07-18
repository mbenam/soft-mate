#include "Renderer.h"
#include "font.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

static inline uint32_t sdlColorToUint32(SDL_Color c) {
    return (uint32_t(c.r) << 24) | (uint32_t(c.g) << 16) | (uint32_t(c.b) << 8) | uint32_t(c.a);
}

static inline SDL_Color uint32ToSdlColor(uint32_t v) {
    return SDL_Color{
        uint8_t((v >> 24) & 0xFF),
        uint8_t((v >> 16) & 0xFF),
        uint8_t((v >> 8) & 0xFF),
        uint8_t(v & 0xFF)
    };
}

// Highlight colour used for cursor brackets across the entire UI.
static constexpr uint32_t kHighlightColor = 0x00FFFFFF; // {0,255,255,255}

Renderer::Renderer() {}

Renderer::~Renderer() {
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool Renderer::init(int logicalWidth, int logicalHeight, int scale, bool hidden) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
        return false;
    }

    Uint32 flags = SDL_WINDOW_RESIZABLE;
    if (hidden) flags |= SDL_WINDOW_HIDDEN;

    m_window = SDL_CreateWindow("M8 Clone", logicalWidth * scale, logicalHeight * scale, flags);
    if (!m_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << "\n";
        return false;
    }

    // Headless (script/automation) mode never presents to a display, so force
    // the software renderer instead of the default GPU-accelerated backend.
    // Discovered via M8_APP_AUTOMATION_SPEC.md Tier 1: running m8_clone --headless
    // back-to-back in a tight loop (13 scripts, one process each) intermittently
    // crashed with STATUS_ACCESS_VIOLATION on a *different* script each run --
    // a GPU-driver race from creating/tearing down a real D3D/Vulkan renderer
    // once per process in rapid succession. The software renderer sidesteps the
    // GPU driver entirely, which is also strictly correct for a hidden window
    // that's never actually presented to screen.
    m_renderer = SDL_CreateRenderer(m_window, hidden ? SDL_SOFTWARE_RENDERER : nullptr);
    if (!m_renderer) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << "\n";
        return false;
    }
    
    SDL_SetRenderLogicalPresentation(m_renderer, logicalWidth, logicalHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    return true;
}

void Renderer::resetVram() {
    std::memset(m_vram, 0, sizeof(m_vram));
    // Reset ch to space (memset sets to 0 which IS space)
    for (int y = 0; y < kGridH; ++y)
        for (int x = 0; x < kGridW; ++x)
            m_vram[y][x].ch = ' ';
}

void Renderer::clear(SDL_Color color) {
    resetVram();
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

void Renderer::present() {
    SDL_RenderPresent(m_renderer);
}

void Renderer::drawChar(char c, int gridX, int gridY, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    
    // Find character in font array
    int i = 0;
    bool found = false;
    while (font[i].letter != 0) {
        if (font[i].letter == c) {
            found = true;
            break;
        }
        i++;
    }
    if (!found) return;

    float px = (float)(gridX * m_cellWidth) + 1.0f;
    float py = (float)(gridY * m_cellHeight) + 0.0f;

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (font[i].code[row][col] == '#') {
                SDL_RenderPoint(m_renderer, px + col, py + row);
            }
        }
    }

    // Shadow grid: stamp ch + color
    if (gridX >= 0 && gridX < kGridW && gridY >= 0 && gridY < kGridH) {
        m_vram[gridY][gridX].ch = c;
        m_vram[gridY][gridX].color = sdlColorToUint32(color);
        m_vram[gridY][gridX].writeCount++;
    }
}

void Renderer::drawString(const std::string& str, int gridX, int gridY, SDL_Color color) {
    for (size_t i = 0; i < str.length(); ++i) {
        drawChar(str[i], gridX + i, gridY, color);
    }
}

void Renderer::drawRect(int gridX, int gridY, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_FRect rect = { 
        (float)(gridX * m_cellWidth), 
        (float)(gridY * m_cellHeight), 
        (float)(w * m_cellWidth), 
        (float)(h * m_cellHeight) 
    };
    SDL_RenderRect(m_renderer, &rect);
}

void Renderer::drawRectPixel(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderRect(m_renderer, &rect);
}

void Renderer::fillRectPixel(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(m_renderer, &rect);

    // Shadow grid: stamp bg on covered cells, and slider for horizontal fills
    uint32_t bgVal = sdlColorToUint32(color);
    int startCellX = x / m_cellWidth;
    int endCellX = (x + w + m_cellWidth - 1) / m_cellWidth;
    int startCellY = y / m_cellHeight;
    int endCellY = (y + h + m_cellHeight - 1) / m_cellHeight;

    for (int cy = startCellY; cy < endCellY; ++cy) {
        for (int cx = startCellX; cx < endCellX; ++cx) {
            if (cx < 0 || cx >= kGridW || cy < 0 || cy >= kGridH) continue;
            m_vram[cy][cx].bg = bgVal;
            m_vram[cy][cx].writeCount++;

            // For horizontal fills (exactly one cell tall), compute slider fill
            if (h == m_cellHeight) {
                int cellPxStart = cx * m_cellWidth;
                int cellPxEnd   = cellPxStart + m_cellWidth;
                int fillStart   = std::max(x, cellPxStart);
                int fillEnd     = std::min(x + w, cellPxEnd);
                if (fillEnd > fillStart)
                    m_vram[cy][cx].slider = uint8_t((fillEnd - fillStart) * 8 / m_cellWidth);
                else
                    m_vram[cy][cx].slider = 0;
            }
        }
    }
}

void Renderer::drawLinePixel(int x1, int y1, int x2, int y2, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(m_renderer, (float)x1, (float)y1, (float)x2, (float)y2);
}

void Renderer::drawBracket(int cx, int y, int cw, SDL_Color color) {
    int bpx = cx * 8 - 2;
    int bpy = y * 8 - 1;
    int bpw = cw * 8 + 3; 
    int bph = 8; 
    int len = 2;
    drawLinePixel(bpx, bpy, bpx + len, bpy, color);
    drawLinePixel(bpx, bpy, bpx, bpy + len, color);
    drawLinePixel(bpx + bpw, bpy, bpx + bpw - len, bpy, color);
    drawLinePixel(bpx + bpw, bpy, bpx + bpw, bpy + len, color);
    drawLinePixel(bpx, bpy + bph, bpx + len, bpy + bph, color);
    drawLinePixel(bpx, bpy + bph, bpx, bpy + bph - len, color);
    drawLinePixel(bpx + bpw, bpy + bph, bpx + bpw - len, bpy + bph, color);
    drawLinePixel(bpx + bpw, bpy + bph, bpx + bpw, bpy + bph - len, color);

    // Shadow grid: bracket covers logical cells [cx, cx+cw) at row y
    for (int i = 0; i < cw; ++i) {
        int bx = cx + i;
        if (bx >= 0 && bx < kGridW && y >= 0 && y < kGridH) {
            m_vram[y][bx].bracket = true;
            m_vram[y][bx].bg = sdlColorToUint32(color);
            // Note: bracket does NOT increment writeCount — it's a visual
            // indicator, not a data field. Overlaps between grid data and
            // cursor bracket are intentional, not layout collisions.
        }
    }
}

bool Renderer::hasOverlap() const {
    for (int y = 0; y < kGridH; ++y)
        for (int x = 0; x < kGridW; ++x)
            if (m_vram[y][x].writeCount > 1) return true;
    return false;
}

// ─── Tier 5: golden snapshot testing ────────────────────────────────────────
// Format: header line, then one line per cell (row-major, all kGridH*kGridW
// cells unconditionally -- simpler and more robust than only emitting
// non-blank cells, at the cost of a larger file):
//   row col ch(as 2-digit hex of the byte) fgColorHex bgColorHex slider bracket
// ch is hex-encoded (not the raw char) so control/space bytes round-trip
// through whitespace-delimited parsing without ambiguity.

void Renderer::writeGolden(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "# m8-sdl3 golden snapshot v1\n";
    f << "# row col ch_hex fg bg slider bracket\n";
    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            const VirtualCell& c = m_vram[y][x];
            f << y << ' ' << x << ' '
              << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
              << static_cast<unsigned>(static_cast<unsigned char>(c.ch)) << ' '
              << std::setw(8) << c.color << ' '
              << std::setw(8) << c.bg << std::dec << std::nouppercase << ' '
              << static_cast<int>(c.slider) << ' '
              << (c.bracket ? 1 : 0) << '\n';
        }
    }
}

bool Renderer::compareGolden(const std::string& path, std::string& mismatchDetail) const {
    std::ifstream f(path);
    if (!f.is_open()) {
        mismatchDetail = "golden file not found: " + path;
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(f, line)) {
        ++lineNum;
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        int row = -1, col = -1;
        std::string chHex, fgHex, bgHex;
        int slider = -1, bracket = -1;
        if (!(iss >> row >> col >> chHex >> fgHex >> bgHex >> slider >> bracket)) {
            mismatchDetail = "malformed golden line " + std::to_string(lineNum) + " in " + path;
            return false;
        }
        if (row < 0 || row >= kGridH || col < 0 || col >= kGridW) {
            mismatchDetail = "golden line " + std::to_string(lineNum) + " has out-of-range row/col";
            return false;
        }

        unsigned char expectedCh = static_cast<unsigned char>(std::stoul(chHex, nullptr, 16));
        uint32_t expectedFg = static_cast<uint32_t>(std::stoul(fgHex, nullptr, 16));
        uint32_t expectedBg = static_cast<uint32_t>(std::stoul(bgHex, nullptr, 16));

        const VirtualCell& actual = m_vram[row][col];
        bool chMismatch = static_cast<unsigned char>(actual.ch) != expectedCh;
        bool fgMismatch = actual.color != expectedFg;
        bool bgMismatch = actual.bg != expectedBg;
        bool sliderMismatch = static_cast<int>(actual.slider) != slider;
        bool bracketMismatch = (actual.bracket ? 1 : 0) != bracket;

        if (chMismatch || fgMismatch || bgMismatch || sliderMismatch || bracketMismatch) {
            std::ostringstream detail;
            detail << "cell [row=" << row << " col=" << col << "] mismatch:";
            if (chMismatch) detail << " ch expected=0x" << std::hex << (unsigned)expectedCh
                                    << " actual=0x" << (unsigned)(unsigned char)actual.ch << std::dec
                                    << " ('" << (char)expectedCh << "' vs '" << actual.ch << "')";
            if (fgMismatch) detail << " fg expected=" << std::hex << std::setw(8) << std::setfill('0')
                                    << expectedFg << " actual=" << actual.color << std::dec;
            if (bgMismatch) detail << " bg expected=" << std::hex << std::setw(8) << std::setfill('0')
                                    << expectedBg << " actual=" << actual.bg << std::dec;
            if (sliderMismatch) detail << " slider expected=" << slider
                                        << " actual=" << (int)actual.slider;
            if (bracketMismatch) detail << " bracket expected=" << bracket
                                         << " actual=" << (actual.bracket ? 1 : 0);
            mismatchDetail = detail.str();
            return false;
        }
    }
    return true;
}

void Renderer::setPlayheads(const StoredPlayhead* ph, int count) {
    m_playheadCount = std::min(count, 8);
    for (int i = 0; i < m_playheadCount; ++i)
        m_playheads[i] = ph[i];
}

void Renderer::dumpScreenText(const char* path) const {
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) { std::cerr << "dumpScreenText: cannot open " << path << "\n"; return; }
    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            fputc(m_vram[y][x].ch, f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

static void writeHexChar(FILE* f, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";
    fputc(hex[v >> 4], f);
    fputc(hex[v & 0xF], f);
}

static void writeHexUint32(FILE* f, uint32_t v) {
    writeHexChar(f, uint8_t((v >> 24) & 0xFF));
    writeHexChar(f, uint8_t((v >> 16) & 0xFF));
    writeHexChar(f, uint8_t((v >> 8) & 0xFF));
    writeHexChar(f, uint8_t(v & 0xFF));
}

void Renderer::dumpJson(const char* path, const char* screenName, int bpm) const {
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) { std::cerr << "dumpJson: cannot open " << path << "\n"; return; }

    fprintf(f, "{\n");
    fprintf(f, "  \"screen\": \"%s\",\n", screenName);
    fprintf(f, "  \"bpm\": %d,\n", bpm);

    // vram: array of 30 strings
    fprintf(f, "  \"vram\": [\n");
    for (int y = 0; y < kGridH; ++y) {
        fprintf(f, "    \"");
        for (int x = 0; x < kGridW; ++x) {
            char ch = m_vram[y][x].ch;
            if (ch == '"' || ch == '\\') fputc('\\', f);
            if (ch >= 32 && ch < 127) fputc(ch, f);
            else fputc(' ', f);
        }
        fprintf(f, "\"%s\n", y < kGridH - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    // colors: 2D array of hex strings
    fprintf(f, "  \"colors\": [\n");
    for (int y = 0; y < kGridH; ++y) {
        fprintf(f, "    [");
        for (int x = 0; x < kGridW; ++x) {
            fprintf(f, "\"");
            writeHexUint32(f, m_vram[y][x].color);
            fprintf(f, "\"%s", x < kGridW - 1 ? ", " : "");
        }
        fprintf(f, "]%s\n", y < kGridH - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    // Cursor: derived from cells whose bg is the highlight colour
    // Find first cell with highlight bg, then compute span
    int curRow = -1, curCol = -1, curWidth = 0;
    for (int y = 0; y < kGridH && curRow == -1; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            if (m_vram[y][x].bg == kHighlightColor) {
                curRow = y;
                curCol = x;
                // Find width of contiguous highlight cells in this row
                curWidth = 0;
                while (x + curWidth < kGridW && m_vram[y][x + curWidth].bg == kHighlightColor)
                    ++curWidth;
                break;
            }
        }
    }
    if (curRow >= 0)
        fprintf(f, "  \"cursor\": { \"row\": %d, \"col\": %d, \"width\": %d },\n", curRow, curCol, curWidth);
    else
        fprintf(f, "  \"cursor\": null,\n");

    // Brackets: cells where bracket == true, grouped by row
    fprintf(f, "  \"brackets\": [\n");
    bool firstBracket = true;
    for (int y = 0; y < kGridH; ++y) {
        int x = 0;
        while (x < kGridW) {
            while (x < kGridW && !m_vram[y][x].bracket) ++x;
            if (x >= kGridW) break;
            int startCol = x;
            while (x < kGridW && m_vram[y][x].bracket) ++x;
            int width = x - startCol;
            if (!firstBracket) fprintf(f, ",\n");
            firstBracket = false;
            fprintf(f, "    {\"row\":%d,\"col\":%d,\"width\":%d}", y, startCol, width);
        }
    }
    fprintf(f, "\n  ],\n");

    // Sliders: cells where slider > 0
    fprintf(f, "  \"sliders\": [\n");
    bool firstSlider = true;
    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            if (m_vram[y][x].slider > 0) {
                if (!firstSlider) fprintf(f, ",\n");
                firstSlider = false;
                fprintf(f, "    {\"row\":%d,\"col\":%d,\"fill\":%d}", y, x, m_vram[y][x].slider);
            }
        }
    }
    fprintf(f, "\n  ],\n");

    // Playheads
    fprintf(f, "  \"playheads\": [\n");
    for (int i = 0; i < m_playheadCount; ++i) {
        const char* mode = "NONE";
        switch (m_playheads[i].playMode) {
            case 1: mode = "PHRASE"; break;
            case 2: mode = "CHAIN"; break;
            case 3: mode = "SONG"; break;
        }
        fprintf(f, "    {\"track\":%d,\"row\":%d,\"mode\":\"%s\"}%s\n",
                m_playheads[i].track, m_playheads[i].phraseRow, mode,
                i < m_playheadCount - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    // Overlay (null for now)
    fprintf(f, "  \"overlay\": null\n");
    fprintf(f, "}\n");

    fclose(f);
}

void Renderer::drawString(const char* str, int gridX, int gridY, SDL_Color color) {
    for (size_t i = 0; str[i] != '\0'; ++i) {
        drawChar(str[i], gridX + i, gridY, color);
    }
}
