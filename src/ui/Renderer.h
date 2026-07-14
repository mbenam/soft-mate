#pragma once

#include <SDL3/SDL.h>
#include <string>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(int logicalWidth, int logicalHeight, int scale = 1);
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

    SDL_Window* getWindow() const { return m_window; }
    SDL_Renderer* getSDLRenderer() const { return m_renderer; }

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    
    int m_cellWidth = 8;
    int m_cellHeight = 8;
};
