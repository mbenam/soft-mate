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
    bool isDirectory = false;
};

class FileBrowser {
public:
    enum class Result {
        NONE,        // Still browsing
        SELECTED,    // A file was selected (getSelectedPath() returns the path)
        CANCELLED    // User cancelled (e.g. ESC, or LEFT at root directory)
    };

    FileBrowser();
    void init(const std::string& startDir, const std::string& filterExtension = "");
    void update(Renderer& renderer, SDL_Color colorWhite, SDL_Color colorCyan, SDL_Color colorRed);

    // Handles keyboard navigation and selection events
    Result handleInput(const SDL_Event& event, bool editHeld);

    // Path of the selected file when Result::SELECTED is returned
    const std::string& getSelectedPath() const { return selectedPath; }

    // Title displayed at top of browser
    void setTitle(const std::string& t) { title = t; }

    // Accessors for testing and UI state
    const std::string& getCurrentDirectory() const { return currentDirStr; }
    int getCursorIndex() const { return cursorIndex; }
    int getScrollOffset() const { return scrollOffset; }
    const std::vector<FileEntry>& getEntries() const { return entries; }

    // Navigation helpers
    void navigateTo(const std::filesystem::path& newPath, const std::string& selectTargetName = "");
    void navigateUp();

    static bool loadWavFile(const std::string& path, m8::engine::SampleData& outData);
    static void freeWavFile(m8::engine::SampleData& data);

private:
    void scanDirectory(const std::string& selectTargetName = "");

    std::filesystem::path currentDir;
    std::string currentDirStr;
    std::string filterExt;  // e.g. ".m8s", ".wav", ".m8i" — empty = show all
    std::string title = "FILE BROWSER";
    std::string selectedPath;
    std::vector<FileEntry> entries;
    int cursorIndex = 0;
    int scrollOffset = 0;
};
