#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include "ui/screens/groove/GrooveScreen.h"
#include "ui/UiCommands.h"
#include "engine/CommandRing.h"

using namespace m8::test;
using namespace m8::engine;
using namespace m8::ui;
using namespace m8::ui::groove;

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

TEST_CASE("Groove default values and 2-step loop", "[groove]") {
    Sequencer seq;
    REQUIRE(seq.grooves[0].steps[0] == 6);
    REQUIRE(seq.grooves[0].steps[1] == 6);
    REQUIRE(seq.grooves[0].steps[2] == 0xFF);
    REQUIRE(seq.grooves[0].length == 2);

    REQUIRE(seq.grooves[1].steps[0] == 7);
    REQUIRE(seq.grooves[1].steps[1] == 5);
    REQUIRE(seq.grooves[1].steps[2] == 0xFF);
    REQUIRE(seq.grooves[1].length == 2);
}

TEST_CASE("Groove UI: Dual-row swing pair editing", "[groove]") {
    Sequencer seq;
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};

    int currentGroove = 0;
    int cursor_x = 0;
    int cursor_y = 0;
    uint8_t lastVal = 6;
    bool arrowPressed = false;

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // Row 0 EDIT+UP: 6/6 -> 7/5
    ev.key.key = SDLK_UP;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 7);
    REQUIRE(seq.grooves[0].steps[1] == 5);

    // Row 0 EDIT+UP: 7/5 -> 8/4
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 8);
    REQUIRE(seq.grooves[0].steps[1] == 4);

    // Row 0 EDIT+DOWN: 8/4 -> 7/5
    ev.key.key = SDLK_DOWN;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 7);
    REQUIRE(seq.grooves[0].steps[1] == 5);

    // Move to row 1
    cursor_y = 1;
    // Row 1 EDIT+UP: 7/5 -> 6/6 (row 1 increments to 6, row 0 decrements to 6)
    ev.key.key = SDLK_UP;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 6);
    REQUIRE(seq.grooves[0].steps[1] == 6);
}

TEST_CASE("Groove UI: Single step adjustment", "[groove]") {
    Sequencer seq;
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};

    int currentGroove = 0;
    int cursor_x = 0;
    int cursor_y = 0;
    uint8_t lastVal = 6;
    bool arrowPressed = false;

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // EDIT+RIGHT on row 0: increments only row 0 without modifying row 1
    ev.key.key = SDLK_RIGHT;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 7);
    REQUIRE(seq.grooves[0].steps[1] == 6);

    // EDIT+LEFT on row 0: decrements only row 0
    ev.key.key = SDLK_LEFT;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 6);
    REQUIRE(seq.grooves[0].steps[1] == 6);
}

TEST_CASE("Groove UI: PPQ column scaling", "[groove]") {
    Sequencer seq;
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};

    int currentGroove = 0;
    int cursor_x = 1; // PPQ column
    int cursor_y = 0;
    uint8_t lastVal = 6;
    bool arrowPressed = false;

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // Initially 24 PPQ (steps 6, 6)
    // Scale up: 24 -> 48 PPQ (steps 12, 12)
    ev.key.key = SDLK_RIGHT;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 12);
    REQUIRE(seq.grooves[0].steps[1] == 12);

    // Scale up again: 48 -> 96 PPQ (steps 24, 24)
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 24);
    REQUIRE(seq.grooves[0].steps[1] == 24);

    // Scale up again: 96 -> 192 PPQ (steps 48, 48)
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 48);
    REQUIRE(seq.grooves[0].steps[1] == 48);

    // Scale down: 192 -> 96 PPQ (steps 24, 24)
    ev.key.key = SDLK_LEFT;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[0].steps[0] == 24);
    REQUIRE(seq.grooves[0].steps[1] == 24);
}

TEST_CASE("Groove UI: Option groove switching and delete", "[groove]") {
    Sequencer seq;
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};

    int currentGroove = 0;
    int cursor_x = 0;
    int cursor_y = 0;
    uint8_t lastVal = 6;
    bool arrowPressed = false;

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // OPT+RIGHT: switches to groove 1
    ev.key.key = SDLK_RIGHT;
    HandleGrooveInput(ev, false, true, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(currentGroove == 1);

    // OPT+DOWN: jumps +16 to groove 17
    ev.key.key = SDLK_DOWN;
    HandleGrooveInput(ev, false, true, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(currentGroove == 17);

    // Delete step on row 0 of groove 17
    ev.key.key = SDLK_DELETE;
    HandleGrooveInput(ev, true, false, false, arrowPressed, seq, currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[17].steps[0] == 0xFF);

    // Tap edit release to restore
    HandleGrooveEditRelease(seq.grooves[17], currentGroove, cursor_x, cursor_y, lastVal, sink);
    REQUIRE(seq.grooves[17].steps[0] == 6);
}
