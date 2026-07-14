#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("B5.1 DEL", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::DEL, 3);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 1);
    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 0);
    
    uint64_t tick0_time = ticks[0].sampleTime;
    REQUIRE(notes[0].sampleTime == tick0_time + 3 * 1000);
}

TEST_CASE("B5.2 DEL clamped", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::DEL, 0x0F);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 1);
    auto ticks = host.eventsOfType(EventType::TICK);
    
    uint64_t tick0_time = ticks[0].sampleTime;
    REQUIRE(notes[0].sampleTime == tick0_time + 5 * 1000); // clamps to 6 - 1 = 5
}

TEST_CASE("B5.3 KIL", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::KIL, 3);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    
    auto noteOns = host.noteOnsForTrack(0);
    auto noteOffs = host.noteOffsForTrack(0);
    REQUIRE(noteOns.size() >= 1);
    REQUIRE(noteOffs.size() >= 1);
    
    REQUIRE(noteOffs[0].sampleTime == noteOns[0].sampleTime + 3 * 1000);
}

TEST_CASE("B5.5 HOP", "[fx]") {
    OfflineHost host;
    for (int r = 0; r < 8; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    setFx(host.sequencer(), 0, 3, 0, FxCmd::HOP, 0);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(2.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() > 5);
    for (size_t i = 0; i < notes.size(); ++i) {
        REQUIRE(notes[i].phraseRow == (i % 4)); // 0, 1, 2, 3
    }
}

TEST_CASE("B5.4 KIL clamped", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::KIL, 0x0F);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    
    auto noteOns = host.noteOnsForTrack(0);
    auto noteOffs = host.noteOffsForTrack(0);
    REQUIRE(noteOns.size() >= 1);
    REQUIRE(noteOffs.size() >= 1);
    
    // 6-tick groove means clamps to 5
    REQUIRE(noteOffs[0].sampleTime == noteOns[0].sampleTime + 5 * 1000);
}

TEST_CASE("B5.6 HOP clamped", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::HOP, 0x0F);
    for (int r = 1; r < 16; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 2);
    REQUIRE(notes[0].phraseRow == 0);
    REQUIRE(notes[1].phraseRow == 15);
}

TEST_CASE("B5.7 HOP interaction with PLAY_CHAIN", "[fx]") {
    OfflineHost host;
    for (int r = 0; r < 16; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    setFx(host.sequencer(), 0, 3, 0, FxCmd::HOP, 0);
    
    setChain(host.sequencer(), 0, 0, 0);
    setChain(host.sequencer(), 0, 1, 1);
    
    host.push(playChain(0, 0));
    host.renderSeconds(2.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() > 5);
    for (size_t i = 0; i < notes.size(); ++i) {
        REQUIRE(notes[i].phraseRow == (i % 4)); // 0, 1, 2, 3
        REQUIRE(notes[i].chainRow == 0);
    }
}
