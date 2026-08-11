#include <catch2/catch_test_macros.hpp>
#include "ui/ConfirmationDialog.h"
#include "ui/Renderer.h"
#include <SDL3/SDL.h>

using namespace m8::ui;

TEST_CASE("ConfirmationDialog default state and toggle", "[confirmation_dialog]") {
    ConfirmationDialog dialog;
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);

    REQUIRE(dialog.getSelection() == 0); // OK is default
    REQUIRE(dialog.getPrompt() == "LOSE CHANGES TO CURRENT SONG?");

    // Toggle to CANCEL via RIGHT
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    auto res = dialog.handleInput(ev, false);
    REQUIRE(res == ConfirmationDialog::Result::NONE);
    REQUIRE(dialog.getSelection() == 1); // CANCEL

    // Toggle back to OK via LEFT
    ev.key.key = SDLK_LEFT;
    res = dialog.handleInput(ev, false);
    REQUIRE(res == ConfirmationDialog::Result::NONE);
    REQUIRE(dialog.getSelection() == 0); // OK

    // Toggle via UP / DOWN
    ev.key.key = SDLK_DOWN;
    dialog.handleInput(ev, false);
    REQUIRE(dialog.getSelection() == 1);

    ev.key.key = SDLK_UP;
    dialog.handleInput(ev, false);
    REQUIRE(dialog.getSelection() == 0);
}

TEST_CASE("ConfirmationDialog confirm and cancel actions", "[confirmation_dialog]") {
    ConfirmationDialog dialog;

    // Test RETURN on OK -> CONFIRMED
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CONFIRMED);

    // Test X on OK -> CONFIRMED
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    ev.key.key = SDLK_X;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CONFIRMED);

    // Test SPACE on OK -> CONFIRMED
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    ev.key.key = SDLK_SPACE;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CONFIRMED);

    // Select CANCEL then RETURN -> CANCELLED
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    ev.key.key = SDLK_RIGHT;
    dialog.handleInput(ev, false);
    ev.key.key = SDLK_RETURN;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CANCELLED);

    // Test ESCAPE -> CANCELLED regardless of selection
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    ev.key.key = SDLK_ESCAPE;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CANCELLED);

    // Test Z -> CANCELLED
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
    ev.key.key = SDLK_Z;
    REQUIRE(dialog.handleInput(ev, false) == ConfirmationDialog::Result::CANCELLED);
}

TEST_CASE("ConfirmationDialog render smoke test", "[confirmation_dialog]") {
    ConfirmationDialog dialog;
    dialog.init("LOSE CHANGES TO CURRENT SONG?", 0);

    Renderer renderer;
    renderer.init(320, 240, 1, true); // headless
    dialog.render(renderer, {255, 60, 60, 255}, {255, 255, 255, 255}, {0, 255, 255, 255});

    const auto& vram = renderer.getVram();
    // Verify prompt is rendered around row 9
    bool foundPrompt = false;
    for (int col = 0; col < 40; ++col) {
        if (vram[9][col].ch == 'L') {
            foundPrompt = true;
            break;
        }
    }
    REQUIRE(foundPrompt);
}
