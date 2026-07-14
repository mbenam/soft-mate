#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/Engine.h"
#include "engine/EngineEvents.h"
#include <memory>
#include <vector>
using namespace m8::engine;

TEST_CASE("Demo song offline verification", "[demo]") {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    auto& engine = *enginePtr;
    engine.loadDemoSong();

    EngineCommand play{};
    play.type = CommandType::PLAY_START;
    play.value = static_cast<int>(PlayMode::SONG);
    play.targetId = 0;
    play.col = 0;
    play.row = 0;
    ring.push(play);

    constexpr int kFramesPerChunk = 512;
    constexpr int kTotalFrames = 30 * 48000;
    std::vector<float> buf(kFramesPerChunk * 2);   // stereo: frames*2 floats
    int framesDone = 0;
    int noteOnCount[8] = {};

    while (framesDone < kTotalFrames) {
        int n = std::min(kFramesPerChunk, kTotalFrames - framesDone);
        engine.render(buf.data(), n);
        for (int i = 0; i < n * 2; ++i) {
            EngineEvent ev;
            while (engine.getEventRing().pop(ev)) {
                if (ev.type == EventType::NOTE_ON && ev.track < 8)
                    noteOnCount[ev.track]++;
            }
        }
        bool bad = false;
        for (int i = 0; i < n * 2; ++i) {
            if (!std::isfinite(buf[i]) || std::abs(buf[i]) > 1.0f) { bad = true; break; }
        }
        REQUIRE_FALSE(bad);
        framesDone += n;
    }

    INFO("Note-on counts per track after 30s:");
    for (int t = 0; t < 8; ++t)
        INFO("  track " << t << ": " << noteOnCount[t]);

    for (int t = 0; t < 8; ++t) {
        REQUIRE(noteOnCount[t] > 0);
    }
}
