#pragma once

#include "engine/Engine.h"
#include <string>
#include <vector>
#include <cstdint>

namespace m8::io {

struct LoadResult {
    bool ok = false;
    std::string error;
    engine::Sequencer sequencer;
    engine::EngineState state;
    std::vector<std::string> samplePaths;
    std::vector<std::string> missing;
    std::vector<uint8_t> original;
    bool writable = false;
};

LoadResult loadSong(const std::string& path, const std::string& sampleRoot);
bool saveSong(const std::string& path, const LoadResult& origin,
              const engine::Sequencer& seq, const engine::EngineState& state,
              std::string& error);

} // namespace m8::io
