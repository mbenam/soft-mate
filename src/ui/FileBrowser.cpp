#include "FileBrowser.h"
#include <algorithm>
#include <cctype>
#define DR_WAV_IMPLEMENTATION
#include "../engine/dr_wav.h"

namespace fs = std::filesystem;

FileBrowser::FileBrowser() {}

void FileBrowser::init(const std::string& startDir, const std::string& filter, Mode m) {
    filterExt = filter;
    mode = m;
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

    std::error_code ec;
    auto sortCaseInsensitive = [](const FileEntry& a, const FileEntry& b) {
        std::string sa = a.name;
        std::string sb = b.name;
        std::transform(sa.begin(), sa.end(), sa.begin(), ::toupper);
        std::transform(sb.begin(), sb.end(), sb.begin(), ::toupper);
        return sa < sb;
    };

    if (mode == Mode::DIRECTORY) {
        // Format relative display directory string e.g. "/THEMES", "/SCALES", "/SCALES/FACTORY"
        std::string relPath = currentDirStr;
        for (char& c : relPath) if (c == '\\') c = '/';

        auto findKnown = [&](const std::string& folderName) -> std::string {
            auto pos = relPath.find(folderName);
            if (pos == std::string::npos) {
                std::string lower = folderName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                pos = relPath.find(lower);
            }
            if (pos != std::string::npos) {
                return "/" + relPath.substr(pos);
            }
            return "";
        };

        std::string displayRel = findKnown("Themes");
        if (displayRel.empty()) displayRel = findKnown("Scales");
        if (displayRel.empty()) displayRel = findKnown("Instruments");
        if (displayRel.empty()) displayRel = findKnown("Songs");
        if (displayRel.empty()) displayRel = "/" + currentDir.filename().generic_string();

        std::transform(displayRel.begin(), displayRel.end(), displayRel.begin(), ::toupper);

        // 1. First entry: "SAVE TO <displayRel>"
        entries.push_back({"SAVE TO " + displayRel, currentDir.generic_string(), false});

        // 2. Parent directory
        if (currentDir.has_parent_path() && currentDir != currentDir.parent_path()) {
            entries.push_back({"/..", currentDir.parent_path().generic_string(), true});
        }

        // 3. Subdirectories
        std::vector<FileEntry> dirs;
        try {
            for (const auto& entry : fs::directory_iterator(currentDir, fs::directory_options::skip_permission_denied, ec)) {
                std::error_code statusEc;
                if (entry.is_directory(statusEc)) {
                    std::string fname = entry.path().filename().generic_string();
                    std::string fnameUpper = fname;
                    std::transform(fnameUpper.begin(), fnameUpper.end(), fnameUpper.begin(), ::toupper);
                    dirs.push_back({"/" + fnameUpper, entry.path().generic_string(), true});
                }
            }
        } catch (...) {}
        std::sort(dirs.begin(), dirs.end(), sortCaseInsensitive);
        entries.insert(entries.end(), dirs.begin(), dirs.end());

        // 4. Create directory entry
        entries.push_back({"(CREATE DIRECTORY)", "", false});
    } else {
        // Add parent directory entry if not at filesystem root
        if (currentDir.has_parent_path() && currentDir != currentDir.parent_path()) {
            entries.push_back({"/..", currentDir.parent_path().generic_string(), true});
        }

        std::vector<FileEntry> dirs;
        std::vector<FileEntry> files;

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

        std::sort(dirs.begin(), dirs.end(), sortCaseInsensitive);
        std::sort(files.begin(), files.end(), sortCaseInsensitive);

        entries.insert(entries.end(), dirs.begin(), dirs.end());
        entries.insert(entries.end(), files.begin(), files.end());
    }

    // If returning from a child folder, position cursor on that folder
    if (!selectTargetName.empty()) {
        std::string target = "/" + selectTargetName;
        std::string targetUpper = target;
        std::transform(targetUpper.begin(), targetUpper.end(), targetUpper.begin(), ::toupper);
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string entryUpper = entries[i].name;
            std::transform(entryUpper.begin(), entryUpper.end(), entryUpper.begin(), ::toupper);
            if (entryUpper == targetUpper) {
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
    (void)editHeld;
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

    // EDIT key (X / Enter / KP_Enter / Right Arrow) selects / executes current row
    if (key == SDLK_RIGHT || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_X) {
        if (cursorIndex >= 0 && cursorIndex < (int)entries.size()) {
            const auto& entry = entries[cursorIndex];
            if (entry.name.rfind("SAVE TO ", 0) == 0) {
                selectedPath = entry.path;
                return Result::SELECTED;
            }
            if (entry.name == "(CREATE DIRECTORY)") {
                return Result::CREATE_DIR;
            }
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

    // Left arrow navigates up if inside subfolder, or cancels if at root
    if (key == SDLK_LEFT) {
        if (!entries.empty()) {
            for (const auto& e : entries) {
                if (e.name == "/..") {
                    navigateUp();
                    return Result::NONE;
                }
            }
        }
        return Result::CANCELLED;
    }

    // OPTION key (Z / Escape / Alt) cancels back to previous screen
    if (key == SDLK_ESCAPE || key == SDLK_Z || key == SDLK_LALT || key == SDLK_RALT) {
        return Result::CANCELLED;
    }

    return Result::NONE;
}

void FileBrowser::update(Renderer& renderer, SDL_Color colorWhite, SDL_Color colorCyan, SDL_Color colorRed) {
    renderer.drawString(title, 2, 1, colorRed);

    if (entries.empty()) {
        renderer.drawString("(NO FILES)", 2, 3, {100, 100, 100, 255});
        return;
    }

    for (int i = 0; i < 16; ++i) {
        int entryIdx = scrollOffset + i;
        if (entryIdx >= (int)entries.size()) break;

        int y = 3 + i;
        std::string name = entries[entryIdx].name;
        if (name.length() > 34) name = name.substr(0, 34);

        bool isSelected = (entryIdx == cursorIndex);

        if (name.rfind("SAVE TO ", 0) == 0) {
            renderer.drawString(name, 2, y, colorCyan);
            if (isSelected) {
                renderer.drawBracket(2, y, name.length(), colorCyan);
            }
        } else if (name == "(CREATE DIRECTORY)") {
            renderer.drawString(name, 2, y, isSelected ? colorWhite : SDL_Color{140, 140, 140, 255});
            if (isSelected) {
                renderer.drawBracket(2, y, name.length(), colorCyan);
            }
        } else {
            SDL_Color itemColor = isSelected ? colorWhite : (entries[entryIdx].isDirectory ? SDL_Color{180, 180, 180, 255} : colorWhite);
            renderer.drawString(name, 2, y, itemColor);
            if (isSelected) {
                renderer.drawBracket(2, y, name.length(), colorCyan);
            }
        }
    }
}

bool FileBrowser::loadWavFile(const std::string& path, m8::engine::SampleData& outData) {
    drwav wav;
    if (!drwav_init_file_with_metadata(&wav, path.c_str(), 0, NULL)) {
        return false;
    }

    float* pSampleData = (float*)malloc(wav.totalPCMFrameCount * wav.channels * sizeof(float));
    if (!pSampleData) {
        drwav_uninit(&wav);
        return false;
    }

    drwav_uint64 framesRead = drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, pSampleData);
    if (framesRead == 0 && wav.totalPCMFrameCount > 0) {
        free(pSampleData);
        drwav_uninit(&wav);
        return false;
    }

    outData.data = pSampleData;
    outData.frames = static_cast<uint32_t>(wav.totalPCMFrameCount);
    outData.channels = static_cast<uint8_t>(wav.channels);
    outData.sampleRate = wav.sampleRate;
    outData.sliceMarkerCount = 0;
    outData.loopStartFrame = 0;
    outData.loopEndFrame = 0;

    for (drwav_uint32 i = 0; i < wav.metadataCount; ++i) {
        if (wav.pMetadata[i].type == drwav_metadata_type_cue) {
            const auto& cue = wav.pMetadata[i].data.cue;
            int count = std::min<int>(static_cast<int>(cue.cuePointCount), m8::engine::SampleData::kMaxSliceMarkers);
            outData.sliceMarkerCount = count;
            for (int m = 0; m < count; ++m) {
                outData.sliceMarkers[m] = cue.pCuePoints[m].sampleOffset;
            }
            std::sort(outData.sliceMarkers, outData.sliceMarkers + outData.sliceMarkerCount);
        } else if (wav.pMetadata[i].type == drwav_metadata_type_smpl) {
            const auto& smpl = wav.pMetadata[i].data.smpl;
            if (smpl.sampleLoopCount > 0 && smpl.pLoops) {
                outData.loopStartFrame = smpl.pLoops[0].firstSampleOffset;
                outData.loopEndFrame = smpl.pLoops[0].lastSampleOffset;
            }
        }
    }

    drwav_uninit(&wav);
    return true;
}

void FileBrowser::freeWavFile(m8::engine::SampleData& data) {
    if (data.data) {
        free(data.data);
        data.data = nullptr;
    }
}

