#include "BundleExport.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace m8 {
namespace io {

BundleResult ExportSongBundle(const std::string& songName,
                             const std::string& currentSongPath,
                             const LoadResult& loadResult,
                             const engine::Sequencer& sequencer,
                             const engine::EngineState& engineState,
                             const std::string& sampleRoot,
                             const std::string& destinationBaseDir) {
    BundleResult res;

    // 1. Determine clean song name
    std::string name = songName;
    if (name.empty() && engineState.project.name[0] != '\0') {
        name = engineState.project.name;
    }
    if (name.empty() && !currentSongPath.empty()) {
        fs::path p(currentSongPath);
        name = p.stem().string();
    }
    // Trim trailing dashes and spaces
    while (!name.empty() && (name.back() == '-' || name.back() == ' ' || name.back() == '\0')) {
        name.pop_back();
    }
    if (name.empty()) name = "BUNDLE";

    // 2. Target bundle directory
    fs::path baseDir = destinationBaseDir.empty() ? "Bundles" : destinationBaseDir;
    fs::path bundleDir = baseDir / name;

    std::error_code ec;
    fs::create_directories(bundleDir, ec);
    if (ec) {
        res.errorMsg = "Cannot create bundle directory: " + bundleDir.string();
        return res;
    }

    res.bundleDirectory = bundleDir.string();

    // 3. Save song file inside bundle directory
    fs::path targetSongPath = bundleDir / (name + ".m8s");
    std::string saveErr;
    bool saveOk = saveSong(targetSongPath.string(), loadResult, sequencer, engineState, saveErr);
    if (!saveOk) {
        res.errorMsg = "Failed to save song in bundle: " + saveErr;
        return res;
    }
    res.songPath = targetSongPath.string();

    // 4. Collect and copy all sample files for INST_SAMPLER
    std::vector<std::string> candidateRoots;
    if (!sampleRoot.empty()) candidateRoots.push_back(sampleRoot);
    candidateRoots.push_back("songs");
    candidateRoots.push_back("Songs");
    candidateRoots.push_back("samples");
    candidateRoots.push_back("Samples");
    candidateRoots.push_back("");
    candidateRoots.push_back("..");
    candidateRoots.push_back("../songs");
    candidateRoots.push_back("../../songs");
    candidateRoots.push_back("../../../songs");
    candidateRoots.push_back("../Samples");
    candidateRoots.push_back("../../Samples");
    candidateRoots.push_back("../../../Samples");
    candidateRoots.push_back("../samples");
    candidateRoots.push_back("../../samples");
    candidateRoots.push_back("../../../samples");

    for (int i = 0; i < 128; ++i) {
        if (engineState.instruments[i].type != engine::InstType::INST_SAMPLER) continue;
        const char* mpath = engineState.instruments[i].sampler.samplePath;
        if (mpath[0] == '\0') continue;

        std::string rel = mpath;
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel = rel.substr(1);

        fs::path foundSrcPath;
        bool found = false;

        for (const auto& r : candidateRoots) {
            fs::path p;
            if (r.empty()) p = rel;
            else p = fs::path(r) / rel;

            if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
                foundSrcPath = p;
                found = true;
                break;
            }
        }

        if (found) {
            fs::path destSamplePath = bundleDir / rel;
            if (destSamplePath.has_parent_path()) {
                fs::create_directories(destSamplePath.parent_path(), ec);
            }
            fs::copy_file(foundSrcPath, destSamplePath, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                res.copiedSamples.push_back(rel);
            } else {
                res.missingSamples.push_back(rel + " (copy failed)");
            }
        } else {
            res.missingSamples.push_back(rel);
        }
    }

    res.ok = true;
    return res;
}

} // namespace io
} // namespace m8
