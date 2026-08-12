#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include "ui/screens/render/RenderScreenLayout.h"
#include "ui/screens/render/RenderScreen.h"
#include "io/RenderAudio.h"
#include "io/SongIO.h"
#include <filesystem>
#include <fstream>

using namespace m8::ui;
using namespace m8::ui::render;

TEST_CASE("RenderScreen layout and navigation structure", "[render]") {
    auto staticText = GetRenderStaticText();
    REQUIRE_FALSE(staticText.empty());
    CHECK(staticText[0].text == "RENDER AUDIO");
    CHECK(staticText[0].row == 0);
    CHECK(staticText[0].col == 0);

    auto fields = GetRenderInteractiveFields();
    CHECK(fields.find(CursorId::SONG_ROW_START) != fields.end());
    CHECK(fields.find(CursorId::SONG_ROW_LAST) != fields.end());
    CHECK(fields.find(CursorId::REPEAT_SONG) != fields.end());
    CHECK(fields.find(CursorId::TRACK_1) != fields.end());
    CHECK(fields.find(CursorId::TRACK_8) != fields.end());
    CHECK(fields.find(CursorId::MODFX) != fields.end());
    CHECK(fields.find(CursorId::MODE) != fields.end());
    CHECK(fields.find(CursorId::NAME) != fields.end());
    CHECK(fields.find(CursorId::RENDER_MIXED) != fields.end());
    CHECK(fields.find(CursorId::RENDER_STEMS) != fields.end());

    auto navMap = GetRenderNavMap();
    // Start -> Last -> Repeat -> Track 1
    CHECK(navMap[CursorId::SONG_ROW_START].down == CursorId::SONG_ROW_LAST);
    CHECK(navMap[CursorId::SONG_ROW_LAST].down == CursorId::REPEAT_SONG);
    CHECK(navMap[CursorId::REPEAT_SONG].down == CursorId::TRACK_1);

    // Track 1 -> Track 2 -> ... -> Track 8
    CHECK(navMap[CursorId::TRACK_1].right == CursorId::TRACK_2);
    CHECK(navMap[CursorId::TRACK_2].right == CursorId::TRACK_3);
    CHECK(navMap[CursorId::TRACK_7].right == CursorId::TRACK_8);
    CHECK(navMap[CursorId::TRACK_8].right == CursorId::NONE);

    // Track -> ModFX -> Delay -> Reverb -> Limiter -> Mix EQ -> Mode -> Name -> Mixed / Stems
    CHECK(navMap[CursorId::TRACK_1].down == CursorId::MODFX);
    CHECK(navMap[CursorId::MODFX].down == CursorId::DELAY);
    CHECK(navMap[CursorId::DELAY].down == CursorId::REVERB);
    CHECK(navMap[CursorId::REVERB].down == CursorId::LIMITER);
    CHECK(navMap[CursorId::LIMITER].down == CursorId::MIX_EQ);
    CHECK(navMap[CursorId::MIX_EQ].down == CursorId::MODE);
    CHECK(navMap[CursorId::MODE].down == CursorId::NAME);
    CHECK(navMap[CursorId::NAME].down == CursorId::RENDER_MIXED);
    CHECK(navMap[CursorId::RENDER_MIXED].right == CursorId::RENDER_STEMS);
}

