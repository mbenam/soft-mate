#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include "io/BundleExport.h"
#include "io/SongIO.h"
#include "ui/ConfirmationDialog.h"
#include "ui/screens/project/ProjectScreen.h"
#include "ui/screens/project/ProjectScreenLayout.h"
#include <filesystem>
#include <fstream>

using namespace m8::ui;
using namespace m8::ui::project;

TEST_CASE("ConfirmationDialog multi-line prompt and action tags", "[bundle]") {
    ConfirmationDialog dialog;
    std::string prompt = "CREATE DIRECTORY OF SONG AND\nSAMPLES? A PRE-EXISTING BUNDLE\nMAY BE OVERWRITTEN";
    dialog.init(prompt, 0, 1);

    CHECK(dialog.getActionTag() == 1);
    CHECK(dialog.getSelection() == 0); // OK

    // Arrow keys toggle between OK and CANCEL
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RIGHT;
    auto res = dialog.handleInput(ev, false);
    CHECK(res == ConfirmationDialog::Result::NONE);
    CHECK(dialog.getSelection() == 1); // CANCEL

    ev.key.key = SDLK_LEFT;
    res = dialog.handleInput(ev, false);
    CHECK(res == ConfirmationDialog::Result::NONE);
    CHECK(dialog.getSelection() == 0); // OK

    // Enter confirms
    ev.key.key = SDLK_RETURN;
    res = dialog.handleInput(ev, false);
    CHECK(res == ConfirmationDialog::Result::CONFIRMED);
}

TEST_CASE("ExportSongBundle creates directory, .m8s song file, and copies sample WAVs", "[bundle]") {
    auto lr = m8::io::loadSong("songs/sunrise.m8s", "songs");
    if (!lr.ok) lr = m8::io::loadSong("../songs/sunrise.m8s", "../songs");
    if (!lr.ok) lr = m8::io::loadSong("../../songs/sunrise.m8s", "../../songs");
    REQUIRE(lr.ok);

    std::string testBundleRoot = "test_out_ui/test_bundle_export";
    std::filesystem::remove_all(testBundleRoot);

    auto res = m8::io::ExportSongBundle(
        "SUNRISE", "songs/sunrise.m8s", lr, lr.sequencer, lr.state, "songs", testBundleRoot);

    REQUIRE(res.ok);
    CHECK(std::filesystem::exists(res.bundleDirectory));
    CHECK(std::filesystem::exists(res.songPath));
    CHECK_FALSE(res.copiedSamples.empty());

    // Verify the bundled song file can be reloaded
    auto reloadRes = m8::io::loadSong(res.songPath, res.bundleDirectory);
    REQUIRE(reloadRes.ok);

    // Verify all copied samples exist inside the bundle directory
    for (const auto& rel : res.copiedSamples) {
        std::filesystem::path p = std::filesystem::path(res.bundleDirectory) / rel;
        CHECK(std::filesystem::exists(p));
    }
}
