#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <cstdint>
#include <cstdio>

struct VirtualCell {
    char     ch         = ' ';
    uint32_t color      = 0x000000FF;   // RGBA of the glyph
    uint32_t bg         = 0x00000000;   // RGBA of the cell background (cursor detection)
    bool     bracket    = false;        // this cell is inside a [ ] bracket
    uint8_t  slider     = 0;            // slider fill 0..8, else 0
    uint8_t  writeCount = 0;            // how many times this cell was written this frame
};

struct StoredPlayhead {
    uint8_t track      = 0;
    uint8_t songRow    = 0;
    uint8_t chainRow   = 0;
    uint8_t phraseRow  = 0;
    uint8_t playMode   = 0;   // 0=NONE,1=PHRASE,2=CHAIN,3=SONG
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(int logicalWidth, int logicalHeight, int scale = 1, bool hidden = false);
    void clear(SDL_Color color);
    void present();
    
    // Draw character at logical cell (x, y). 
    void drawChar(char c, int x, int y, SDL_Color color);
    void drawString(const std::string& str, int x, int y, SDL_Color color);
    void drawString(const char* str, int x, int y, SDL_Color color);
    void drawRect(int x, int y, int w, int h, SDL_Color color);
    
    // Pixel-level drawing
    void drawRectPixel(int x, int y, int w, int h, SDL_Color color);
    void fillRectPixel(int x, int y, int w, int h, SDL_Color color);
    void drawLinePixel(int x1, int y1, int x2, int y2, SDL_Color color);
    void drawBracket(int cx, int y, int cw, SDL_Color color);

    // Shadow grid accessors
    static constexpr int kGridW = 40;
    static constexpr int kGridH = 30;
    const VirtualCell(&getVram() const)[kGridH][kGridW] { return m_vram; }
    void resetVram();

    // Playhead storage (called from main.cpp before rendering)
    void setPlayheads(const StoredPlayhead* ph, int count);

    // Dump methods (temporary debug triggers)
    void dumpScreenText(const char* path) const;
    void dumpJson(const char* path, const char* screenName, int bpm) const;

    // Overlap detection: returns true if any cell was written more than once this frame
    bool hasOverlap() const;

    // Tier 5 golden snapshot testing (M8_APP_AUTOMATION_SPEC.md). Compact,
    // JSON-free text format: one line per cell (glyph, fg color, bg color,
    // slider fill, bracket flag) -- deliberately excludes bpm/cursor-derived-
    // from-bg-alone/playhead noise by comparing raw per-cell fields directly,
    // so a snapshot is reproducible from any deterministic navigation
    // sequence (e.g. `goto <SCREEN>` from a fresh boot).
    void writeGolden(const std::string& path) const;
    // Returns true if `path`'s stored snapshot matches the current VRAM
    // exactly. On mismatch, fills `mismatchDetail` with the first differing
    // cell's row/col and expected-vs-actual. Returns false (with a reason in
    // `mismatchDetail`) if the golden file doesn't exist or is malformed.
    bool compareGolden(const std::string& path, std::string& mismatchDetail) const;

    SDL_Window* getWindow() const { return m_window; }
    SDL_Renderer* getSDLRenderer() const { return m_renderer; }

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    
    int m_cellWidth = 8;
    int m_cellHeight = 8;

    VirtualCell m_vram[kGridH][kGridW]{};
    StoredPlayhead m_playheads[8]{};
    int m_playheadCount = 0;
};
