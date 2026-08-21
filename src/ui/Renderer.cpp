#include "Renderer.h"
#include "font.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <vector>
#include <array>

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

void Renderer::setOrigin(int cellX, int cellY) {
    m_originX = cellX;
    m_originY = cellY;
}

void Renderer::drawChar(char c, int gridX, int gridY, SDL_Color color) {
    drawCharAbs(c, gridX + m_originX, gridY + m_originY, color);
}

void Renderer::drawCharAbs(char c, int gridX, int gridY, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

    // FONT OPTIONS, applied to the glyph lookup only. Only letters are folded:
    // the meter and curve glyphs live at 0x01..0x0E and must reach the table
    // untouched, and toupper/tolower on a negative char is undefined, hence the
    // unsigned cast.
    //
    // The shadow grid below deliberately stores the ORIGINAL character, not the
    // folded one. The setting is cosmetic, so letting it reach the grid would
    // make every dump, golden, capture and `assert_screen contains` swing on a
    // font preference -- and would break `goto`, which verifies its landing by
    // matching the header text.
    char glyph = c;
    unsigned char uc = (unsigned char)c;
    if (uc >= 'a' && uc <= 'z' && m_fontUppercase)  glyph = char(uc - 'a' + 'A');
    else if (uc >= 'A' && uc <= 'Z' && !m_fontUppercase) glyph = char(uc - 'A' + 'a');

    // Find character in font array
    int i = 0;
    bool found = false;
    while (font[i].letter != 0) {
        if (font[i].letter == glyph) {
            found = true;
            break;
        }
        i++;
    }
    if (!found) return;

    float px = (float)(gridX * m_cellWidth) + 1.0f;
    float py = (float)(gridY * m_cellHeight) + 0.0f;

    for (int row = 0; row < 8; ++row) {
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
        drawChar(str[i], gridX + (int)i, gridY, color);
    }
}

void Renderer::drawStringAbs(const std::string& str, int gridX, int gridY, SDL_Color color) {
    for (size_t i = 0; i < str.length(); ++i) {
        drawCharAbs(str[i], gridX + (int)i, gridY, color);
    }
}

void Renderer::drawRect(int gridX, int gridY, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    gridX += m_originX;
    gridY += m_originY;
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
    x += m_originX * m_cellWidth;
    y += m_originY * m_cellHeight;
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderRect(m_renderer, &rect);
}

void Renderer::fillRectPixel(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    x += m_originX * m_cellWidth;
    y += m_originY * m_cellHeight;
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
    const int ox = m_originX * m_cellWidth;
    const int oy = m_originY * m_cellHeight;
    drawLinePixelRaw(x1 + ox, y1 + oy, x2 + ox, y2 + oy, color);
}

void Renderer::drawLinePixelRaw(int x1, int y1, int x2, int y2, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(m_renderer, (float)x1, (float)y1, (float)x2, (float)y2);
}

