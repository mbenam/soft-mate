#include <catch2/catch_test_macros.hpp>
#include "ui/screens/scale/ScaleScreen.h"
#include "ui/screens/scale/ScaleScreenLayout.h"
#include "ui/Renderer.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"
#include "engine/EngineStateUpdater.h"
#include "ui/UiCommands.h"
#include "io/ScaleIO.h"
#include <filesystem>

using namespace m8::ui;
using namespace m8::ui::scale;
using namespace m8::engine;

struct TestScaleContext {
    CommandRing<EngineCommand, 1024> commandRing;
    CommandSink commandSink{commandRing};
    EngineState engineState;
    CursorId cursorId = CursorId::KEY;
    int nameCharIndex = 0;
    int currentScaleIndex = 0;
    bool editHeld = false;
    bool arrowPressed = false;

    void sendKeyDown(SDL_Keycode key, bool edit) {
        editHeld = edit;
        arrowPressed = false;
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = key;
        HandleScaleInput(ev, editHeld, arrowPressed, engineState, currentScaleIndex, cursorId, nameCharIndex, commandSink);
    }
};

TEST_CASE("ScaleScreen: KEY editing rotates root note and updates rows", "[scale]") {
    TestScaleContext ctx;
    ctx.cursorId = CursorId::KEY;
    ctx.engineState.scales[0].key = 0; // C

    // Pressing RIGHT (+1) changes KEY from C to C# (1)
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.scales[0].key == 1);

    EngineCommand cmd;
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::SCALE_KEY);
    REQUIRE(cmd.value == 1);

    // Set KEY to E (4)
    ctx.engineState.scales[0].key = 4;

    Renderer renderer;
    renderer.resetVram();
    RenderScaleScreen(renderer, ctx.engineState, 0, CursorId::KEY);

    const auto& vram = renderer.getVram();

    // Row 2 should have "KEY    E"
    std::string row2;
    for (int c = 0; c < 40; ++c) row2 += (vram[2][c].ch ? vram[2][c].ch : ' ');
    REQUIRE(row2.find("KEY") != std::string::npos);
    REQUIRE(row2.find("E") != std::string::npos);

    // Rows 4..15 should start at E and rotate through the 12 chromatic intervals
    const char* expectedNotes[12] = {"E", "F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#"};
    for (int i = 0; i < 12; ++i) {
        std::string rowText;
        for (int c = 0; c < 40; ++c) rowText += (vram[4 + i][c].ch ? vram[4 + i][c].ch : ' ');
        REQUIRE(rowText.find(expectedNotes[i]) == 0);
    }
}

TEST_CASE("ScaleScreen: EN and OFFSET display and toggle behavior", "[scale]") {
    TestScaleContext ctx;
    ctx.engineState.scales[0].key = 4; // E
    
    // Disable interval 0 (E)
    ctx.engineState.scales[0].notes[0].enable = false;
    // Enable interval 1 (F)
    ctx.engineState.scales[0].notes[1].enable = true;
    ctx.engineState.scales[0].notes[1].offset = 0.0f;

    Renderer renderer;
    renderer.resetVram();
    RenderScaleScreen(renderer, ctx.engineState, 0, CursorId::KEY);
    const auto& vram = renderer.getVram();

    // Row 4 (E, disabled) should show "--  --.--"
    std::string row4;
    for (int c = 0; c < 40; ++c) row4 += (vram[4][c].ch ? vram[4][c].ch : ' ');
    REQUIRE(row4.find("--") != std::string::npos);
    REQUIRE(row4.find("--.--") != std::string::npos);

    // Row 5 (F, enabled) should show "ON" and "00.00"
    std::string row5;
    for (int c = 0; c < 40; ++c) row5 += (vram[5][c].ch ? vram[5][c].ch : ' ');
    REQUIRE(row5.find("ON") != std::string::npos);
    REQUIRE(row5.find("00.00") != std::string::npos);

    // Test EN toggle on interval 0
    ctx.cursorId = NoteEnCursor(0);
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.scales[0].notes[0].enable == true);

    EngineCommand cmd;
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::SCALE_NOTE_EN);
    REQUIRE(cmd.value == 1);
    REQUIRE(cmd.row == 0);
}

TEST_CASE("ScaleScreen: Navigation and TUNE editing", "[scale]") {
    TestScaleContext ctx;
    ctx.cursorId = CursorId::KEY;

    // Moving DOWN from KEY reaches NOTE_EN_0
    ctx.sendKeyDown(SDLK_DOWN, false);
    REQUIRE(ctx.cursorId == NoteEnCursor(0));

    // Moving RIGHT reaches NOTE_OFFSET_0
    ctx.sendKeyDown(SDLK_RIGHT, false);
    REQUIRE(ctx.cursorId == NoteOffsetCursor(0));

    // Moving LEFT reaches NOTE_EN_0
    ctx.sendKeyDown(SDLK_LEFT, false);
    REQUIRE(ctx.cursorId == NoteEnCursor(0));

    // Move to NOTE_EN_11 then DOWN to reach TUNE_INT
    ctx.cursorId = NoteEnCursor(11);
    ctx.sendKeyDown(SDLK_DOWN, false);
    REQUIRE(ctx.cursorId == CursorId::TUNE_INT);

    // Moving RIGHT from TUNE_INT reaches TUNE_DEC
    ctx.sendKeyDown(SDLK_RIGHT, false);
    REQUIRE(ctx.cursorId == CursorId::TUNE_DEC);

    // Test TUNE_INT editing (whole part)
    ctx.cursorId = CursorId::TUNE_INT;
    ctx.engineState.scales[0].tune = 440.30f;

    // X + UP steps by 10s: 440.30 -> 450.30
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(std::abs(ctx.engineState.scales[0].tune - 450.30f) < 1e-2);

    // X + RIGHT steps by 1s: 450.30 -> 451.30
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(std::abs(ctx.engineState.scales[0].tune - 451.30f) < 1e-2);

    // Test TUNE_DEC editing (fractional part)
    ctx.cursorId = CursorId::TUNE_DEC;
    // X + UP steps by 10s: 451.30 -> 451.40
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(std::abs(ctx.engineState.scales[0].tune - 451.40f) < 1e-2);

    // X + RIGHT steps by 1s: 451.40 -> 451.41
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(std::abs(ctx.engineState.scales[0].tune - 451.41f) < 1e-2);
}

