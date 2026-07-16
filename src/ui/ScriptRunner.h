#pragma once

#include <string>
#include <vector>
#include <SDL3/SDL.h>

class Renderer;

// Forward-declared app context — populated by main.cpp, passed to ScriptRunner
// so it can execute load/save/set_sample_root without being friends with main().
struct ScriptAppContext {
    Renderer* renderer = nullptr;
    bool (*isPlaying)(void* ctx) = nullptr;
    bool (*hasError)(void* ctx) = nullptr;
    const std::string& (*getErrorMessage)(void* ctx) = nullptr;
    const std::string& (*getSongName)(void* ctx) = nullptr;
    bool (*loadSong)(const std::string& path, void* ctx) = nullptr;
    bool (*saveSong)(const std::string& path, void* ctx) = nullptr;
    void (*setSampleRoot)(const std::string& root, void* ctx) = nullptr;
    bool (*renderOffline)(int seconds, const std::string& path, void* ctx) = nullptr;
    bool* audioActive = nullptr;  // points at main.cpp's stream-null flag
    void* userData = nullptr;
};

class ScriptRunner {
public:
    // Parse a script file. Returns false on parse error (sets exit code 2).
    bool loadScript(const std::string& path);

    // Called BEFORE SDL_PollEvent each frame. Pushes synthetic SDL_Events for
    // pending KEY/HOLD/PLAY/STOP commands. Returns true while script is running.
    bool onFrameStart();

    // Called AFTER renderer.present() each frame. Executes DUMP/ASSERT commands.
    // Returns false when script is done or an assertion failed.
    bool onFrameEnd(const ScriptAppContext& ctx);

    bool  isDone()      const { return m_done; }
    bool  wantsQuit()   const { return m_quit; }
    int   getExitCode() const { return m_exitCode; }

    void  setOutDir(const std::string& dir) { m_outDir = dir; }

    // For wait-only commands (no app ctx needed): lightweight frame advance
    bool tickFrame();

private:
    enum class CmdType {
        KEY, HOLD, TYPE, WAIT,
        PLAY, STOP,
        LOAD, SAVE, SET_SAMPLE_ROOT, RENDER,
        DUMP_SCREEN, DUMP_JSON, SCREENSHOT,
        ASSERT_SCREEN_CONTAINS, ASSERT_SCREEN_ROW_CONTAINS,
        ASSERT_SCREEN_NOT_CONTAINS,
        ASSERT_PLAYING, ASSERT_STOPPED,
        ASSERT_NO_ERROR, ASSERT_ERROR_CONTAINS,
        ASSERT_SONG_NAME,
        ASSERT_NO_OVERLAP,
        ASSERT_CELL_COLOR, ASSERT_ROW_MATCHES, ASSERT_SLIDER
    };

    struct Command {
        CmdType     type;
        std::string arg;       // text / path / button name(s)
        std::string arg2;      // second argument (e.g. error substring)
        int         intArg = 0;
        SDL_Keycode key1 = SDLK_UNKNOWN;
        SDL_Keycode key2 = SDLK_UNKNOWN;
    };

    // Parsing
    bool        parseLine(const std::string& line, int lineNum, Command& out);
    SDL_Keycode parseButton(const std::string& name);
    std::string outPath(const std::string& filename);

    // Execution helpers
    void pushKeyEvent(SDL_Keycode key, bool down);
    bool assertScreenContains(const Renderer& r, const std::string& text);
    bool assertScreenRowContains(const Renderer& r, int row, const std::string& text);
    bool assertScreenNotContains(const Renderer& r, const std::string& text);
    void autoDump(const ScriptAppContext& ctx);

    std::vector<Command> m_commands;
    size_t               m_cmdIndex = 0;

    // Wait / hold state
    int          m_waitFrames = 0;
    bool         m_holdActive = false;
    SDL_Keycode  m_holdKey = SDLK_UNKNOWN;
    int          m_holdFramesLeft = 0;

    int          m_exitCode = 0;
    bool         m_done = false;
    bool         m_quit = false;
    bool         m_assertFailed = false;
    std::string  m_outDir;
};
