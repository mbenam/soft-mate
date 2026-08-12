#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../engine/Engine.h"
#include "../engine/SeqTypes.h"
#include "../engine/CommandRing.h"

namespace m8 {
namespace io {

struct RenderSettings {
    int songRowStart = 0;              // 0x00 .. 0xFF
    int songRowLast = -1;              // -1 = AUTO (--), 0x00 .. 0xFF
    int repeatCount = 0;               // 0 = OFF (00), 1 .. 99
    bool trackEnabled[8] = {true, true, true, true, true, true, true, true};
    bool modfxEnabled = true;
    bool delayEnabled = true;
    bool reverbEnabled = true;
    bool limiterEnabled = true;
    bool mixEqEnabled = true;
    bool is32Bit = false;              // false = 16-BIT, true = 32-BIT
    char name[13] = "PROBE_SELFTE";    // 12 chars + null
    std::string sampleRoot;            // Directory to resolve samples from (empty = search defaults)
    std::string statusMsg;
    uint32_t statusExpiry = 0;
};

struct RenderResult {
    bool ok = false;
    std::string errorMsg;
    std::vector<std::string> outputFiles;
    size_t totalFrames = 0;
};

RenderResult RenderSongAudio(const RenderSettings& settings,
                            const engine::Sequencer& uiSequencer,
                            const engine::EngineState& uiEngineState,
                            bool stemsMode,
                            const std::string& outputDirectory = "Renders");

} // namespace io
} // namespace m8
