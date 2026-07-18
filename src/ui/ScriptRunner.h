#pragma once

#include <cstdint>
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
    // Track-0 playhead observability (M8_APP_AUTOMATION_SPEC.md Tier 2). Reads the
    // engine's wait-free packed playhead atomic directly (Engine::getPlayheadState /
    // getPlayhead) -- NOT the shadow grid, which can't see the playhead line
    // (drawn via drawLinePixel, not drawChar/fillRectPixel).
    uint32_t (*getPlayheadState)(void* ctx) = nullptr;  // opaque; compare for "did it change"
    int (*getPlayheadRow)(void* ctx) = nullptr;         // track 0's phraseRow (0..15)
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
    // Tier 5: when true, assert_matches_golden WRITES the current snapshot
    // instead of comparing against a stored one -- the `--update-goldens`
    // regeneration mode.
    void  setUpdateGoldens(bool update) { m_updateGoldens = update; }

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
        ASSERT_CELL_COLOR, ASSERT_ROW_MATCHES, ASSERT_SLIDER,
        // Tier 2 (M8_APP_AUTOMATION_SPEC.md): deterministic observability.
        WAIT_UNTIL, CHECKPOINT_PLAYHEAD, ASSERT_PLAYHEAD_ROW, ASSERT_PLAYHEAD_ADVANCED,
        // Tier 3: author-and-verify vocabulary.
        ASSERT_FIELD, GOTO_SCREEN, ASSERT_WAV,
        // Tier 5: golden snapshot testing.
        ASSERT_MATCHES_GOLDEN
    };

    struct Command {
        CmdType     type;
        std::string arg;       // text / path / button name(s) / wait_until predicate kind / assert_field label / assert_wav file
        std::string arg2;      // second argument (e.g. error substring / wait_until operand / assert_field expected value / assert_wav metric)
        int         intArg = 0;   // wait_until timeout target row / goto tx / assert_wav op code
        int         intArg2 = 0;  // wait_until timeout (frames) / goto ty
        double      dblArg = 0.0; // assert_wav threshold value
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
    // Tier 3: find `label` on the shadow grid and read the value text to its
    // right, stopping at the first run of >=2 spaces (the M8 screens' column
    // gap) or end of row. Returns false if the label isn't found anywhere.
    bool readField(const Renderer& r, const std::string& label, std::string& outValue);
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
    bool         m_updateGoldens = false;

    // Tier 2: deterministic observability state.
    uint32_t     m_playheadCheckpoint = 0;
    bool         m_playheadCheckpointSet = false;
    int          m_waitUntilFramesLeft = -1;  // -1 = not currently waiting

    // Tier 3: `goto <screen>` state machine (driven by onFrameStart, verified
    // by onFrameEnd). Phases: 0/1/2 = normalize to SONG (DOWNx4, UPx2, LEFTx4
    // -- unconditionally valid from any grid position, see ViewManager.cpp's
    // getViewAt; DOWNx4 because y ranges over [-2,2], so reaching the y=-2
    // floor from the worst case (y=2, e.g. SCALE/INST_POOL) takes 4 presses,
    // not 2 -- an earlier version used DOWNx2 and silently undershot when
    // landing on a y=1/2 screen, corrupting the *next* goto's normalization),
    // 3 = move right to target column, 4 = move to target row, 5 = navigation
    // done, ready for onFrameEnd to verify + advance.
    int          m_gotoPhase = -1;   // -1 = inactive
    int          m_gotoCount = 0;    // presses remaining in the current phase
    int          m_gotoTargetX = 0;
    int          m_gotoTargetY = 0;
};
