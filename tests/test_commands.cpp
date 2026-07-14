#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;
using namespace std;

TEST_CASE("B6.1 ParamID round trip", "[commands]") {
    OfflineHost host;
    EngineCommand cmd; cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::MIX_TRK_VOL;
    cmd.value = 42;
    host.push(cmd);
    host.render(512); // flush
    
    REQUIRE(host.engine().getStateForInit().mixer.track_vol[0] == 42); // Param dispatcher sets track 0 vol here by default without row offset? Actually let's just test P_TEMPO_INT
    cmd.paramId = ParamID::BPM_INT;
    cmd.value = 142;
    host.push(cmd);
    host.render(512);
    REQUIRE(host.engine().getStateForInit().bpm == 142);
}

TEST_CASE("B6.5 PlayMode values pinned", "[commands]") {
    STATIC_REQUIRE(static_cast<uint8_t>(PlayMode::PHRASE) == 1);
    STATIC_REQUIRE(static_cast<uint8_t>(PlayMode::CHAIN) == 2);
    STATIC_REQUIRE(static_cast<uint8_t>(PlayMode::SONG) == 3);
}

TEST_CASE("B6.6 Playhead row identity", "[commands]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 4, 60, 100, 0);
    
    host.push(playPhrase(0, 4, 0));
    // Check ticks
    for (int t = 0; t < 6; ++t) {
        host.render(1000);
        auto p = host.engine().getPlayhead(0);
        if (t < 5) REQUIRE(p.phraseRow == 4);
        else REQUIRE(p.phraseRow == 5);
    }
}

TEST_CASE("B6.7 Engine-initiated stop is visible", "[commands]") {
    OfflineHost host;
    setChain(host.sequencer(), 0, 0, 0);
    setChain(host.sequencer(), 0, 1, PHRASE_EMPTY);
    
    host.push(playChain(0, 0));
    host.renderSeconds(5.0); // should stop after 1 row
    
    auto p = host.engine().getPlayhead(0);
    REQUIRE(p.playMode == static_cast<uint8_t>(PlayMode::NONE));
}

TEST_CASE("B6.8 PLAY_START latency", "[commands]") {
    OfflineHost host;
    // Fast forward to a random phase offset
    host.renderSeconds(500.0 / 48000.0);
    host.clearEvents();
    
    // Push PLAY_START
    host.push(playSong(0));
    host.renderSeconds(0.01);
    
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() == 1);
    REQUIRE(ticks[0].sampleTime == 501);
}


TEST_CASE("B6.2 Ring overflow resync", "[commands]") {
    OfflineHost host;
    Sequencer refSeq;
    refSeq.clear();
    
    std::vector<EngineCommand> pending;
    
    for (int i = 0; i < 2000; ++i) {
        EngineCommand cmd; cmd.type = CommandType::SET_STEP;
        cmd.targetId = i % Sequencer::NUM_PHRASES;
        cmd.row = (i / Sequencer::NUM_PHRASES) % Sequencer::ROWS;
        cmd.u.step.note = i % 128;
        
        refSeq.phrases[cmd.targetId][cmd.row] = cmd.u.step;
        
        if (!host.push(cmd)) {
            pending.push_back(cmd);
        }
    }
    
    host.render(512); // flush
    
    for (const auto& cmd : pending) {
        bool ok = host.push(cmd);
        REQUIRE(ok);
    }
    
    host.render(512); // flush
    
    REQUIRE(std::memcmp(&host.sequencer(), &refSeq, sizeof(Sequencer)) == 0);
}

TEST_CASE("B6.3 Playhead after stop", "[commands]") {
    OfflineHost host;
    host.push(playSong(0));
    host.render(512);
    
    EngineCommand cmd; cmd.type = CommandType::PLAY_STOP;
    host.push(cmd);
    host.render(512);
    
    for (int t = 0; t < 8; ++t) {
        REQUIRE(host.engine().getPlayhead(t).playMode == 0);
    }
}

TEST_CASE("B6.4 Playhead is never stale", "[commands]") {
    OfflineHost host;
    host.push(playSong(0));
    host.render(512);
    
    EngineCommand cmd; cmd.type = CommandType::PLAY_STOP;
    host.push(cmd);
    host.render(512);
    
    host.push(playPhrase(0, 0, 3));
    host.render(512);
    
    for (int t = 0; t < 8; ++t) {
        auto p = host.engine().getPlayhead(t);
        REQUIRE(p.playMode == static_cast<uint8_t>(PlayMode::PHRASE));
        REQUIRE(p.activeCol == 3);
    }
}

TEST_CASE("B6.9 Playhead packing round-trip", "[commands]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    
    for (int song = 0; song < 256; song += 31) {
        for (int chain = 0; chain < 256; chain += 31) {
            for (int phrase = 0; phrase < 256; phrase += 31) {
                for (int mode = 0; mode < 4; ++mode) {
                    for (int col = 0; col < 8; col += 2) {
                        state.playSongRow[0] = song;
                        state.playChainRow[0] = chain;
                        state.playPhraseRow[0] = phrase;
                        state.playMode = static_cast<PlayMode>(mode);
                        state.activeCol = col;
                        state.playTick[0] = 0;
                        
                        host.forcePublishPlayhead(0);
                        
                        auto p = host.engine().getPlayhead(0);
                        REQUIRE(p.songRow == song);
                        REQUIRE(p.chainRow == chain);
                        REQUIRE(p.phraseRow == phrase);
                        REQUIRE(p.playMode == mode);
                        REQUIRE(p.activeCol == col);
                    }
                }
            }
        }
    }
}
