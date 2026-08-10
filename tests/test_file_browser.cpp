#include <catch2/catch_test_macros.hpp>
#include "../src/ui/FileBrowser.h"
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>

namespace fs = std::filesystem;

namespace {

// Helper to construct a synthetic SDL_EVENT_KEY_DOWN
SDL_Event makeKeyEvent(SDL_Keycode key) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = key;
    return ev;
}

// Scoped temporary directory fixture for testing file browser operations
struct TempDirFixture {
    fs::path tempRoot;

    TempDirFixture() {
        tempRoot = fs::temp_directory_path() / ("m8_fb_test_" + std::to_string(std::rand()));
        fs::create_directories(tempRoot / "sub1" / "nested");
        fs::create_directories(tempRoot / "sub2");

        // Create test files
        createDummyFile(tempRoot / "song1.m8s");
        createDummyFile(tempRoot / "song2.M8S");
        createDummyFile(tempRoot / "readme.txt");
        createDummyFile(tempRoot / "sub1" / "sub_song.m8s");
        createDummyFile(tempRoot / "sub1" / "sample.wav");
        createDummyFile(tempRoot / "sub1" / "nested" / "deep.m8s");
    }

    ~TempDirFixture() {
        std::error_code ec;
        fs::remove_all(tempRoot, ec);
    }

    void createDummyFile(const fs::path& path) {
        std::ofstream f(path);
        f << "dummy";
    }
};

} // namespace

TEST_CASE("FileBrowser directory scanning and extension filter", "[file_browser]") {
    TempDirFixture fixture;
    FileBrowser fb;

    SECTION("Filters .m8s case-insensitively and lists subdirectories") {
        fb.init(fixture.tempRoot.string(), ".m8s");

        const auto& entries = fb.getEntries();
        // Should include /.. (has parent path), /sub1, /sub2, song1.m8s, song2.M8S, but NOT readme.txt
        bool foundSub1 = false;
        bool foundSub2 = false;
        bool foundSong1 = false;
        bool foundSong2 = false;
        bool foundReadme = false;

        for (const auto& e : entries) {
            if (e.name == "/sub1") foundSub1 = true;
            if (e.name == "/sub2") foundSub2 = true;
            if (e.name == "song1.m8s") foundSong1 = true;
            if (e.name == "song2.M8S") foundSong2 = true;
            if (e.name == "readme.txt") foundReadme = true;
        }

        REQUIRE(foundSub1);
        REQUIRE(foundSub2);
        REQUIRE(foundSong1);
        REQUIRE(foundSong2);
        REQUIRE_FALSE(foundReadme);
    }

    SECTION("Empty filter lists all files") {
        fb.init(fixture.tempRoot.string(), "");

        const auto& entries = fb.getEntries();
        bool foundReadme = false;
        for (const auto& e : entries) {
            if (e.name == "readme.txt") foundReadme = true;
        }
        REQUIRE(foundReadme);
    }
}

TEST_CASE("FileBrowser folder and subfolder traversal", "[file_browser]") {
    TempDirFixture fixture;
    FileBrowser fb;
    fb.init(fixture.tempRoot.string(), ".m8s");

    SECTION("Drill down into subfolder and return back up via /..") {
        // Move cursor to /sub1
        const auto& entries = fb.getEntries();
        int sub1Index = -1;
        for (int i = 0; i < (int)entries.size(); ++i) {
            if (entries[i].name == "/sub1") {
                sub1Index = i;
                break;
            }
        }
        REQUIRE(sub1Index >= 0);

        // Move down to sub1
        for (int i = 0; i < sub1Index; ++i) {
            auto res = fb.handleInput(makeKeyEvent(SDLK_DOWN), false);
            REQUIRE(res == FileBrowser::Result::NONE);
        }
        REQUIRE(fb.getCursorIndex() == sub1Index);

        // Press RIGHT (or ENTER / X) to enter /sub1
        auto res = fb.handleInput(makeKeyEvent(SDLK_RIGHT), false);
        REQUIRE(res == FileBrowser::Result::NONE);

        // We are now inside sub1
        fs::path expectedSub1 = fs::weakly_canonical(fixture.tempRoot / "sub1");
        fs::path actualCur = fs::weakly_canonical(fb.getCurrentDirectory());
        REQUIRE(actualCur == expectedSub1);

        // Should see /.. , /nested, and sub_song.m8s
        const auto& subEntries = fb.getEntries();
        REQUIRE_FALSE(subEntries.empty());
        REQUIRE(subEntries[0].name == "/..");

        bool foundNested = false;
        bool foundSubSong = false;
        for (const auto& e : subEntries) {
            if (e.name == "/nested") foundNested = true;
            if (e.name == "sub_song.m8s") foundSubSong = true;
        }
        REQUIRE(foundNested);
        REQUIRE(foundSubSong);

        // Drill further down into /nested
        int nestedIdx = -1;
        for (int i = 0; i < (int)subEntries.size(); ++i) {
            if (subEntries[i].name == "/nested") {
                nestedIdx = i;
                break;
            }
        }
        REQUIRE(nestedIdx >= 0);
        for (int i = 0; i < nestedIdx; ++i) {
            fb.handleInput(makeKeyEvent(SDLK_DOWN), false);
        }
        fb.handleInput(makeKeyEvent(SDLK_RETURN), false);

        fs::path expectedNested = fs::weakly_canonical(fixture.tempRoot / "sub1" / "nested");
        REQUIRE(fs::weakly_canonical(fb.getCurrentDirectory()) == expectedNested);

        // Navigate UP from nested using /..
        REQUIRE(fb.getCursorIndex() == 0); // On /..
        fb.handleInput(makeKeyEvent(SDLK_RETURN), false);
        REQUIRE(fs::weakly_canonical(fb.getCurrentDirectory()) == expectedSub1);
        // Cursor should refocus on /nested
        REQUIRE(fb.getEntries()[fb.getCursorIndex()].name == "/nested");

        // Navigate UP from sub1 using LEFT key
        auto leftRes = fb.handleInput(makeKeyEvent(SDLK_LEFT), false);
        REQUIRE(leftRes == FileBrowser::Result::NONE);
        REQUIRE(fs::weakly_canonical(fb.getCurrentDirectory()) == fs::weakly_canonical(fixture.tempRoot));
        // Cursor should refocus on /sub1
        REQUIRE(fb.getEntries()[fb.getCursorIndex()].name == "/sub1");
    }

    SECTION("Select file returns Result::SELECTED with path") {
        const auto& entries = fb.getEntries();
        int song1Idx = -1;
        for (int i = 0; i < (int)entries.size(); ++i) {
            if (entries[i].name == "song1.m8s") {
                song1Idx = i;
                break;
            }
        }
        REQUIRE(song1Idx >= 0);

        for (int i = 0; i < song1Idx; ++i) {
            fb.handleInput(makeKeyEvent(SDLK_DOWN), false);
        }

        // Press X (select)
        auto res = fb.handleInput(makeKeyEvent(SDLK_X), false);
        REQUIRE(res == FileBrowser::Result::SELECTED);
        REQUIRE(fs::weakly_canonical(fb.getSelectedPath()) == fs::weakly_canonical(fixture.tempRoot / "song1.m8s"));
    }

    SECTION("ESCAPE returns Result::CANCELLED") {
        auto res = fb.handleInput(makeKeyEvent(SDLK_ESCAPE), false);
        REQUIRE(res == FileBrowser::Result::CANCELLED);
    }
}
