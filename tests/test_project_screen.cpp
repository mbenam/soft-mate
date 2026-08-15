#include <catch2/catch_test_macros.hpp>
#include "ui/screens/project/ProjectScreen.h"
#include "ui/UiCommands.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"
#include "ui/ViewManager.h"
#include "ui/FileBrowser.h"
#include "ui/ConfirmationDialog.h"
#include "ui/CharPicker.h"
#include "io/SongIO.h"
#include "support/OfflineHost.h"
#include <cstring>

using namespace m8::ui;
using namespace m8::ui::project;
using namespace m8::engine;
using namespace m8::test;

struct TestProjectContext {
    CommandRing<EngineCommand, 1024> commandRing;
    CommandSink commandSink{commandRing};
    EngineState engineState;
    Sequencer sequencer;
    ViewManager viewManager;
    FileBrowser fileBrowser;
    ConfirmationDialog confirmDialog;
    CharPicker charPicker;
    bool browserForSongLoad = false;
    bool textInputActive = false;
    std::string textInputBuffer;
    std::string textInputPrompt;
    std::string currentSongPath;
    m8::io::LoadResult currentLoadResult;
    std::string missingSamplesMsg;
    int nameCharIndex = 0;
    CursorId cursorId = CursorId::TEMPO_INT;
    bool arrowPressedDuringEdit = false;

    ProjectActionState getActions() {
        return ProjectActionState{
            browserForSongLoad, fileBrowser, viewManager, textInputActive,
            textInputBuffer, textInputPrompt, currentSongPath, currentLoadResult,
            sequencer, missingSamplesMsg, confirmDialog, charPicker, nameCharIndex
        };
    }

    void sendKeyDown(SDL_Keycode key, bool editHeld) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = key;
        auto actions = getActions();
        HandleProjectInput(ev, editHeld, arrowPressedDuringEdit,
                           engineState, cursorId, nameCharIndex, commandSink, actions);
    }

    void sendKeyUp(SDL_Keycode key) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_UP;
        ev.key.key = key;
        HandleProjectKeyUp(ev, engineState, cursorId, commandSink);
    }
};

TEST_CASE("ProjectScreen: Whole tempo adjustments (TEMPO_INT)", "[project_tempo]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::TEMPO_INT;
    ctx.engineState.bpm = 128;
    ctx.engineState.bpm_frac = 0;

    SECTION("Up/Down adjustments by 10s") {
        // Holding X and pressing UP increases by 10
        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm == 138);

        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm == 148);

        // Holding X and pressing DOWN decreases by 10
        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm == 138);

        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm == 128);
    }

    SECTION("Left/Right adjustments by 1") {
        // Holding X and pressing RIGHT increases by 1
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 129);

        // Holding X and pressing LEFT decreases by 1
        ctx.sendKeyDown(SDLK_LEFT, true);
        REQUIRE(ctx.engineState.bpm == 128);
    }

    SECTION("Clamping at upper and lower boundaries") {
        ctx.engineState.bpm = 395;
        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm == 400);

        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm == 400);

        ctx.engineState.bpm = 25;
        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm == 20);

        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm == 20);
    }
}

TEST_CASE("ProjectScreen: Decimal tempo adjustments (TEMPO_DEC)", "[project_tempo]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::TEMPO_DEC;
    ctx.engineState.bpm = 128;
    ctx.engineState.bpm_frac = 0;

    SECTION("Up/Down adjustments by 10s") {
        // Holding X and pressing UP increases bpm_frac by 10
        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm_frac == 10);

        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm_frac == 20);

        // Holding X and pressing DOWN decreases bpm_frac by 10
        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm_frac == 10);

        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm_frac == 0);
    }

    SECTION("Left/Right adjustments by 1") {
        // Holding X and pressing RIGHT increases bpm_frac by 1
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm_frac == 1);

        // Holding X and pressing LEFT decreases bpm_frac by 1
        ctx.sendKeyDown(SDLK_LEFT, true);
        REQUIRE(ctx.engineState.bpm_frac == 0);
    }

    SECTION("Clamping at 0 and 99") {
        ctx.engineState.bpm_frac = 95;
        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm_frac == 99);

        ctx.sendKeyDown(SDLK_UP, true);
        REQUIRE(ctx.engineState.bpm_frac == 99);

        ctx.engineState.bpm_frac = 5;
        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm_frac == 0);

        ctx.sendKeyDown(SDLK_DOWN, true);
        REQUIRE(ctx.engineState.bpm_frac == 0);
    }
}

