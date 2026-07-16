#include "FileBrowser.h"
#include <algorithm>
#define DR_WAV_IMPLEMENTATION
#include "../engine/dr_wav.h"

namespace fs = std::filesystem;

FileBrowser::FileBrowser() {}

void FileBrowser::init(const std::string& startDir, const std::string& filter) {
    currentDir = startDir;
    filterExt = filter;
    if (!fs::exists(currentDir)) {
        fs::create_directories(currentDir);
    }
    scanDirectory();
}

void FileBrowser::scanDirectory() {
    entries.clear();
    cursorIndex = 0;
    scrollOffset = 0;

    entries.push_back({"/..", fs::path(currentDir).parent_path().string(), true});

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    try {
        for (const auto& entry : fs::directory_iterator(currentDir)) {
            if (entry.is_directory()) {
                dirs.push_back({"/" + entry.path().filename().string(), entry.path().string(), true});
            } else {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
                if (filterExt.empty()) {
                    files.push_back({entry.path().filename().string(), entry.path().string(), false});
                } else {
                    std::string fe = filterExt;
                    std::transform(fe.begin(), fe.end(), fe.begin(), ::toupper);
                    if (ext == fe) {
                        files.push_back({entry.path().filename().string(), entry.path().string(), false});
                    }
                }
            }
        }
    } catch (...) {}

    auto sortFunc = [](const FileEntry& a, const FileEntry& b) {
        return a.name < b.name;
    };
    std::sort(dirs.begin(), dirs.end(), sortFunc);
    std::sort(files.begin(), files.end(), sortFunc);

    entries.insert(entries.end(), dirs.begin(), dirs.end());
    entries.insert(entries.end(), files.begin(), files.end());
}

std::string FileBrowser::handleInput(SDL_Event& event, bool editHeld) {
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_DOWN) {
            if (cursorIndex < (int)entries.size() - 1) cursorIndex++;
            if (cursorIndex >= scrollOffset + 16) scrollOffset++;
        } else if (event.key.key == SDLK_UP) {
            if (cursorIndex > 0) cursorIndex--;
            if (cursorIndex < scrollOffset) scrollOffset--;
        } else if (event.key.key == SDLK_RIGHT || (event.key.key == SDLK_X && !editHeld)) {
            if (cursorIndex >= 0 && cursorIndex < (int)entries.size()) {
                if (entries[cursorIndex].isDirectory) {
                    currentDir = entries[cursorIndex].path;
                    scanDirectory();
                } else {
                    return entries[cursorIndex].path;
                }
            }
        }
    }
    return "";
}

void FileBrowser::update(Renderer& renderer, SDL_Color colorWhite, SDL_Color colorCyan, SDL_Color colorRed) {
    renderer.drawString(title, 2, 1, colorCyan);

    for (int i = 0; i < 16; ++i) {
        int entryIdx = scrollOffset + i;
        if (entryIdx >= (int)entries.size()) break;

        int y = 4 + i;
        std::string name = entries[entryIdx].name;
        if (name.length() > 28) name = name.substr(0, 28);

        if (entryIdx == cursorIndex) {
            renderer.fillRectPixel(2 * 8 - 2, y * 8, name.length() * 8 + 4, 8, colorRed);
            renderer.drawString(name, 2, y, {255, 255, 255, 255});
        } else {
            renderer.drawString(name, 2, y, colorWhite);
        }
    }
}

bool FileBrowser::loadWavFile(const std::string& path, m8::engine::SampleData& outData) {
    unsigned int channels;
    unsigned int sampleRate;
    drwav_uint64 totalPCMFrameCount;
    float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sampleRate, &totalPCMFrameCount, NULL);

    if (pSampleData == NULL) {
        return false;
    }

    outData.data = pSampleData;
    outData.frames = totalPCMFrameCount;
    outData.channels = channels;
    outData.sampleRate = sampleRate;

    return true;
}

void FileBrowser::freeWavFile(m8::engine::SampleData& data) {
    if (data.data) {
        drwav_free(data.data, NULL);
        data.data = nullptr;
    }
}
