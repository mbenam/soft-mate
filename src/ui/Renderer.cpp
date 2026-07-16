#include "Renderer.h"
#include "font.h"
#include <iostream>
#include <fstream>
#include <sstream>
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

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
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
