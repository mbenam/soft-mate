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

// Author a complete .m8s from engine state alone — no source file to overlay.
// Unlike saveSong (which re-serialises edits over a file the user loaded), this
// builds a song entirely from the engine: instrument TYPES, mods/envelopes,
// names, and sample paths included, so it can persist a song that was never
// loaded from disk (e.g. the code-generated demo). `templatePath` is a real V4+
// .m8s whose bytes seed the fields the library does not model. Does not touch
// the load/save byte-identical round-trip path.
bool saveNewSong(const std::string& path, const std::string& templatePath,
                 const engine::Sequencer& seq, const engine::EngineState& state,
                 std::string& error);

} // namespace m8::io