TEST_CASE("ProjectScreen: Tempo nudge (< >)", "[project_tempo]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::TEMPO_NUDGE;
    ctx.engineState.bpm = 128;

    SECTION("Nudge increase with RIGHT and return on key release") {
        // Holding X and pressing RIGHT increases bpm
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 129);

        // Key repeat while holding RIGHT continues increasing bpm
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 130);

        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 131);

        // Releasing RIGHT resets bpm to starting 128
        ctx.sendKeyUp(SDLK_RIGHT);
        REQUIRE(ctx.engineState.bpm == 128);
    }

    SECTION("Nudge decrease with LEFT and return on key release") {
        // Holding X and pressing LEFT decreases bpm
        ctx.sendKeyDown(SDLK_LEFT, true);
        REQUIRE(ctx.engineState.bpm == 127);

        // Key repeat while holding LEFT continues decreasing bpm
        ctx.sendKeyDown(SDLK_LEFT, true);
        REQUIRE(ctx.engineState.bpm == 126);

        // Releasing LEFT resets bpm to starting 128
        ctx.sendKeyUp(SDLK_LEFT);
        REQUIRE(ctx.engineState.bpm == 128);
    }

    SECTION("Nudge reset on releasing X") {
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 129);

        // Releasing X resets bpm
        ctx.sendKeyUp(SDLK_X);
        REQUIRE(ctx.engineState.bpm == 128);
    }

    SECTION("Nudge reset on navigation away") {
        ctx.sendKeyDown(SDLK_RIGHT, true);
        REQUIRE(ctx.engineState.bpm == 129);

        // Moving cursor away without edit
        ctx.sendKeyDown(SDLK_LEFT, false);
        REQUIRE(ctx.engineState.bpm == 128);
        REQUIRE(ctx.cursorId == CursorId::TEMPO_DEC);
    }
}

TEST_CASE("ProjectScreen: TRANSPOSE editing commits on edit release", "[project_transpose]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::TRANSPOSE;
    ctx.engineState.project.transpose = 0;

    // Holding X and adjusting transpose
    ctx.sendKeyDown(SDLK_UP, true); // +12
    REQUIRE(ctx.engineState.project.transpose == 12);

    ctx.sendKeyDown(SDLK_RIGHT, true); // +1
    REQUIRE(ctx.engineState.project.transpose == 13);

    // Verify command is NOT sent to engine yet while edit is held
    EngineCommand cmd;
    bool hadCommand = ctx.commandRing.pop(cmd);
    REQUIRE_FALSE(hadCommand);

    // Releasing X commits the transpose to the engine command ring
    ctx.sendKeyUp(SDLK_X);
    bool receivedCommand = ctx.commandRing.pop(cmd);
    REQUIRE(receivedCommand);
    REQUIRE(cmd.type == CommandType::UPDATE_PARAM);
    REQUIRE(cmd.paramId == ParamID::PROJ_TRANSPOSE);
    REQUIRE(static_cast<int8_t>(cmd.value) == 13);

    // Apply command to engine state
    EngineState engState;
    EngineStateUpdater::applyParameterUpdate(engState, cmd);
    REQUIRE(engState.project.transpose == 13);

    // Now test octaves down
    ctx.sendKeyDown(SDLK_DOWN, true); // -12 -> 1
    REQUIRE(ctx.engineState.project.transpose == 1);
    ctx.sendKeyDown(SDLK_LEFT, true); // -1 -> 0
    REQUIRE(ctx.engineState.project.transpose == 0);

    ctx.sendKeyUp(SDLK_X);
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::PROJ_TRANSPOSE);
    REQUIRE(static_cast<int8_t>(cmd.value) == 0);

    // Verify notice timer is set
    REQUIRE(getTransposeNoticeUntil() > 0);

    // Test 8-bit wrapping: 00 -> LEFT -> FF (-1) -> RIGHT -> 00 (0); 7F (127) -> RIGHT -> 80 (-128)
    ctx.engineState.project.transpose = 0;
    ctx.sendKeyDown(SDLK_LEFT, true);
    REQUIRE(static_cast<uint8_t>(ctx.engineState.project.transpose) == 0xFF);
    REQUIRE(ctx.engineState.project.transpose == -1);

    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(static_cast<uint8_t>(ctx.engineState.project.transpose) == 0x00);
    REQUIRE(ctx.engineState.project.transpose == 0);

    ctx.engineState.project.transpose = 127;
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(static_cast<uint8_t>(ctx.engineState.project.transpose) == 0x80);
    REQUIRE(ctx.engineState.project.transpose == -128);

    ctx.sendKeyDown(SDLK_LEFT, true);
    REQUIRE(static_cast<uint8_t>(ctx.engineState.project.transpose) == 0x7F);
    REQUIRE(ctx.engineState.project.transpose == 127);
}

