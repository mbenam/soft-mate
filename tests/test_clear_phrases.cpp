#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include "engine/Sequencer.h"
#include "engine/SongCleanup.h"
#include "ui/ConfirmationDialog.h"
#include "ui/screens/project/ProjectScreen.h"
#include "ui/screens/project/ProjectScreenLayout.h"

using namespace m8::engine;
using namespace m8::ui;

TEST_CASE("Sequencer clearUnusedPhrasesAndChains cleans unused and deduplicates", "[clean_phrases]") {
    Sequencer seq;
    seq.clear();

    // Set up active song: row 0 track 0 -> chain 0; row 1 track 0 -> chain 1 (duplicate of chain 0)
    seq.song[0].tracks[0] = 0;
    seq.song[1].tracks[0] = 1;

    // Chain 0 has phrase 0 and phrase 1
    seq.chains[0][0].phrase = 0;
    seq.chains[0][1].phrase = 1;

    // Chain 1 is duplicate of chain 0 (phrase 2 and phrase 1, where phrase 2 is duplicate of phrase 0)
    seq.chains[1][0].phrase = 2;
    seq.chains[1][1].phrase = 1;

    // Phrase 0: C-4
    seq.phrases[0][0].note = 60;
    seq.phrases[0][0].instr = 1;

    // Phrase 1: E-4
    seq.phrases[1][0].note = 64;
    seq.phrases[1][0].instr = 1;

    // Phrase 2: C-4 (exact duplicate of phrase 0)
    seq.phrases[2][0].note = 60;
    seq.phrases[2][0].instr = 1;

    // Unused chain 5
    seq.chains[5][0].phrase = 5;
    seq.phrases[5][0].note = 72;

    // Unused phrase 10
    seq.phrases[10][0].note = 80;

    auto res = seq.clearUnusedPhrasesAndChains();

    // Verify deduplication
    CHECK(res.deduplicatedPhrases >= 1);
    CHECK(res.deduplicatedChains >= 1);

    // Song row 1 track 0 should now point to canonical chain 0
    CHECK(seq.song[1].tracks[0] == 0);

    // Phrase 0 and 1 remain active
    CHECK(seq.phrases[0][0].note == 60);
    CHECK(seq.phrases[1][0].note == 64);

    // Unused / deduplicated chains and phrases should be cleared
    CHECK(seq.chains[1][0].phrase == PHRASE_EMPTY);
    CHECK(seq.chains[5][0].phrase == PHRASE_EMPTY);
    CHECK(seq.phrases[2][0].note == NOTE_EMPTY);
    CHECK(seq.phrases[5][0].note == NOTE_EMPTY);
    CHECK(seq.phrases[10][0].note == NOTE_EMPTY);
}

TEST_CASE("ConfirmationDialog for CLEAR_PHRASES", "[clean_phrases]") {
    ConfirmationDialog dialog;
    std::string prompt = "CLEAR UNUSED PHRASES/CHAINS\nAND REMOVE DUPLICATES?";
    dialog.init(prompt, 0, 2);

    CHECK(dialog.getActionTag() == 2);
    CHECK(dialog.getSelection() == 0); // OK

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    dialog.handleInput(ev, false);
    CHECK(dialog.getSelection() == 1); // CANCEL

    ev.key.key = SDLK_LEFT;
    dialog.handleInput(ev, false);
    CHECK(dialog.getSelection() == 0); // OK

    ev.key.key = SDLK_RETURN;
    auto res = dialog.handleInput(ev, false);
    CHECK(res == ConfirmationDialog::Result::CONFIRMED);
}

TEST_CASE("ClearUnusedInstrumentsAndTables cleans unused and deduplicates", "[clean_inst]") {
    Sequencer seq;
    seq.clear();
    EngineState state;

    // Instrument 0: Macrosyn Lead
    state.instruments[0].type = InstType::INST_MACROSYN;
    setName(state.instruments[0].name, "LEAD");
    state.instruments[0].macrosyn.shape = 2;

    // Instrument 1: Macrosyn Lead (exact duplicate)
    state.instruments[1].type = InstType::INST_MACROSYN;
    setName(state.instruments[1].name, "LEAD");
    state.instruments[1].macrosyn.shape = 2;

    // Instrument 2: Unused Sampler
    state.instruments[2].type = InstType::INST_SAMPLER;
    setName(state.instruments[2].name, "UNUSED");
    std::strncpy(state.instruments[2].sampler.samplePath, "kick.wav", 127);

    // Phrase uses instrument 0 and duplicate instrument 1
    seq.phrases[0][0].instr = 0;
    seq.phrases[0][1].instr = 1;

    // Table 0: active
    seq.tables[0][0].transp = 12;
    // Table 1: exact duplicate of Table 0
    seq.tables[1][0].transp = 12;
    // Table 2: unused
    seq.tables[2][0].transp = 5;

    seq.phrases[0][0].fx[0] = FxSlot{ FxCmd::TBL, 0 };
    seq.phrases[0][1].fx[0] = FxSlot{ FxCmd::TBL, 1 };

    auto res = ClearUnusedInstrumentsAndTables(seq, state);

    CHECK(res.deduplicatedInstruments == 1);
    CHECK(res.clearedInstruments == 2);
    CHECK(res.deduplicatedTables == 1);
    CHECK(res.clearedTables == 2);

    // Instrument 1 remapped to 0 on phrase step 1
    CHECK(seq.phrases[0][1].instr == 0);
    // Table 1 remapped to 0 on phrase step 1
    CHECK(seq.phrases[0][1].fx[0].val == 0);

    // Instrument 0 remains intact
    CHECK(state.instruments[0].type == InstType::INST_MACROSYN);
    CHECK(std::string(state.instruments[0].name).rfind("LEAD", 0) == 0);

    // Unused and duplicate instruments/tables cleared
    CHECK(std::strncmp(state.instruments[1].name, "------------", 12) == 0);
    CHECK(std::strncmp(state.instruments[2].name, "------------", 12) == 0);
    CHECK(seq.tables[1][0].transp == 0);
    CHECK(seq.tables[2][0].transp == 0);
}

TEST_CASE("ConfirmationDialog for CLEAR_INST", "[clean_inst]") {
    ConfirmationDialog dialog;
    std::string prompt = "CLEAR UNUSED INST/TABLES/EQS AND\nREMOVE DUPLICATES?";
    dialog.init(prompt, 0, 3);

    CHECK(dialog.getActionTag() == 3);
    CHECK(dialog.getSelection() == 0); // OK

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    dialog.handleInput(ev, false);
    CHECK(dialog.getSelection() == 1); // CANCEL

    ev.key.key = SDLK_LEFT;
    dialog.handleInput(ev, false);
    CHECK(dialog.getSelection() == 0); // OK

    ev.key.key = SDLK_RETURN;
    auto res = dialog.handleInput(ev, false);
    CHECK(res == ConfirmationDialog::Result::CONFIRMED);
}

