#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;
using namespace std;

TEST_CASE("B3.1 Tick spacing", "[tempo]") {
    OfflineHost host;
    host.push(playSong(0));
    host.renderSeconds(10.0);
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 2);
    // At 120 BPM, 6 ticks/row, 48000 Hz, sample delta is exactly 1000
    for (size_t i = 1; i < ticks.size(); ++i) {
        REQUIRE(ticks[i].sampleTime - ticks[i-1].sampleTime == 1000);
    }
}

TEST_CASE("B3.2 Row spacing", "[tempo]") {
    OfflineHost host;
    for(int r = 0; r < 16; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    setChain(host.sequencer(), 0, 0, 0);
    setSong(host.sequencer(), 0, 0, 0);
    
    host.push(playSong(0));
    host.renderSeconds(2.0);
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() > 2);
    for (size_t i = 1; i < notes.size(); ++i) {
        REQUIRE(notes[i].sampleTime - notes[i-1].sampleTime == 6000); // 1000 * 6
    }
}

TEST_CASE("B3.3 Non-integer samplesPerTick", "[tempo]") {
    OfflineHost host;
    EngineCommand cmd; cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::BPM_INT; cmd.value = 137;
    host.push(cmd);
    
    host.push(playSong(0));
    host.renderSeconds(60.0);
    
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 2);
    
    double exactInterval = 48000.0 / (137.0 * 4.0 / 60.0 * 6.0); // 875.9124...
    
    for (size_t i = 1; i < ticks.size(); ++i) {
        uint64_t delta = ticks[i].sampleTime - ticks[i-1].sampleTime;
        REQUIRE((delta == 875 || delta == 876));
        
        double expectedTime = exactInterval * i;
        double diff = std::abs((double)(ticks[i].sampleTime - ticks[0].sampleTime) - expectedTime);
        REQUIRE(diff < 1.5); // bounded drift
    }
}

TEST_CASE("B3.4 Tempo change mid-playback", "[tempo]") {
    OfflineHost host;
    host.push(playSong(0));
    host.renderSeconds(2.0);
    
    EngineCommand cmd; cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::BPM_INT; cmd.value = 60;
    host.push(cmd);
    
    host.clearEvents();
    host.renderSeconds(2.0);
    
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 2);
    for (size_t i = 1; i < ticks.size(); ++i) {
        // Delta should double from 1000 to 2000
        uint64_t delta = ticks[i].sampleTime - ticks[i-1].sampleTime;
        REQUIRE(delta == 2000);
    }
}

TEST_CASE("B3.5 Chunk-size invariance", "[tempo]") {
    // Run B3.1 at chunk size 7
    OfflineHost host;
    host.push(playSong(0));
    host.renderSeconds(2.0, 7);
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 2);
    for (size_t i = 1; i < ticks.size(); ++i) {
        REQUIRE(ticks[i].sampleTime - ticks[i-1].sampleTime == 1000);
    }
}

TEST_CASE("B3.6 BPM clamping", "[tempo]") {
    OfflineHost host;
    EngineCommand cmd; cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::BPM_INT; cmd.value = 0;
    host.push(cmd);
    host.push(playSong(0));
    host.renderSeconds(1.0);
    
    cmd.paramId = ParamID::BPM_INT; cmd.value = 100000;
    host.push(cmd);
    host.renderSeconds(1.0);
    // Should not hang or crash
    REQUIRE(true);
}