TEST_CASE("ProjectScreen: GLOBAL TRANSPOSE notice renders signed decimal and times out", "[project_transpose]") {
    Renderer renderer;
    EngineState engState;
    engState.project.transpose = 0x95; // -107 signed

    // Set notice active for future ticks
    setTransposeNoticeUntil(SDL_GetTicks() + 2500);
    RenderProjectScreen(renderer, engState, CursorId::TRANSPOSE);

    const auto& vram = renderer.getVram();
    // Check that row 3 has "TRANSPOSE     95" (exactly 2-char hex, NOT FFFFFF95)
    std::string row3;
    for (int c = 0; c < 40; ++c) {
        char ch = vram[3][c].ch;
        row3 += (ch ? ch : ' ');
    }
    REQUIRE(row3.find("TRANSPOSE     95") != std::string::npos);

    // Check that row 28 contains "GLOBAL TRANSPOSE: -107"
    bool foundNotice = false;
    for (int r = 0; r < 30; ++r) {
        std::string rowText;
        for (int c = 0; c < 40; ++c) {
            char ch = vram[r][c].ch;
            rowText += (ch ? ch : ' ');
        }
        if (rowText.find("GLOBAL TRANSPOSE: -107") != std::string::npos) {
            foundNotice = true;
            REQUIRE(r == 28);
            break;
        }
    }
    REQUIRE(foundNotice);

    // Expire the timer
    setTransposeNoticeUntil(0);
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::TRANSPOSE);

    bool foundAfterExpire = false;
    const auto& vramAfter = renderer.getVram();
    for (int r = 0; r < 30; ++r) {
        std::string rowText;
        for (int c = 0; c < 40; ++c) {
            char ch = vramAfter[r][c].ch;
            rowText += (ch ? ch : ' ');
        }
        if (rowText.find("GLOBAL TRANSPOSE") != std::string::npos) {
            foundAfterExpire = true;
            break;
        }
    }
    REQUIRE_FALSE(foundAfterExpire);
}

