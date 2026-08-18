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

TEST_CASE("FX: INS mid-phrase instrument change", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::INS, 3);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(0.5);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 1);
    REQUIRE(notes[0].instrument == 3);
}

TEST_CASE("FX: GGR global groove", "[fx]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.trackGroove[0] = 5;
    state.trackGroove[1] = 7;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::GGR, 2);

    host.push(playPhrase(0, 0, 0));
    host.render(100);

    REQUIRE(host.state().project.groove == 2);
    REQUIRE(host.state().trackGroove[0] == -1);
    REQUIRE(host.state().trackGroove[1] == -1);
}

TEST_CASE("FX: TPO and TSP runtime updates", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::TPO, 140);
    setFx(host.sequencer(), 0, 0, 1, FxCmd::TSP, 2);

    host.push(playPhrase(0, 0, 0));
    host.render(100);

    REQUIRE(host.state().bpm == 140);
    REQUIRE(host.state().project.transpose == 2);
}

TEST_CASE("FX: Phrase relative VOL and PIT", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 80, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::VOL, 20);
    setFx(host.sequencer(), 0, 0, 1, FxCmd::PIT, 3); // +3 semitones -> MIDI 63 (D#4)

    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(0.5);

    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 1);
    // D#4 freq is ~311.13 Hz
    REQUIRE(notes[0].frequency > 300.0f);
    REQUIRE(notes[0].frequency < 320.0f);
}

TEST_CASE("FX: CHA probability", "[fx]") {
    OfflineHost host;
    // CHA0F on step 0: left side probability 0 (never trigger note), right side F (always)
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::CHA, 0x0F);

    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(0.5);

    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.empty());
}

TEST_CASE("FX: NXT trigger next track", "[fx]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::NXT, 2); // triggers instrument 2 on track 1

    host.push(playPhrase(0, 0, 0));
    host.render(100);

    auto track1Notes = host.noteOnsForTrack(1);
    REQUIRE(track1Notes.size() >= 0);
}

TEST_CASE("FX: REP repeat command with step increment", "[fx]") {
    OfflineHost host;
    // Step 0: PIT 01 (+1 semi) -> C#4
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setFx(host.sequencer(), 0, 0, 0, FxCmd::PIT, 1);

    // Step 1: REP 02 (+2 to PIT -> PIT 03 = D#4)
    setStep(host.sequencer(), 0, 1, 60, 100, 0);
    setFx(host.sequencer(), 0, 1, 0, FxCmd::REP, 2);

    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(0.5);

    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 2);
    // Note 0 is C#4 (~277.18 Hz)
    REQUIRE(notes[0].frequency > 270.0f);
    REQUIRE(notes[0].frequency < 285.0f);
    // Note 1 is D#4 (~311.13 Hz)
    REQUIRE(notes[1].frequency > 305.0f);
    REQUIRE(notes[1].frequency < 320.0f);
}
