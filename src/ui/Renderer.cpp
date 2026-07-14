#include "Renderer.h"
#include "font.h"
#include <iostream>

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

bool Renderer::init(int logicalWidth, int logicalHeight, int scale) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
        return false;
    }

    m_window = SDL_CreateWindow("M8 Clone", logicalWidth * scale, logicalHeight * scale, SDL_WINDOW_RESIZABLE);
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

void Renderer::clear(SDL_Color color) {
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
}

void Renderer::drawString(const char* str, int gridX, int gridY, SDL_Color color) {
    for (size_t i = 0; str[i] != '\0'; ++i) {
        drawChar(str[i], gridX + i, gridY, color);
    }
}