TEST_CASE("Engine: Global project transpose affects only TRANSP enabled instruments", "[project_transpose]") {
    m8::test::OfflineHost host;

    // Instrument 0: TRANSP ON (default)
    auto& st = host.engine().getStateForInit();
    st.instruments[0].type = InstType::INST_MACROSYN;
    st.instruments[0].macrosyn.transp = 1;

    // Instrument 1: TRANSP OFF (e.g. drum sample)
    st.instruments[1].type = InstType::INST_SAMPLER;
    st.instruments[1].sampler.transp = 0;

    // Set global project transpose to +12 semitones
    st.project.transpose = 12;

    // Track 0 plays Inst 0 (C-4, MIDI 60)
    // Track 1 plays Inst 1 (C-4, MIDI 60)
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // Phrase 0: Inst 0
    setStep(host.sequencer(), 1, 0, 60, 100, 1); // Phrase 1: Inst 1
    setChain(host.sequencer(), 0, 0, 0, 0);      // Chain 0: Phrase 0, tsp 0
    setChain(host.sequencer(), 1, 0, 1, 0);      // Chain 1: Phrase 1, tsp 0
    host.sequencer().song[0].tracks[0] = 0;
    host.sequencer().song[0].tracks[1] = 1;

    host.push(playSong(0));
    host.renderSeconds(1.0);

    auto notes0 = host.noteOnsForTrack(0);
    auto notes1 = host.noteOnsForTrack(1);

    REQUIRE(notes0.size() >= 1);
    REQUIRE(notes1.size() >= 1);

    float f_C4 = 440.0f * std::pow(2.0f, (60 - 69) / 12.0f); // 261.63 Hz
    float f_C5 = 440.0f * std::pow(2.0f, (72 - 69) / 12.0f); // 523.25 Hz

    // Track 0 (TRANSP ON) transposed from C-4 to C-5 (+12)
    REQUIRE(std::abs(notes0[0].frequency - f_C5) < 1e-2);

    // Track 1 (TRANSP OFF) stays at C-4 (+0)
    REQUIRE(std::abs(notes1[0].frequency - f_C4) < 1e-2);
}

TEST_CASE("ProjectScreen: GROOVE editing and commit on edit release", "[project_groove]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::GROOVE;
    ctx.engineState.project.groove = 0;

    // Pressing RIGHT (+1) -> groove becomes 1
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.project.groove == 1);

    // Pressing UP (+16) -> groove becomes 17 (0x11)
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(ctx.engineState.project.groove == 17);

    // Pressing UP (+16) again -> clamps at maximum 31 (0x1F)
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(ctx.engineState.project.groove == 31);

    // Pressing RIGHT at maximum stays at 31
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.project.groove == 31);

    // No command pushed while editing
    EngineCommand cmd;
    REQUIRE_FALSE(ctx.commandRing.pop(cmd));

    // Release X -> commits to engine
    ctx.sendKeyUp(SDLK_X);
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::PROJ_GROOVE);
    REQUIRE(cmd.value == 31);

    // Apply to engine state
    EngineState engState;
    EngineStateUpdater::applyParameterUpdate(engState, cmd);
    REQUIRE(engState.project.groove == 31);

    // Test stepping down and clamping at 0
    ctx.sendKeyDown(SDLK_DOWN, true); // 31 - 16 = 15
    REQUIRE(ctx.engineState.project.groove == 15);
    ctx.sendKeyDown(SDLK_DOWN, true); // 15 - 16 -> clamped at 0
    REQUIRE(ctx.engineState.project.groove == 0);
    ctx.sendKeyDown(SDLK_LEFT, true); // clamped at 0
    REQUIRE(ctx.engineState.project.groove == 0);

    ctx.sendKeyUp(SDLK_X);
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::PROJ_GROOVE);
    REQUIRE(cmd.value == 0);
}

TEST_CASE("ProjectScreen: GROOVE rendering displays DEFAULT for 00 and hides accent for non-zero", "[project_groove]") {
    Renderer renderer;
    EngineState engState;

    // When groove == 0: row 4 has "GROOVE        00DEFAULT"
    engState.project.groove = 0;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::GROOVE);
    const auto& vram0 = renderer.getVram();
    std::string row4_0;
    for (int c = 0; c < 40; ++c) {
        char ch = vram0[4][c].ch;
        row4_0 += (ch ? ch : ' ');
    }
    REQUIRE(row4_0.find("GROOVE        00DEFAULT") != std::string::npos);

    // When groove == 31 (0x1F): row 4 has "GROOVE        1F" and NOT "DEFAULT"
    engState.project.groove = 31;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::GROOVE);
    const auto& vram1F = renderer.getVram();
    std::string row4_1F;
    for (int c = 0; c < 40; ++c) {
        char ch = vram1F[4][c].ch;
        row4_1F += (ch ? ch : ' ');
    }
    REQUIRE(row4_1F.find("GROOVE        1F") != std::string::npos);
    REQUIRE(row4_1F.find("DEFAULT") == std::string::npos);
}