TEST_CASE("RenderScreen navigation and parameter stepping", "[render]") {
    RenderScreenState state;
    m8::engine::Sequencer seq{};
    m8::engine::EngineState engineState{};
    ViewManager viewManager;
    CharPicker charPicker;
    bool arrowPressed = false;

    // 1. Step SONG_ROW_START
    state.cursorId = CursorId::SONG_ROW_START;
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    HandleRenderInput(ev, true /* editHeld */, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.songRowStart == 1);

    ev.key.key = SDLK_UP;
    HandleRenderInput(ev, true, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.songRowStart == 17);

    // 2. Step SONG_ROW_LAST
    state.cursorId = CursorId::SONG_ROW_LAST;
    CHECK(state.settings.songRowLast == -1); // AUTO
    ev.key.key = SDLK_LEFT;
    HandleRenderInput(ev, true, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.songRowLast == 255);

    // 3. Reset via EDIT + OPTION
    ev.key.key = SDLK_Z;
    HandleRenderInput(ev, true, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.songRowLast == -1);

    // 4. Toggle TRACK_1
    state.cursorId = CursorId::TRACK_1;
    CHECK(state.settings.trackEnabled[0] == true);
    ev.key.key = SDLK_RIGHT;
    HandleRenderInput(ev, true, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.trackEnabled[0] == false);

    // 5. Toggle MODE
    state.cursorId = CursorId::MODE;
    CHECK(state.settings.is32Bit == false);
    ev.key.key = SDLK_RIGHT;
    HandleRenderInput(ev, true, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.settings.is32Bit == true);

    // 6. Option key without Edit exits modal
    viewManager.pushModal(ViewType::RENDER);
    CHECK(viewManager.getCurrentView() == ViewType::RENDER);
    ev.key.key = SDLK_Z;
    HandleRenderInput(ev, false /* not editHeld */, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(viewManager.getCurrentView() != ViewType::RENDER);
}

TEST_CASE("RenderScreen NAME field navigation and editing", "[render]") {
    RenderScreenState state;
    m8::engine::Sequencer seq{};
    m8::engine::EngineState engineState{};
    ViewManager viewManager;
    CharPicker charPicker;
    bool arrowPressed = false;

    state.cursorId = CursorId::NAME;
    state.nameCharIndex = 0;

    // Right moves nameCharIndex
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    HandleRenderInput(ev, false, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.nameCharIndex == 1);
    CHECK(state.cursorId == CursorId::NAME);

    // Left moves nameCharIndex back
    ev.key.key = SDLK_LEFT;
    HandleRenderInput(ev, false, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.nameCharIndex == 0);
    CHECK(state.cursorId == CursorId::NAME);

    // Up moves cursor to MODE
    ev.key.key = SDLK_UP;
    HandleRenderInput(ev, false, arrowPressed, state, seq, engineState, viewManager, charPicker);
    CHECK(state.cursorId == CursorId::MODE);
}

TEST_CASE("RenderSongAudio offline WAV generation", "[render]") {
    m8::engine::Sequencer seq{};
    m8::engine::EngineState state{};
    m8::io::RenderSettings settings{};
    std::strncpy(settings.name, "TEST_RENDER", sizeof(settings.name) - 1);

    // Put a dummy phrase on track 0 chain 0 song row 0
    seq.song[0].tracks[0] = 0;
    seq.chains[0][0].phrase = 0;
    seq.chains[0][1].phrase = m8::engine::PHRASE_EMPTY;
    seq.phrases[0][0].note = 60; // C-4
    seq.phrases[0][0].instr = 0;
    state.instruments[0].type = m8::engine::InstType::INST_WAVSYNTH;

    std::string testOutDir = "test_out_ui/render_test";
    std::filesystem::remove_all(testOutDir);

    // 1. MIXED 16-BIT Render
    settings.is32Bit = false;
    auto res = m8::io::RenderSongAudio(settings, seq, state, false, testOutDir);
    REQUIRE(res.ok);
    REQUIRE(res.outputFiles.size() == 1);
    CHECK(std::filesystem::exists(res.outputFiles[0]));

    // Check WAV header (RIFF, WAVE, fmt )
    std::ifstream f(res.outputFiles[0], std::ios::binary);
    REQUIRE(f.is_open());
    char tag[5] = {0};
    f.read(tag, 4);
    CHECK(std::string(tag) == "RIFF");
    f.seekg(8);
    f.read(tag, 4);
    CHECK(std::string(tag) == "WAVE");

    // 2. STEMS Render
    auto resStems = m8::io::RenderSongAudio(settings, seq, state, true, testOutDir);
    REQUIRE(resStems.ok);
    REQUIRE_FALSE(resStems.outputFiles.empty());
    CHECK(std::filesystem::exists(resStems.outputFiles[0]));
}

TEST_CASE("RenderSongAudio renders sunrise.m8s with sampler instruments", "[render]") {
    auto lr = m8::io::loadSong("songs/sunrise.m8s", "songs");
    if (!lr.ok) lr = m8::io::loadSong("../songs/sunrise.m8s", "../songs");
    if (!lr.ok) lr = m8::io::loadSong("../../songs/sunrise.m8s", "../../songs");
    REQUIRE(lr.ok);

    m8::io::RenderSettings settings{};
    std::strncpy(settings.name, "SUNRISE_TEST", sizeof(settings.name) - 1);
    settings.songRowStart = 0;
    settings.songRowLast = 1; // render first 2 rows
    settings.sampleRoot = "songs";

    std::string testOutDir = "test_out_ui/sunrise_render_test";
    std::filesystem::remove_all(testOutDir);

    auto res = m8::io::RenderSongAudio(settings, lr.sequencer, lr.state, false, testOutDir);
    REQUIRE(res.ok);
    REQUIRE(res.outputFiles.size() == 1);
    CHECK(std::filesystem::exists(res.outputFiles[0]));
    CHECK(std::filesystem::file_size(res.outputFiles[0]) > 44);
}