void Renderer::drawBracket(int cx, int y, int cw, SDL_Color color) {
    cx += m_originX;
    y  += m_originY;
    int bpx = cx * 8 - 2;
    int bpy = y * 8 - 1;
    int bpw = cw * 8 + 3; 
    int bph = 8; 
    int len = 2;
    drawLinePixelRaw(bpx, bpy, bpx + len, bpy, color);
    drawLinePixelRaw(bpx, bpy, bpx, bpy + len, color);
    drawLinePixelRaw(bpx + bpw, bpy, bpx + bpw - len, bpy, color);
    drawLinePixelRaw(bpx + bpw, bpy, bpx + bpw, bpy + len, color);
    drawLinePixelRaw(bpx, bpy + bph, bpx + len, bpy + bph, color);
    drawLinePixelRaw(bpx, bpy + bph, bpx, bpy + bph - len, color);
    drawLinePixelRaw(bpx + bpw, bpy + bph, bpx + bpw - len, bpy + bph, color);
    drawLinePixelRaw(bpx + bpw, bpy + bph, bpx + bpw, bpy + bph - len, color);

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
            char ch = m_vram[y][x].ch;
            // Meter fills live at 0x01-0x07 (MIXER_SPEC.md §5.1) and curve
            // dashes at 0x08-0x0E (EQ_SPEC.md §6). Writing them raw would put
            // control codes in every dump -- 0x0A would literally break the
            // dump into extra lines. Mapped, a meter reads as a column of 1-7
            // and a curve as a row of a-g, both assertable like any other text.
            if (ch >= 0x01 && ch <= 0x07)      ch = static_cast<char>('0' + ch);
            else if (ch >= 0x08 && ch <= 0x0E) ch = static_cast<char>('a' + (ch - 0x08));
            fputc(ch, f);
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

void Renderer::writeUiCapture(const std::string& path, const std::string& screenName,
                               const std::string& firmware,
                               const std::string& themeId) const {
    // ---- Build palette (insertion order, then sorted canonically) ----------
    std::vector<std::array<uint8_t, 3>> palette;
    auto addColor = [&](uint32_t rgba) -> int {
        uint8_t r = (rgba >> 24) & 0xFF;
        uint8_t g = (rgba >> 16) & 0xFF;
        uint8_t b = (rgba >>  8) & 0xFF;
        std::array<uint8_t, 3> col = {r, g, b};
        for (int i = 0; i < (int)palette.size(); ++i)
            if (palette[i] == col) return i;
        palette.push_back(col);
        return (int)palette.size() - 1;
    };

    // ---- Build cells list ------------------------------------------------
    // Skip cells that were never drawn to. writeCount tracks how many times
    // each cell was written this frame (by drawChar or fillRectPixel); 0
    // means untouched. This matches the device convention: it only emits
    // cells that received a draw command. (drawBracket sets bg but does not
    // increment writeCount — cursor chrome is emitted as rects, not cell
    // background, so bracket-only cells are correctly skipped here.)
    struct CapCell { int col, row; char ch; int fg, bg; };
    std::vector<CapCell> cells;
    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            const VirtualCell& vc = m_vram[y][x];
            if (vc.writeCount == 0) continue;
            int fg = addColor(vc.color);
            int bg = addColor(vc.bg);
            cells.push_back({x, y, vc.ch, fg, bg});
        }
    }

    // Sort palette canonically (r, g, b) and remap style ids.
    std::vector<std::array<uint8_t, 3>> sortedPal = palette;
    std::sort(sortedPal.begin(), sortedPal.end());
    sortedPal.erase(std::unique(sortedPal.begin(), sortedPal.end()), sortedPal.end());
    auto remapStyle = [&](int raw) -> int {
        if (raw < 0 || raw >= (int)palette.size()) return -1;
        const auto& col = palette[raw];
        for (int i = 0; i < (int)sortedPal.size(); ++i)
            if (sortedPal[i] == col) return i;
        return -1;
    };

    // ---- Serialize -------------------------------------------------------
    std::ofstream out(path);
    if (!out) { std::cerr << "writeUiCapture: cannot open " << path << "\n"; return; }

    out << "{\n";
    out << "  \"screen\": \"" << screenName << "\",\n";
    out << "  \"firmware\": \"" << firmware << "\",\n";
    out << "  \"font_mode\": 0,\n";
    out << "  \"pitch_x\": " << m_cellWidth << ",\n";
    out << "  \"pitch_y\": " << m_cellHeight << ",\n";
    out << "  \"settled\": true,\n";
    out << "  \"theme_id\": \"" << themeId << "\",\n";

    out << "  \"palette\": [\n";
    for (size_t i = 0; i < sortedPal.size(); ++i) {
        const auto& col = sortedPal[i];
        out << "    [" << (int)col[0] << ", " << (int)col[1] << ", " << (int)col[2] << "]";
        out << (i + 1 < sortedPal.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"cells\": [\n";
    for (size_t i = 0; i < cells.size(); ++i) {
        const auto& cl = cells[i];
        // Escape exactly as the device side does (m8/UiCapture.cpp): the meter
        // fills at 0x01..0x07 and the EQ curve glyphs at 0x08..0x0E are real
        // cell contents, and writing them raw yields a file no strict JSON
        // parser will read -- side_by_side.py and `m8drv inspect` both die on
        // the control character while the hand-rolled readers do not, so the
        // breakage only shows on the screens that have meters.
        char escaped[8];
        const unsigned char uch = static_cast<unsigned char>(cl.ch);
        if      (cl.ch == '"')  std::snprintf(escaped, sizeof(escaped), "\\\"");
        else if (cl.ch == '\\') std::snprintf(escaped, sizeof(escaped), "\\\\");
        else if (uch < 0x20 || uch >= 0x7F)
            std::snprintf(escaped, sizeof(escaped), "\\u%04X", uch);
        else                     std::snprintf(escaped, sizeof(escaped), "%c", cl.ch);
        out << "    {\"col\":" << cl.col << ",\"row\":" << cl.row
            << ",\"ch\":\"" << escaped << "\",\"fg\":" << remapStyle(cl.fg)
            << ",\"bg\":" << remapStyle(cl.bg) << "}";
        out << (i + 1 < cells.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    // Clone has no highlight rects in this implementation (cursor is encoded
    // in cell.bg == kHighlightColor, not as a separate Rect).
    out << "  \"rects\": []\n";
    out << "}\n";
}