TEST_CASE("ProjectScreen: SCALE editing with 1s and 16s and delayed commit", "[project_scale]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::SCALE;
    ctx.engineState.project.scale = 0;

    // Holding X and pressing RIGHT steps by 1: 00 -> 01
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.project.scale == 1);

    // No command pushed while editing
    EngineCommand cmd;
    REQUIRE(!ctx.commandRing.pop(cmd));

    // Holding X and pressing UP steps by 16: 01 -> 17 (0x11)
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(ctx.engineState.project.scale == 17);

    // Set scale to 0xAA (170)
    ctx.engineState.project.scale = 0xAA;
    ctx.sendKeyUp(SDLK_X);

    // Releasing X pushes PROJ_SCALE command
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::PROJ_SCALE);
    REQUIRE(cmd.value == 0xAA);
}

TEST_CASE("ProjectScreen: SCALE rendering displays key signature accents", "[project_scale]") {
    Renderer renderer;
    EngineState engState;

    // When scale == 0: row 5 has "SCALE         00 C"
    engState.project.scale = 0x00;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::SCALE);
    const auto& vram0 = renderer.getVram();
    std::string row5_0;
    for (int c = 0; c < 40; ++c) {
        char ch = vram0[5][c].ch;
        row5_0 += (ch ? ch : ' ');
    }
    REQUIRE(row5_0.find("SCALE") != std::string::npos);
    REQUIRE(row5_0.find("00") != std::string::npos);
    REQUIRE(row5_0.find(" C") != std::string::npos);

    // ASSERTION CHANGED 2026-08-14. This used to set scale = 0xAA and require
    // the row to contain "A#1", pinning a renderer that derived the KEY from the
    // scale byte's low nibble (0xA -> A#). Measured on fw 6.5.2: that field is
    // the ACTIVE SCALE INDEX, not a key. Stepping it 00 -> 08 left the key at C
    // and moved the scale NAME to MINOR PENTATON, so the row reads
    // "<index> <key> <scale name>" with the key coming from elsewhere. Under the
    // old renderer scale 08 would have shown G# where the device shows C.
    //
    // So the key now tracks project.key, independently of the index.
    engState.project.scale = 0x08;
    engState.project.key   = 3;     // D#
    std::memcpy(engState.scales[8].name, "MINOR PENTATONIC", 16);
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::SCALE);
    const auto& vram8 = renderer.getVram();
    std::string row5_8;
    for (int c = 0; c < 40; ++c) {
        char ch = vram8[5][c].ch;
        row5_8 += (ch ? ch : ' ');
    }
    REQUIRE(row5_8.find("SCALE") != std::string::npos);
    REQUIRE(row5_8.find("08") != std::string::npos);
    REQUIRE(row5_8.find("D#") != std::string::npos);
    REQUIRE(row5_8.find("MINOR PENTATONIC") != std::string::npos);

    // The index must not leak into the key: 08 under the old renderer was G#.
    REQUIRE(row5_8.find("G#") == std::string::npos);
}

TEST_CASE("ProjectScreen: LIVE_QUANTIZE editing with 1s and 16s and delayed commit", "[project_quantize]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::LIVE_QUANTIZE;
    ctx.engineState.project.live_quantize = 0;

    // Holding X and pressing RIGHT steps by 1: 00 -> 01
    ctx.sendKeyDown(SDLK_RIGHT, true);
    REQUIRE(ctx.engineState.project.live_quantize == 1);

    // No command pushed while editing
    EngineCommand cmd;
    REQUIRE(!ctx.commandRing.pop(cmd));

    // Holding X and pressing UP steps by 16: 01 -> 17 (0x11)
    ctx.sendKeyDown(SDLK_UP, true);
    REQUIRE(ctx.engineState.project.live_quantize == 17);

    // Set quantize to 0xFF (255)
    ctx.engineState.project.live_quantize = 0xFF;
    ctx.sendKeyUp(SDLK_X);

    // Releasing X pushes PROJ_LIVE_QUANTIZE command
    REQUIRE(ctx.commandRing.pop(cmd));
    REQUIRE(cmd.paramId == ParamID::PROJ_LIVE_QUANTIZE);
    REQUIRE(cmd.value == 0xFF);
}

