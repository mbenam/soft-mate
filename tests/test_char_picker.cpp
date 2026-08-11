#include <catch2/catch_test_macros.hpp>
#include "ui/CharPicker.h"

TEST_CASE("CharPicker: Initial state and ABC grid positioning", "[char_picker]") {
    m8::ui::CharPicker picker;
    picker.init('1');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 0);
    REQUIRE(picker.getSelectedChar() == '1');
    REQUIRE_FALSE(picker.isRandomSelected());
    REQUIRE_FALSE(picker.isModeToggleSelected());

    picker.init('A');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 1);
    REQUIRE(picker.getSelectedChar() == 'A');

    picker.init('Z');
    REQUIRE(picker.getGridX() == 5);
    REQUIRE(picker.getGridY() == 3);
    REQUIRE(picker.getSelectedChar() == 'Z');

    picker.init('!');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 4);
    REQUIRE(picker.getSelectedChar() == '!');
}

TEST_CASE("CharPicker: QWERTY grid layout and positioning", "[char_picker]") {
    m8::ui::CharPicker picker;
    picker.setLayoutMode(m8::ui::LayoutMode::QWERTY);

    picker.init('Q');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 1);
    REQUIRE(picker.getSelectedChar() == 'Q');

    picker.init('P');
    REQUIRE(picker.getGridX() == 9);
    REQUIRE(picker.getGridY() == 1);
    REQUIRE(picker.getSelectedChar() == 'P');

    picker.init('A');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 2);
    REQUIRE(picker.getSelectedChar() == 'A');

    picker.init('Z');
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getGridY() == 3);
    REQUIRE(picker.getSelectedChar() == 'Z');
}

TEST_CASE("CharPicker: Arrow navigation and wrapping", "[char_picker]") {
    m8::ui::CharPicker picker;
    picker.init('1'); // (0, 0)

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // Move RIGHT
    ev.key.key = SDLK_RIGHT;
    picker.handleInput(ev);
    REQUIRE(picker.getGridX() == 1);
    REQUIRE(picker.getSelectedChar() == '2');

    // Move LEFT (wrap to 0)
    ev.key.key = SDLK_LEFT;
    picker.handleInput(ev);
    REQUIRE(picker.getGridX() == 0);
    REQUIRE(picker.getSelectedChar() == '1');

    // Move LEFT from 0 wraps to 9
    picker.handleInput(ev);
    REQUIRE(picker.getGridX() == 9);
    REQUIRE(picker.getSelectedChar() == '0');

    // Move DOWN
    ev.key.key = SDLK_DOWN;
    picker.handleInput(ev);
    REQUIRE(picker.getGridY() == 1);
    REQUIRE(picker.getSelectedChar() == 'J');

    // Move UP
    ev.key.key = SDLK_UP;
    picker.handleInput(ev);
    REQUIRE(picker.getGridY() == 0);
    REQUIRE(picker.getSelectedChar() == '0');
}

TEST_CASE("CharPicker: Bottom row, RANDOM option and Mode Toggle", "[char_picker]") {
    m8::ui::CharPicker picker;
    picker.init('!'); // row 4, col 0

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // DOWN to row 5 (RANDOM)
    ev.key.key = SDLK_DOWN;
    picker.handleInput(ev);
    REQUIRE(picker.getGridY() == 5);
    REQUIRE(picker.isRandomSelected());
    REQUIRE_FALSE(picker.isModeToggleSelected());

    // RIGHT to Mode toggle button (ABC>)
    ev.key.key = SDLK_RIGHT;
    picker.handleInput(ev);
    REQUIRE(picker.isModeToggleSelected());
    REQUIRE_FALSE(picker.isRandomSelected());
    REQUIRE(picker.getLayoutMode() == m8::ui::LayoutMode::ABC);

    // On ABC>, pressing RIGHT toggles layout to QWERTY
    ev.key.key = SDLK_RIGHT;
    picker.handleInput(ev);
    REQUIRE(picker.isModeToggleSelected());
    REQUIRE(picker.getLayoutMode() == m8::ui::LayoutMode::QWERTY);

    // On QWERTY>, pressing RIGHT toggles layout back to ABC
    ev.key.key = SDLK_RIGHT;
    picker.handleInput(ev);
    REQUIRE(picker.isModeToggleSelected());
    REQUIRE(picker.getLayoutMode() == m8::ui::LayoutMode::ABC);

    // Pressing LEFT moves cursor back to RANDOM
    ev.key.key = SDLK_LEFT;
    picker.handleInput(ev);
    REQUIRE(picker.isRandomSelected());
    REQUIRE_FALSE(picker.isModeToggleSelected());

    // Pressing UP moves cursor back to row 4
    ev.key.key = SDLK_UP;
    picker.handleInput(ev);
    REQUIRE(picker.getGridY() == 4);

    // Generate random name
    std::string name = m8::ui::CharPicker::generateRandomName();
    REQUIRE_FALSE(name.empty());
    REQUIRE(name.length() <= 12);
}