TEST_CASE("ScaleScreen: NAME character navigation and param updater", "[scale]") {
    TestScaleContext ctx;
    ctx.cursorId = CursorId::NAME;
    ctx.nameCharIndex = 0;

    // Moving RIGHT on NAME advances nameCharIndex
    ctx.sendKeyDown(SDLK_RIGHT, false);
    REQUIRE(ctx.nameCharIndex == 1);

    ctx.sendKeyDown(SDLK_RIGHT, false);
    REQUIRE(ctx.nameCharIndex == 2);

    // Moving LEFT on NAME decrements nameCharIndex
    ctx.sendKeyDown(SDLK_LEFT, false);
    REQUIRE(ctx.nameCharIndex == 1);

    // Test SCALE_NAME via PushParam
    PushParam(ctx.commandSink, ctx.engineState, ParamID::SCALE_NAME, 'M', 0, 0);
    REQUIRE(ctx.engineState.scales[0].name[0] == 'M');
}

TEST_CASE("ScaleScreen: ScaleIO save and load roundtrip", "[scale]") {
    m8::engine::Scale scale;
    std::strncpy(scale.name, "TEST SCALE------", 16);
    scale.name[16] = '\0';
    scale.key = 4; // E
    scale.tune = 442.50f;
    for (int i = 0; i < 12; ++i) {
        scale.notes[i].enable = (i % 2 == 0);
        scale.notes[i].offset = (i % 2 == 0) ? static_cast<float>(i) * 0.5f : 0.0f;
    }

    std::string outPath, err;
    std::string testDir = "test_scales_temp";
    bool saved = m8::io::saveScale(testDir, scale, outPath, err);
    REQUIRE(saved);
    REQUIRE(!outPath.empty());

    m8::engine::Scale loadedScale;
    bool loaded = m8::io::loadScale(outPath, loadedScale, err);
    REQUIRE(loaded);
    REQUIRE(std::string(loadedScale.name) == "TEST SCALE------");
    for (int i = 0; i < 12; ++i) {
        REQUIRE(loadedScale.notes[i].enable == (i % 2 == 0));
        if (loadedScale.notes[i].enable) {
            REQUIRE(std::abs(loadedScale.notes[i].offset - (static_cast<float>(i) * 0.5f)) < 1e-2);
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
}

TEST_CASE("ScaleScreen: CMD_LOAD and CMD_SAVE triggers FileBrowser modal", "[scale]") {
    TestScaleContext ctx;
    FileBrowser fb;
    ViewManager vm;
    m8::ui::scale::ScaleBrowserMode sbm = m8::ui::scale::ScaleBrowserMode::NONE;
    m8::ui::scale::ScaleActionState actions{fb, vm, sbm};

    // Navigate DOWN to CMD_LOAD
    ctx.cursorId = CursorId::NAME;
    ctx.sendKeyDown(SDLK_DOWN, false);
    REQUIRE(ctx.cursorId == CursorId::CMD_LOAD);

    // Trigger LOAD
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    HandleScaleInput(ev, false, ctx.arrowPressed, ctx.engineState, 0, ctx.cursorId, ctx.nameCharIndex, ctx.commandSink, &actions);
    REQUIRE(sbm == m8::ui::scale::ScaleBrowserMode::LOAD);
    REQUIRE(vm.getCurrentView() == ViewType::FILE_BROWSER);
    REQUIRE(fb.getMode() == FileBrowser::Mode::FILES);
    vm.popModal();

    // Move RIGHT to CMD_SAVE
    ctx.cursorId = CursorId::CMD_LOAD;
    ctx.sendKeyDown(SDLK_RIGHT, false);
    REQUIRE(ctx.cursorId == CursorId::CMD_SAVE);

    // Trigger SAVE
    HandleScaleInput(ev, false, ctx.arrowPressed, ctx.engineState, 0, ctx.cursorId, ctx.nameCharIndex, ctx.commandSink, &actions);
    REQUIRE(sbm == m8::ui::scale::ScaleBrowserMode::SAVE_DIR);
    REQUIRE(vm.getCurrentView() == ViewType::FILE_BROWSER);
    REQUIRE(fb.getMode() == FileBrowser::Mode::DIRECTORY);

    // Test that OPTION key (SDLK_Z) cancels file browser
    SDL_Event optEv{};
    optEv.type = SDL_EVENT_KEY_DOWN;
    optEv.key.key = SDLK_Z;
    REQUIRE(fb.handleInput(optEv, false) == FileBrowser::Result::CANCELLED);
}
