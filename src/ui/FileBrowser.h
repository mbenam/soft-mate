#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "Renderer.h"
#include "../engine/CommandRing.h"
#include "../engine/SamplePool.h"

struct FileEntry {
    std::string name;
    std::string path;
    bool isDirectory;
};

class FileBrowser {
public:
    FileBrowser();
    void init(const std::string& startDir);
    void update(Renderer& renderer, SDL_Color colorWhite, SDL_Color colorCyan, SDL_Color colorRed);
    
    // Returns the selected file path if a file was selected this frame, else empty string
    std::string handleInput(SDL_Event& event, bool editHeld);

    static bool loadWavFile(const std::string& path, m8::engine::SampleData& outData);
    static void freeWavFile(m8::engine::SampleData& data);

private:
    void scanDirectory();

    std::string currentDir;
    std::vector<FileEntry> entries;
    int cursorIndex = 0;
    int scrollOffset = 0;
};
