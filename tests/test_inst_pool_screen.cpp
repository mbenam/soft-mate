// INSTRUMENT POOL screen: the pool as a jump list.
//
// Selecting a row opens that instrument on the INSTRUMENT screen. The catch is
// that X is also the pool's edit modifier (X+UP/DOWN adjusts the value under
// the cursor), so "select" has to be an X *tap* -- distinguished from an edit
// gesture by `arrowPressedDuringEdit`, which main.cpp checks before calling
// HandleInstPoolEditRelease. IP3/IP4 pin that flag contract, since the guard
// itself lives in main.cpp and is covered end to end by
// tests/ui/project_inst_pool.m8script.

#include <catch2/catch_test_macros.hpp>
#include "ui/screens/inst_pool/InstPoolScreen.h"
#include "ui/UiCommands.h"
#include "ui/ViewManager.h"
#include "engine/CommandRing.h"
#include "engine/Engine.h"

using namespace m8::ui;
using namespace m8::ui::inst_pool;
using namespace m8::engine;

namespace {

struct TestPoolContext {
    CommandRing<EngineCommand, 1024> commandRing;
    CommandSink commandSink{commandRing};
    EngineState engineState;
    ViewManager viewManager;
    int cursorX = 0;
    int cursorY = 0;
    int currentInstIndex = 0;
    int eqBank = 0;
    bool arrowPressedDuringEdit = false;

    TestPoolContext() { viewManager.setCoords(3, 2); }   // start on INST_POOL

    void sendKeyDown(SDL_Keycode key, bool editHeld) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = key;
        HandleInstPoolInput(ev, editHeld, arrowPressedDuringEdit, engineState,
                            cursorX, cursorY, commandSink, viewManager, currentInstIndex);
    }
};

} // namespace

TEST_CASE("IP1 ENTER on a pool row opens that instrument", "[inst_pool]") {
    TestPoolContext ctx;
    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);

    for (int i = 0; i < 5; ++i) ctx.sendKeyDown(SDLK_DOWN, false);
    REQUIRE(ctx.cursorY == 5);

    ctx.sendKeyDown(SDLK_RETURN, false);

    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INSTRUMENT);
    REQUIRE(ctx.viewManager.getCol() == 3);
    REQUIRE(ctx.viewManager.getRow() == 0);
    REQUIRE(ctx.currentInstIndex == 5);
}

TEST_CASE("IP2 X tap opens the instrument under the cursor", "[inst_pool]") {
    TestPoolContext ctx;
    ctx.cursorY = 0x2A;

    SECTION("from the name column") {
        ctx.cursorX = 0;
        HandleInstPoolEditRelease(ctx.cursorX, ctx.cursorY, ctx.engineState,
                                  ctx.viewManager, ctx.currentInstIndex, ctx.eqBank);
        REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INSTRUMENT);
        REQUIRE(ctx.currentInstIndex == 0x2A);
    }

    // The row identifies the instrument, so any column selects it -- the send
    // columns are not a different target.
    SECTION("from a send column") {
        ctx.cursorX = 3;
        HandleInstPoolEditRelease(ctx.cursorX, ctx.cursorY, ctx.engineState,
                                  ctx.viewManager, ctx.currentInstIndex, ctx.eqBank);
        REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INSTRUMENT);
        REQUIRE(ctx.currentInstIndex == 0x2A);
    }
}

TEST_CASE("IP3 an edit gesture flags itself so the release does not navigate", "[inst_pool]") {
    // X+UP edits the value under the cursor. It must also mark
    // arrowPressedDuringEdit, or main.cpp's release guard would let a value
    // edit throw the user onto the INSTRUMENT screen.
    TestPoolContext ctx;
    ctx.cursorY = 3;
    ctx.cursorX = 0;
    REQUIRE(ctx.engineState.instruments[3].type == InstType::INST_SAMPLER);

    ctx.sendKeyDown(SDLK_UP, /*editHeld=*/true);

    REQUIRE(ctx.arrowPressedDuringEdit);
    REQUIRE(ctx.engineState.instruments[3].type == InstType::INST_MACROSYN);  // the edit ran
    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);         // and did not navigate
}

TEST_CASE("IP4 horizontal arrows during an edit hold also suppress the jump", "[inst_pool]") {
    // The pool has no horizontal edit action, so X+LEFT/RIGHT changes nothing --
    // but it is still a hold, not a tap, and must not read as "open this".
    TestPoolContext ctx;
    ctx.sendKeyDown(SDLK_RIGHT, /*editHeld=*/true);

    REQUIRE(ctx.arrowPressedDuringEdit);
    REQUIRE(ctx.cursorX == 0);   // unchanged: horizontal movement needs no edit hold
    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);
}

TEST_CASE("IP5 an out-of-range cursor never moves the view", "[inst_pool]") {
    TestPoolContext ctx;
    ctx.currentInstIndex = 7;

    HandleInstPoolEditRelease(0, -1, ctx.engineState, ctx.viewManager, ctx.currentInstIndex, ctx.eqBank);
    HandleInstPoolEditRelease(0, 128, ctx.engineState, ctx.viewManager, ctx.currentInstIndex, ctx.eqBank);

    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);
    REQUIRE(ctx.currentInstIndex == 7);
}

TEST_CASE("IP6 the EQ column opens the EQ editor, not the instrument", "[inst_pool]") {
    // The EQ column names something other than this instrument's own
    // parameters -- a shared bank -- so a tap there is a doorway to the EQ
    // editor (EQ_SPEC.md step 7).
    TestPoolContext ctx;
    ctx.cursorY = 9;
    ctx.cursorX = 5;                       // EQ column
    ctx.engineState.instruments[9].sampler.eq = 6;

    HandleInstPoolEditRelease(ctx.cursorX, ctx.cursorY, ctx.engineState,
                              ctx.viewManager, ctx.currentInstIndex, ctx.eqBank);

    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::EQ);
    REQUIRE(ctx.eqBank == 6);              // the editor opens that bank
    REQUIRE(ctx.currentInstIndex == 9);
}
