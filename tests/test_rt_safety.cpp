#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include "support/OfflineHost.h"

using namespace m8::engine;
using namespace m8::test;

TEST_CASE("B8.5 TSan smoke test", "[rt_safety]") {
    OfflineHost host;
    std::atomic<bool> stopFlag{false};
    
    // Audio thread: runs render() in a tight loop
    std::thread audioThread([&]() {
        std::vector<float> tmp(512 * 2);
        while (!stopFlag) {
            host.engine().render(tmp.data(), 512);
        }
    });
    
    // Main thread: pushing commands and reading getPlayhead()
    auto start = std::chrono::steady_clock::now();
    int iter = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        EngineCommand cmd{};
        if (iter % 4 == 0) {
            cmd.type = CommandType::PLAY_START;
            cmd.value = static_cast<int>(PlayMode::SONG);
            cmd.targetId = iter % 255;
        } else if (iter % 4 == 1) {
            cmd.type = CommandType::UPDATE_PARAM;
            cmd.targetId = static_cast<int>(ParamID::MIX_MIX_VOL);
            cmd.value = iter % 255;
        } else if (iter % 4 == 2) {
            cmd.type = CommandType::SET_STEP;
            cmd.targetId = 0; // phrase
            cmd.row = 0;
            cmd.u.step.note = 60;
        } else {
            cmd.type = CommandType::LOAD_SAMPLE;
            cmd.targetId = 0;
            cmd.u.sample = SampleData{};
        }
        host.push(cmd);
        
        for (int t = 0; t < 8; ++t) { volatile auto p = host.engine().getPlayhead(t); (void)p; }
        
        volatile int st = host.engine().getStateForInit().mixer.mix_vol;
        if (st == 1234567) std::cout << "wow\n";
        
        iter++;
    }
    
    stopFlag = true;
    audioThread.join();
    
    REQUIRE(true); // If it doesn't crash or trigger TSan, we pass
}
