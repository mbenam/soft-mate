#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <vector>
#include <cstring>
#include <cstdio>

using namespace m8::test;
using namespace m8::engine;
using namespace std;

TEST_CASE("B7.1 Load and play", "[sample_pool]") {
    OfflineHost host;
    
    std::vector<float> ramp(100);
    for (int i=0; i<100; ++i) ramp[i] = i / 100.0f;
    
    SampleData sd;
    sd.data = ramp.data();
    sd.channels = 1;
    sd.sampleRate = 48000;
    sd.frames = 100;
    
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);
    
    host.engine().getStateForInit().instruments[0].type = InstType::INST_SAMPLER;
    host.engine().getStateForInit().instruments[0].sampler.sample = 0;
    
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    
    host.render(200); // Note triggered
    REQUIRE(host.audio().size() == 400); // 200 stereo samples
    // Just ensure it played something
    bool hasAudio = false;
    for(float f : host.audio()) {
        if (std::abs(f) > 0.001f) hasAudio = true;
    }
    REQUIRE(hasAudio);
}

TEST_CASE("B7.4 Handle bounds", "[sample_pool]") {
    OfflineHost host;
    
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = -1;
    cmd.u.sample = SampleData{};
    printf("B7.4 push -1\n"); fflush(stdout);
    host.push(cmd);
    
    cmd.targetId = 9999;
    cmd.u.sample = SampleData{};
    printf("B7.4 push 9999\n"); fflush(stdout);
    host.push(cmd);
    
    printf("B7.4 render\n"); fflush(stdout);
    host.render(512);
    printf("B7.4 done\n");
    REQUIRE(true); // Should not crash
}

TEST_CASE("B7.2 Reload sample while playing", "[sample_pool]") {
    OfflineHost host;
    
    std::vector<float> data1(100, 0.5f);
    SampleData sd1;
    sd1.data = data1.data();
    sd1.frames = 100;
    std::strncpy(sd1.path, "sample_a.wav", 127);
    
    EngineCommand loadCmd;
    loadCmd.type = CommandType::LOAD_SAMPLE;
    loadCmd.targetId = 0;
    loadCmd.u.sample = sd1;
    host.push(loadCmd);
    host.render(10);
    
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // instrument 0
    host.push(playPhrase(0, 0, 0));
    host.render(100);
    
    std::vector<float> data2(100, 0.25f);
    SampleData sd2;
    sd2.data = data2.data();
    sd2.frames = 100;
    std::strncpy(sd2.path, "sample_b.wav", 127);
    
    loadCmd.u.sample = sd2;
    host.push(loadCmd);
    host.render(100);
    
    int gcCount = 0;
    SampleData gcData;
    while (host.engine().getGcRing().pop(gcData)) {
        if (gcData.data == sd1.data) gcCount++;
    }
    REQUIRE(gcCount == 1);
    
    auto offs = host.eventsOfType(EventType::NOTE_OFF);
    REQUIRE(offs.size() >= 1);
}


TEST_CASE("B7.3 GC ring drained exactly once", "[sample_pool]") {
    OfflineHost host;
    
    // Load 10 samples with distinct paths to the same instrument (0)
    for (int i = 0; i < 10; ++i) {
        SampleData sd;
        sd.data = (float*)(uintptr_t)(i + 1); // fake pointer
        
        char path[128];
        std::snprintf(path, sizeof(path), "fake_%d.wav", i);
        std::strncpy(sd.path, path, 127);
        
        EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
        cmd.targetId = 0;
        cmd.u.sample = sd;
        host.push(cmd);
        host.render(10); // flush
    }
    
    int gcCount = 0;
    SampleData gcData;
    while (host.engine().getGcRing().pop(gcData)) {
        gcCount++;
    }
    
    // The first load replaces an empty slot (old.data == nullptr)
    // The next 9 loads replace existing sample data and should go into the GC ring.
    // The ring is size 64 so none are dropped.
    REQUIRE(gcCount == 9);
}

TEST_CASE("B7.5 Null sample", "[sample_pool]") {
    OfflineHost host;
    
    host.engine().getStateForInit().instruments[0].type = InstType::INST_SAMPLER;
    host.engine().getStateForInit().instruments[0].sampler.sample = -1; // Null sample
    
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    
    host.render(512);

    bool hasAudio = false;
    for(float f : host.audio()) { if (std::abs(f) > 0.001f) hasAudio = true; }
    REQUIRE(!hasAudio);
}

TEST_CASE("B7.6 Sample-rate conversion", "[sample_pool]") {
    OfflineHost host;
    
    std::vector<float> ramp(48000);
    for (int i = 0; i < 48000; ++i) ramp[i] = i / 48000.0f;
    
    SampleData sd;
    sd.data = ramp.data();
    sd.channels = 1;
    sd.sampleRate = 44100; // Tagged as 44.1kHz
    sd.frames = 48000;
    
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);
    
    host.engine().getStateForInit().instruments[0].type = InstType::INST_SAMPLER;
    host.engine().getStateForInit().instruments[0].sampler.sample = 0;
    
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // Note 60 is C4 (root pitch, 261.625565 Hz)
    host.push(playPhrase(0, 0, 0));
    
    host.render(1000); // render exactly 1000 frames

    float expectedPhase = 1000.0f * (44100.0f / 48000.0f); // 918.75
    float actualPhase = host.engine().getSamplePhase(0);
    REQUIRE(std::abs(actualPhase - expectedPhase) < 1.0f);
}
