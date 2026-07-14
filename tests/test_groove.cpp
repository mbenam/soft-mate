#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("B5.9 Swing groove", "[groove]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    seq.grooves[0].length = 16;
    for(int i=0; i<16; ++i) seq.grooves[0].steps[i] = (i % 2 == 0) ? 8 : 4;
    
    for(int r = 0; r < 8; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(2.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 4);
    REQUIRE(notes[1].sampleTime - notes[0].sampleTime == 8000);
    REQUIRE(notes[2].sampleTime - notes[1].sampleTime == 4000);
    REQUIRE(notes[3].sampleTime - notes[2].sampleTime == 8000);
}

TEST_CASE("B5.8 Groove length limits", "[groove]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    seq.grooves[0].length = 18; // > 16
    for (int i = 0; i < 16; ++i) seq.grooves[0].steps[i] = 3; 
    
    for (int r = 0; r < 16; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(2.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 16);
    for (size_t i = 1; i < 16; ++i) {
        REQUIRE(notes[i].sampleTime - notes[i-1].sampleTime == 3000); // 3 * 1000
    }
}

TEST_CASE("B5.10 Groove index advance", "[groove]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    seq.grooves[0].length = 3;
    seq.grooves[0].steps[0] = 5;
    seq.grooves[0].steps[1] = 5;
    seq.grooves[0].steps[2] = 5;
    
    for (int r = 0; r < 5; ++r) setStep(host.sequencer(), 0, r, 60, 100, 0);
    
    host.push(playPhrase(0, 0, 0));
    host.render(25000); // Exactly 25 ticks (5 rows) at 1000 frames/tick
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 5);
    
    REQUIRE(host.engine().getStateForInit().playGrooveIndex[0] == 2); // 0, 1, 2, 0, 1 -> ending at 2 since 5 notes = 5 advances, 5 % 3 = 2
}

TEST_CASE("B5.11 Groove index bounds", "[groove]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    seq.grooves[0].length = 3;
    
    // Artificially set state to 15
    auto& state = host.engine().getStateForInit();
    state.playGrooveIndex[0] = 15;
    
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(0.1);
    
    // The bounds check happens at doTick(). Since length is 3, 15 is invalid and should snap to 0.
    // Then it advances by 1, so it should be 1 after the first row finishes playing, or at least bounded.
    REQUIRE(host.engine().getStateForInit().playGrooveIndex[0] < 3);
}