TEST_CASE("ProjectScreen: LIVE_QUANTIZE rendering displays CHAIN LEN for 00 and STEPS for non-zero", "[project_quantize]") {
    Renderer renderer;
    EngineState engState;

    // When live_quantize == 0: row 6 has "LIVE QUANTIZ  00CHAIN LEN"
    engState.project.live_quantize = 0x00;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::LIVE_QUANTIZE);
    const auto& vram0 = renderer.getVram();
    std::string row6_0;
    for (int c = 0; c < 40; ++c) {
        char ch = vram0[6][c].ch;
        row6_0 += (ch ? ch : ' ');
    }
    REQUIRE(row6_0.find("LIVE QUANTIZ") != std::string::npos);
    REQUIRE(row6_0.find("00") != std::string::npos);
    REQUIRE(row6_0.find("CHAIN LEN") != std::string::npos);

    // When live_quantize == 0x01: row 6 has "LIVE QUANTIZ  01STEPS" and NOT "CHAIN LEN"
    engState.project.live_quantize = 0x01;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::LIVE_QUANTIZE);
    const auto& vram01 = renderer.getVram();
    std::string row6_01;
    for (int c = 0; c < 40; ++c) {
        char ch = vram01[6][c].ch;
        row6_01 += (ch ? ch : ' ');
    }
    REQUIRE(row6_01.find("LIVE QUANTIZ") != std::string::npos);
    REQUIRE(row6_01.find("01") != std::string::npos);
    REQUIRE(row6_01.find("STEPS") != std::string::npos);
    REQUIRE(row6_01.find("CHAIN LEN") == std::string::npos);

    // When live_quantize == 0xFF: row 6 has "LIVE QUANTIZ  FFSTEPS"
    engState.project.live_quantize = 0xFF;
    renderer.resetVram();
    RenderProjectScreen(renderer, engState, CursorId::LIVE_QUANTIZE);
    const auto& vramFF = renderer.getVram();
    std::string row6_FF;
    for (int c = 0; c < 40; ++c) {
        char ch = vramFF[6][c].ch;
        row6_FF += (ch ? ch : ' ');
    }
    REQUIRE(row6_FF.find("LIVE QUANTIZ") != std::string::npos);
    REQUIRE(row6_FF.find("FF") != std::string::npos);
    REQUIRE(row6_FF.find("STEPS") != std::string::npos);
}

TEST_CASE("ProjectScreen: INST POOL entry jumps to the INST POOL view", "[project_inst_pool]") {
    TestProjectContext ctx;
    ctx.cursorId = CursorId::INST_POOL;
    ctx.viewManager.setCoords(0, 0);
    REQUIRE(ctx.viewManager.getCurrentView() == ViewType::SONG);

    SECTION("ENTER jumps to INST POOL (view coords 3,2)") {
        ctx.sendKeyDown(SDLK_RETURN, false);
        REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);
        REQUIRE(ctx.viewManager.getCol() == 3);
        REQUIRE(ctx.viewManager.getRow() == 2);
    }

    SECTION("X-release (no arrows) jumps to INST POOL") {
        auto actions = ctx.getActions();
        HandleProjectEditRelease(ctx.cursorId, ctx.nameCharIndex, ctx.engineState,
                                 ctx.commandSink, actions);
        REQUIRE(ctx.viewManager.getCurrentView() == ViewType::INST_POOL);
        REQUIRE(ctx.viewManager.getCol() == 3);
        REQUIRE(ctx.viewManager.getRow() == 2);
    }
}
