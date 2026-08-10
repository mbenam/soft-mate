#include "FileBrowser.h"
#include <algorithm>
#include <cctype>
#define DR_WAV_IMPLEMENTATION
#include "../engine/dr_wav.h"

namespace fs = std::filesystem;

FileBrowser::FileBrowser() {}

void FileBrowser::init(const std::string& startDir, const std::string& filter) {
    filterExt = filter;
    selectedPath.clear();

    std::error_code ec;
    std::string dir = startDir.empty() ? "." : startDir;
    fs::path p(dir);

    if (!fs::exists(p, ec)) {
        fs::create_directories(p, ec);
    }

    if (fs::exists(p, ec)) {
        currentDir = fs::weakly_canonical(p, ec);
        if (ec) currentDir = fs::absolute(p, ec).lexically_normal();
    } else {
        currentDir = fs::current_path(ec);
    }

    scanDirectory();
}

void FileBrowser::navigateTo(const fs::path& newPath, const std::string& selectTargetName) {
    std::error_code ec;
    if (fs::is_directory(newPath, ec)) {
        currentDir = fs::weakly_canonical(newPath, ec);
        if (ec) currentDir = newPath.lexically_normal();
        scanDirectory(selectTargetName);
    }
}

void FileBrowser::navigateUp() {
    if (currentDir.has_parent_path() && currentDir != currentDir.parent_path()) {
        std::string oldDirName = currentDir.filename().generic_string();
        fs::path parent = currentDir.parent_path();
        navigateTo(parent, oldDirName);
    }
}

void FileBrowser::scanDirectory(const std::string& selectTargetName) {
    entries.clear();
    cursorIndex = 0;
    scrollOffset = 0;
    selectedPath.clear();
    currentDirStr = currentDir.generic_string();

    // Add parent directory entry if not at filesystem root
    if (currentDir.has_parent_path() && currentDir != currentDir.parent_path()) {
        entries.push_back({"/..", currentDir.parent_path().generic_string(), true});
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;
    std::error_code ec;

    try {
        for (const auto& entry : fs::directory_iterator(currentDir, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code statusEc;
            if (entry.is_directory(statusEc)) {
                std::string fname = entry.path().filename().generic_string();
                dirs.push_back({"/" + fname, entry.path().generic_string(), true});
            } else if (entry.is_regular_file(statusEc)) {
                std::string ext = entry.path().extension().generic_string();
                bool match = false;
                if (filterExt.empty()) {
                    match = true;
                } else {
                    std::string extUpper = ext;
                    std::transform(extUpper.begin(), extUpper.end(), extUpper.begin(), ::toupper);
                    std::string filtUpper = filterExt;
                    std::transform(filtUpper.begin(), filtUpper.end(), filtUpper.begin(), ::toupper);
                    if (!filtUpper.empty() && filtUpper.front() != '.') {
                        filtUpper = "." + filtUpper;
                    }
                    if (extUpper == filtUpper) {
                        match = true;
                    }
                }
                if (match) {
                    std::string fname = entry.path().filename().generic_string();
                    files.push_back({fname, entry.path().generic_string(), false});
                }
            }
        }
    } catch (...) {}

    auto sortCaseInsensitive = [](const FileEntry& a, const FileEntry& b) {
        std::string sa = a.name;
        std::string sb = b.name;
        std::transform(sa.begin(), sa.end(), sa.begin(), ::toupper);
        std::transform(sb.begin(), sb.end(), sb.begin(), ::toupper);
        return sa < sb;
    };
    std::sort(dirs.begin(), dirs.end(), sortCaseInsensitive);
    std::sort(files.begin(), files.end(), sortCaseInsensitive);

    entries.insert(entries.end(), dirs.begin(), dirs.end());
    entries.insert(entries.end(), files.begin(), files.end());

    // If returning from a child folder, position cursor on that folder
    if (!selectTargetName.empty()) {
        std::string target = "/" + selectTargetName;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].name == target) {
                cursorIndex = static_cast<int>(i);
                if (cursorIndex >= 16) {
                    scrollOffset = cursorIndex - 15;
                }
                break;
            }
        }
    }
}

FileBrowser::Result FileBrowser::handleInput(const SDL_Event& event, bool editHeld) {
    if (event.type != SDL_EVENT_KEY_DOWN) {
        return Result::NONE;
    }

    auto key = event.key.key;

    if (key == SDLK_DOWN) {
        if (cursorIndex < (int)entries.size() - 1) {
            cursorIndex++;
            if (cursorIndex >= scrollOffset + 16) {
                scrollOffset = cursorIndex - 15;
            }
        }
        return Result::NONE;
    }

    if (key == SDLK_UP) {
        if (cursorIndex > 0) {
            cursorIndex--;
            if (cursorIndex < scrollOffset) {
                scrollOffset = cursorIndex;
            }
        }
        return Result::NONE;
    }

    if (key == SDLK_PAGEUP) {
        cursorIndex = std::max(0, cursorIndex - 16);
        if (cursorIndex < scrollOffset) {
            scrollOffset = cursorIndex;
        }
        return Result::NONE;
    }

    if (key == SDLK_PAGEDOWN) {
        cursorIndex = std::min((int)entries.size() - 1, cursorIndex + 16);
        if (cursorIndex >= scrollOffset + 16) {
            scrollOffset = std::max(0, cursorIndex - 15);
        }
        return Result::NONE;
    }

    if (key == SDLK_RIGHT || key == SDLK_RETURN || key == SDLK_KP_ENTER || (key == SDLK_X && !editHeld)) {
        if (cursorIndex >= 0 && cursorIndex < (int)entries.size()) {
            const auto& entry = entries[cursorIndex];
            if (entry.isDirectory) {
                if (entry.name == "/..") {
                    navigateUp();
                } else {
                    navigateTo(fs::path(entry.path));
                }
                return Result::NONE;
            } else {
                selectedPath = entry.path;
                return Result::SELECTED;
            }
        }
        return Result::NONE;
    }

    if (key == SDLK_LEFT) {
        // If we are inside a subfolder and have a parent entry, navigate up
        if (!entries.empty() && entries[0].name == "/..") {
            navigateUp();
            return Result::NONE;
        }
        // At root boundary -> cancel modal
        return Result::CANCELLED;
    }

    if (key == SDLK_ESCAPE) {
        return Result::CANCELLED;
    }

    return Result::NONE;
}

void FileBrowser::update(Renderer& renderer, SDL_Color colorWhite, SDL_Color colorCyan, SDL_Color colorRed) {
    renderer.drawString(title, 2, 1, colorCyan);

    // Format current directory string for display
    std::string displayPath = currentDirStr;
    if (displayPath.length() > 34) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 31);
    }
    renderer.drawString(displayPath, 2, 2, {140, 140, 140, 255});

    if (entries.empty()) {
        renderer.drawString("(NO FILES)", 2, 4, {100, 100, 100, 255});
        return;
    }

    for (int i = 0; i < 16; ++i) {
        int entryIdx = scrollOffset + i;
        if (entryIdx >= (int)entries.size()) break;

        int y = 4 + i;
        std::string name = entries[entryIdx].name;
        if (name.length() > 34) name = name.substr(0, 34);

        if (entryIdx == cursorIndex) {
            renderer.fillRectPixel(2 * 8 - 2, y * 8, name.length() * 8 + 4, 8, colorRed);
            renderer.drawString(name, 2, y, {255, 255, 255, 255});
        } else {
            SDL_Color itemColor = entries[entryIdx].isDirectory ? colorCyan : colorWhite;
            renderer.drawString(name, 2, y, itemColor);
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

