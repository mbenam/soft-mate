#pragma once
#include <string>
#include <vector>
#include "../engine/Engine.h"
#include "../engine/Sequencer.h"
#include "SongIO.h"

namespace m8 {
namespace io {

struct BundleResult {
    bool ok = false;
    std::string errorMsg;
    std::string bundleDirectory;
    std::string songPath;
    std::vector<std::string> copiedSamples;
    std::vector<std::string> missingSamples;
};

BundleResult ExportSongBundle(const std::string& songName,
                             const std::string& currentSongPath,
                             const LoadResult& loadResult,
                             const engine::Sequencer& sequencer,
                             const engine::EngineState& engineState,
                             const std::string& sampleRoot = "",
                             const std::string& destinationBaseDir = "Bundles");

} // namespace io
} // namespace m8
