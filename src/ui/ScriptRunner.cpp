#include "ScriptRunner.h"
#include "Renderer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <regex>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

// ─── Button name → SDL keycode mapping ──────────────────────────────────────

SDL_Keycode ScriptRunner::parseButton(const std::string& name) {
    struct mapping { const char* name; SDL_Keycode key; };
    static const mapping table[] = {
        {"UP",    SDLK_UP},
        {"DOWN",  SDLK_DOWN},
        {"LEFT",  SDLK_LEFT},
        {"RIGHT", SDLK_RIGHT},
        {"X",     SDLK_X},
        {"Z",     SDLK_Z},
        {"SHIFT", SDLK_LSHIFT},
        {"SPACE", SDLK_SPACE},
    };
    for (auto& m : table) {
        if (name == m.name) return m.key;
    }
    return SDLK_UNKNOWN;
}

// ─── Out-dir helper ─────────────────────────────────────────────────────────

std::string ScriptRunner::outPath(const std::string& filename) {
    if (m_outDir.empty()) return filename;
    // Ensure trailing separator
    std::string dir = m_outDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
#ifdef _WIN32
        dir += '\\';
#else
        dir += '/';
#endif
    return dir + filename;
}

// ─── Parse a single line ────────────────────────────────────────────────────

bool ScriptRunner::parseLine(const std::string& raw, int lineNum, Command& out) {
    std::string line = raw;
    // Strip comments
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    // Trim leading/trailing whitespace
    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false; // blank line
    line = line.substr(start);
    auto end = line.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) line = line.substr(0, end + 1);
    if (line.empty()) return false;

    // Tokenise by first space
    auto space = line.find(' ');
    std::string verb = (space != std::string::npos) ? line.substr(0, space) : line;
    std::string rest = (space != std::string::npos) ? line.substr(space + 1) : "";

    // ── verb-only commands ──────────────────────────────────────────────────
    if (verb == "play")  { out.type = CmdType::PLAY;  return true; }
    if (verb == "stop")  { out.type = CmdType::STOP;  return true; }

    // ── hold <BUTTON> <frames> ─────────────────────────────────────────────
    if (verb == "hold") {
        auto sp = rest.find(' ');
        if (sp == std::string::npos) {
            std::cerr << "Script parse error line " << lineNum
                      << ": hold needs <BUTTON> <frames>\n";
            return false;
        }
        std::string btn = rest.substr(0, sp);
        int frames = 0;
        try { frames = std::stoi(rest.substr(sp + 1)); }
        catch (...) {
            std::cerr << "Script parse error line " << lineNum
                      << ": bad frame count '" << rest.substr(sp + 1) << "'\n";
            return false;
        }
        SDL_Keycode k = parseButton(btn);
        if (k == SDLK_UNKNOWN) {
            std::cerr << "Script parse error line " << lineNum
                      << ": unknown button '" << btn << "'\n";
            return false;
        }
        out.type = CmdType::HOLD;
        out.key1 = k;
        out.intArg = frames;
        return true;
    }

    // ── key <BUTTON>[+<BUTTON>] ────────────────────────────────────────────
    if (verb == "key") {
        SDL_Keycode k1 = SDLK_UNKNOWN, k2 = SDLK_UNKNOWN;
        auto plus = rest.find('+');
        if (plus != std::string::npos) {
            k1 = parseButton(rest.substr(0, plus));
            k2 = parseButton(rest.substr(plus + 1));
        } else {
            k1 = parseButton(rest);
        }
        if (k1 == SDLK_UNKNOWN) {
            std::cerr << "Script parse error line " << lineNum
                      << ": unknown button '" << rest << "'\n";
            return false;
        }
        out.type = CmdType::KEY;
        out.key1 = k1;
        out.key2 = k2;
        return true;
    }

    // ── type "<text>" ──────────────────────────────────────────────────────
    if (verb == "type") {
        // Strip surrounding quotes if present
        std::string text = rest;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            text = text.substr(1, text.size() - 2);
        out.type = CmdType::TYPE;
        out.arg  = text;
        return true;
    }

    // ── wait <frames> ──────────────────────────────────────────────────────
    if (verb == "wait") {
        int frames = 0;
        try { frames = std::stoi(rest); }
        catch (...) {
            std::cerr << "Script parse error line " << lineNum
                      << ": bad frame count '" << rest << "'\n";
            return false;
        }
        out.type = CmdType::WAIT;
        out.intArg = frames;
        return true;
    }

    // ── load / save / set_sample_root ──────────────────────────────────────
    if (verb == "load") {
        out.type = CmdType::LOAD;
        out.arg  = rest;
        return true;
    }
    if (verb == "save") {
        out.type = CmdType::SAVE;
        out.arg  = rest;
        return true;
    }
    if (verb == "set_sample_root") {
        out.type = CmdType::SET_SAMPLE_ROOT;
        out.arg  = rest;
        return true;
    }

    // ── render <seconds> <file.wav> ────────────────────────────────────────
    if (verb == "render") {
        auto sp = rest.find(' ');
        if (sp == std::string::npos) {
            std::cerr << "Script parse error line " << lineNum
                      << ": render needs <seconds> <file.wav>\n";
            return false;
        }
        int seconds = 0;
        try { seconds = std::stoi(rest.substr(0, sp)); }
        catch (...) {
            std::cerr << "Script parse error line " << lineNum
                      << ": bad seconds '" << rest.substr(0, sp) << "'\n";
            return false;
        }
        out.type = CmdType::RENDER;
        out.intArg = seconds;
        out.arg = rest.substr(sp + 1);
        return true;
    }

    // ── dump / screenshot ──────────────────────────────────────────────────
    if (verb == "dump_screen") {
        out.type = CmdType::DUMP_SCREEN;
        out.arg  = rest;
        return true;
    }
    if (verb == "dump_json") {
        out.type = CmdType::DUMP_JSON;
        out.arg  = rest;
        return true;
    }
    if (verb == "screenshot") {
        out.type = CmdType::SCREENSHOT;
        out.arg  = rest;
        return true;
    }

    // ── assert commands ────────────────────────────────────────────────────
    // Declare assert_screen locals here (outside the if-block) so that
    // the goto bad_assert inside the if-block doesn't jump over their init.
    std::string::size_type assert_sp;
    std::string assert_sub;
    std::string assert_rest2;
    if (verb == "assert_screen") {
        assert_sp = rest.find(' ');
        assert_sub = (assert_sp != std::string::npos) ? rest.substr(0, assert_sp) : rest;
        assert_rest2 = (assert_sp != std::string::npos) ? rest.substr(assert_sp + 1) : "";

        if (assert_sub == "contains") {
            std::string txt = assert_rest2;
            if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"')
                txt = txt.substr(1, txt.size() - 2);
            out.type = CmdType::ASSERT_SCREEN_CONTAINS;
            out.arg  = txt;
            return true;
        }
        if (assert_sub == "not_contains") {
            std::string txt = assert_rest2;
            if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"')
                txt = txt.substr(1, txt.size() - 2);
            out.type = CmdType::ASSERT_SCREEN_NOT_CONTAINS;
            out.arg  = txt;
            return true;
        }
        if (assert_sub == "row") {
            auto sp2 = assert_rest2.find(' ');
            if (sp2 == std::string::npos) goto bad_assert;
            int row = -1;
            try { row = std::stoi(assert_rest2.substr(0, sp2)); }
            catch (...) { goto bad_assert; }
            std::string rest3 = assert_rest2.substr(sp2 + 1);
            if (rest3.substr(0, 8) != "contains") goto bad_assert;
            std::string txt = rest3.substr(9);
            if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"')
                txt = txt.substr(1, txt.size() - 2);
            out.type = CmdType::ASSERT_SCREEN_ROW_CONTAINS;
            out.intArg = row;
            out.arg  = txt;
            return true;
        }
    bad_assert:
        std::cerr << "Script parse error line " << lineNum
                  << ": bad assert_screen syntax: '" << rest << "'\n";
        return false;
    }

    if (verb == "assert_playing") {
        out.type = CmdType::ASSERT_PLAYING; return true;
    }
    if (verb == "assert_stopped") {
        out.type = CmdType::ASSERT_STOPPED; return true;
    }
    if (verb == "assert_no_error") {
        out.type = CmdType::ASSERT_NO_ERROR; return true;
    }
    if (verb == "assert_error") {
        // "assert_error contains \"...\""
        auto sp = rest.find(' ');
        if (sp == std::string::npos || rest.substr(0, sp) != "contains") {
            std::cerr << "Script parse error line " << lineNum
                      << ": assert_error expects 'contains \"<text>\"'\n";
            return false;
        }
        std::string txt = rest.substr(sp + 1);
        if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"')
            txt = txt.substr(1, txt.size() - 2);
        out.type = CmdType::ASSERT_ERROR_CONTAINS;
        out.arg  = txt;
        return true;
    }
    if (verb == "assert_song_name") {
        std::string txt = rest;
        if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"')
            txt = txt.substr(1, txt.size() - 2);
        out.type = CmdType::ASSERT_SONG_NAME;
        out.arg  = txt;
        return true;
    }

    if (verb == "assert_no_overlap") {
        out.type = CmdType::ASSERT_NO_OVERLAP; return true;
    }

    if (verb == "assert_cell_color") {
        // "assert_cell_color row <n> col <n> is <RRGGBBAA>"
        auto sp1 = rest.find(' ');
        if (sp1 == std::string::npos || rest.substr(0, sp1) != "row") goto bad_assert;
        auto sp2 = rest.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) goto bad_assert;
        int row = -1;
        try { row = std::stoi(rest.substr(sp1 + 1, sp2 - sp1 - 1)); }
        catch (...) { goto bad_assert; }
        auto sp3 = rest.find(' ', sp2 + 1);
        if (sp3 == std::string::npos || rest.substr(sp2 + 1, sp3 - sp2 - 1) != "col") goto bad_assert;
        auto sp4 = rest.find(' ', sp3 + 1);
        if (sp4 == std::string::npos) goto bad_assert;
        int col = -1;
        try { col = std::stoi(rest.substr(sp3 + 1, sp4 - sp3 - 1)); }
        catch (...) { goto bad_assert; }
        auto sp5 = rest.find(' ', sp4 + 1);
        if (sp5 == std::string::npos || rest.substr(sp4 + 1, sp5 - sp4 - 1) != "is") goto bad_assert;
        std::string hex = rest.substr(sp5 + 1);
        if (hex.size() >= 2 && hex.front() == '"' && hex.back() == '"')
            hex = hex.substr(1, hex.size() - 2);
        out.type = CmdType::ASSERT_CELL_COLOR;
        out.intArg = row;
        out.arg  = std::to_string(col);
        out.arg2 = hex;
        return true;
    }

    if (verb == "assert_row_matches") {
        // "assert_row_matches <n> \"<regex>\""
        auto sp1 = rest.find(' ');
        if (sp1 == std::string::npos) goto bad_assert;
        int row = -1;
        try { row = std::stoi(rest.substr(0, sp1)); }
        catch (...) { goto bad_assert; }
        std::string pat = rest.substr(sp1 + 1);
        if (pat.size() >= 2 && pat.front() == '"' && pat.back() == '"')
            pat = pat.substr(1, pat.size() - 2);
        out.type = CmdType::ASSERT_ROW_MATCHES;
        out.intArg = row;
        out.arg  = pat;
        return true;
    }

    if (verb == "assert_slider") {
        // "assert_slider row <n> col <n> fill <0-8>"
        auto sp1 = rest.find(' ');
        if (sp1 == std::string::npos || rest.substr(0, sp1) != "row") goto bad_assert;
        auto sp2 = rest.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) goto bad_assert;
        int row = -1;
        try { row = std::stoi(rest.substr(sp1 + 1, sp2 - sp1 - 1)); }
        catch (...) { goto bad_assert; }
        auto sp3 = rest.find(' ', sp2 + 1);
        if (sp3 == std::string::npos || rest.substr(sp2 + 1, sp3 - sp2 - 1) != "col") goto bad_assert;
        auto sp4 = rest.find(' ', sp3 + 1);
        if (sp4 == std::string::npos) goto bad_assert;
        int col = -1;
        try { col = std::stoi(rest.substr(sp3 + 1, sp4 - sp3 - 1)); }
        catch (...) { goto bad_assert; }
        auto sp5 = rest.find(' ', sp4 + 1);
        if (sp5 == std::string::npos || rest.substr(sp4 + 1, sp5 - sp4 - 1) != "fill") goto bad_assert;
        int fill = -1;
        try { fill = std::stoi(rest.substr(sp5 + 1)); }
        catch (...) { goto bad_assert; }
        out.type = CmdType::ASSERT_SLIDER;
        out.intArg = row;
        out.arg  = std::to_string(col);
        out.arg2 = std::to_string(fill);
        return true;
    }

    std::cerr << "Script parse error line " << lineNum
              << ": unknown command '" << verb << "'\n";
    return false;
}

// ─── Load script file ───────────────────────────────────────────────────────

bool ScriptRunner::loadScript(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Script error: cannot open '" << path << "'\n";
        m_exitCode = 2;
        m_done = true;
        return false;
    }

    m_commands.clear();
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        Command cmd;
        if (parseLine(line, lineNum, cmd)) {
            m_commands.push_back(cmd);
        } else if (!line.empty() && line.find('#') == std::string::npos) {
            // Non-blank, non-comment line that failed to parse
            m_exitCode = 2;
            m_done = true;
            return false;
        }
    }
    m_cmdIndex = 0;
    m_waitFrames = 0;
    m_holdActive = false;
    m_done = false;
    m_quit = false;
    m_exitCode = 0;
    m_assertFailed = false;
    return true;
}

// ─── Event push helpers ─────────────────────────────────────────────────────

void ScriptRunner::pushKeyEvent(SDL_Keycode key, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.key = key;
    ev.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    SDL_PushEvent(&ev);
}

// ─── Frame tick (for pure wait / done check) ───────────────────────────────

bool ScriptRunner::tickFrame() {
    if (m_done) return false;
    if (m_assertFailed) { m_done = true; return false; }
    if (m_cmdIndex >= m_commands.size()) { m_done = true; return false; }

    // Drain any active wait
    if (m_waitFrames > 0) {
        --m_waitFrames;
        return true;
    }
    return true;
}

// ─── onFrameStart — push KEY events before SDL_PollEvent ───────────────────

bool ScriptRunner::onFrameStart() {
    if (m_done) return false;
    if (m_assertFailed) { m_done = true; return false; }
    if (m_cmdIndex >= m_commands.size()) { m_done = true; return false; }

    // ── drain active hold ──────────────────────────────────────────────────
    if (m_holdActive) {
        pushKeyEvent(m_holdKey, true);
        --m_holdFramesLeft;
        if (m_holdFramesLeft <= 0) {
            pushKeyEvent(m_holdKey, false);
            m_holdActive = false;
            ++m_cmdIndex;
        }
        return true;
    }

    // ── drain active wait ──────────────────────────────────────────────────
    if (m_waitFrames > 0) {
        --m_waitFrames;
        return true;
    }

    // ── process next command ───────────────────────────────────────────────
    if (m_cmdIndex >= m_commands.size()) { m_done = true; return false; }
    const Command& cmd = m_commands[m_cmdIndex];

    switch (cmd.type) {
    case CmdType::KEY:
        if (cmd.key2 != SDLK_UNKNOWN) {
            // Compound key: first down, second down, second up, first up
            pushKeyEvent(cmd.key1, true);
            pushKeyEvent(cmd.key2, true);
            pushKeyEvent(cmd.key2, false);
            pushKeyEvent(cmd.key1, false);
        } else {
            pushKeyEvent(cmd.key1, true);
            pushKeyEvent(cmd.key1, false);
        }
        ++m_cmdIndex;
        break;

    case CmdType::HOLD:
        m_holdActive = true;
        m_holdKey = cmd.key1;
        m_holdFramesLeft = cmd.intArg;
        pushKeyEvent(m_holdKey, true);
        --m_holdFramesLeft;
        if (m_holdFramesLeft <= 0) {
            pushKeyEvent(m_holdKey, false);
            m_holdActive = false;
            ++m_cmdIndex;
        }
        break;

    case CmdType::TYPE:
        for (char c : cmd.arg) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_TEXT_INPUT;
            static char textBuf[2];
            textBuf[0] = c;
            textBuf[1] = '\0';
            ev.text.text = textBuf;
            SDL_PushEvent(&ev);
        }
        // Confirm with RETURN
        pushKeyEvent(SDLK_RETURN, true);
        pushKeyEvent(SDLK_RETURN, false);
        ++m_cmdIndex;
        break;

    case CmdType::WAIT:
        m_waitFrames = cmd.intArg;
        ++m_cmdIndex;
        break;

    case CmdType::PLAY:
    case CmdType::STOP:
    case CmdType::LOAD:
    case CmdType::SAVE:
    case CmdType::SET_SAMPLE_ROOT:
    case CmdType::RENDER:
    case CmdType::DUMP_SCREEN:
    case CmdType::DUMP_JSON:
    case CmdType::SCREENSHOT:
    case CmdType::ASSERT_SCREEN_CONTAINS:
    case CmdType::ASSERT_SCREEN_ROW_CONTAINS:
    case CmdType::ASSERT_SCREEN_NOT_CONTAINS:
    case CmdType::ASSERT_PLAYING:
    case CmdType::ASSERT_STOPPED:
    case CmdType::ASSERT_NO_ERROR:
    case CmdType::ASSERT_ERROR_CONTAINS:
    case CmdType::ASSERT_SONG_NAME:
        // These are handled in onFrameEnd or directly here
        break;
    }

    // ── execute non-visual commands immediately ────────────────────────────
    if (m_cmdIndex < m_commands.size()) {
        const Command& c = m_commands[m_cmdIndex];
        if (c.type == CmdType::PLAY || c.type == CmdType::STOP) {
            pushKeyEvent(SDLK_SPACE, true);
            pushKeyEvent(SDLK_SPACE, false);
            ++m_cmdIndex;
        }
    }

    return !m_done;
}

// ─── onFrameEnd — dump / assert after render ───────────────────────────────

bool ScriptRunner::onFrameEnd(const ScriptAppContext& ctx) {
    if (m_done) return false;
    if (m_assertFailed) { m_done = true; return false; }

    // Process any remaining DUMP / SCREENSHOT / ASSERT commands that are at the front
    bool progress = true;
    while (progress && m_cmdIndex < m_commands.size()) {
        progress = false;
        const Command& c = m_commands[m_cmdIndex];

        switch (c.type) {

        // ── LOAD ───────────────────────────────────────────────────────────
        case CmdType::LOAD: {
            if (ctx.loadSong) {
                if (!ctx.loadSong(c.arg, ctx.userData)) {
                    // load failed — error is already in missingSamplesMsg via the callback
                }
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── SAVE ───────────────────────────────────────────────────────────
        case CmdType::SAVE: {
            if (ctx.saveSong) {
                ctx.saveSong(c.arg, ctx.userData);
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── SET_SAMPLE_ROOT ────────────────────────────────────────────────
        case CmdType::SET_SAMPLE_ROOT: {
            if (ctx.setSampleRoot) {
                ctx.setSampleRoot(c.arg, ctx.userData);
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── RENDER (offline WAV) ──────────────────────────────────────────
        case CmdType::RENDER: {
            if (ctx.audioActive && *ctx.audioActive) {
                std::cerr << "render: refused — audio stream is active "
                          << "(close the device first to avoid a data race)\n";
                m_exitCode = 2;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            if (!ctx.renderOffline) {
                std::cerr << "render: no render callback\n";
                m_exitCode = 2;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            std::string path = outPath(c.arg);
            if (!ctx.renderOffline(c.intArg, path, ctx.userData)) {
                std::cerr << "render: failed to write " << path << "\n";
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── DUMP SCREEN ────────────────────────────────────────────────────
        case CmdType::DUMP_SCREEN: {
            std::string path = outPath(c.arg);
            ctx.renderer->dumpScreenText(path.c_str());
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── DUMP JSON ──────────────────────────────────────────────────────
        case CmdType::DUMP_JSON: {
            std::string path = outPath(c.arg);
            ctx.renderer->dumpJson(path.c_str(), "UNKNOWN", 0);
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── SCREENSHOT (BMP) ───────────────────────────────────────────────
        case CmdType::SCREENSHOT: {
            std::string path = outPath(c.arg);
            // Convert .png to .bmp if user specified png
            if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") {
                std::cerr << "screenshot: SDL3 has no PNG writer, saving as .bmp instead\n";
                path = path.substr(0, path.size() - 4) + ".bmp";
            }

            SDL_Renderer* sdlR = ctx.renderer->getSDLRenderer();
            if (sdlR) {
                SDL_Surface* surf = SDL_RenderReadPixels(sdlR, nullptr);
                if (surf) {
                    SDL_SaveBMP(surf, path.c_str());
                    SDL_DestroySurface(surf);
                }
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT SCREEN CONTAINS ─────────────────────────────────────────
        case CmdType::ASSERT_SCREEN_CONTAINS: {
            if (!assertScreenContains(*ctx.renderer, c.arg)) {
                std::cerr << "ASSERT FAILED (line containing 'contains \""
                          << c.arg << "\"'): text not found in screen\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT SCREEN ROW CONTAINS ─────────────────────────────────────
        case CmdType::ASSERT_SCREEN_ROW_CONTAINS: {
            if (!assertScreenRowContains(*ctx.renderer, c.intArg, c.arg)) {
                std::cerr << "ASSERT FAILED (row " << c.intArg
                          << " does not contain \"" << c.arg << "\")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT SCREEN NOT CONTAINS ─────────────────────────────────────
        case CmdType::ASSERT_SCREEN_NOT_CONTAINS: {
            if (!assertScreenNotContains(*ctx.renderer, c.arg)) {
                std::cerr << "ASSERT FAILED (screen should NOT contain \""
                          << c.arg << "\" but it does)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT PLAYING ─────────────────────────────────────────────────
        case CmdType::ASSERT_PLAYING: {
            if (ctx.isPlaying && !ctx.isPlaying(ctx.userData)) {
                std::cerr << "ASSERT FAILED (not playing)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT STOPPED ─────────────────────────────────────────────────
        case CmdType::ASSERT_STOPPED: {
            if (ctx.isPlaying && ctx.isPlaying(ctx.userData)) {
                std::cerr << "ASSERT FAILED (still playing)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT NO ERROR ────────────────────────────────────────────────
        case CmdType::ASSERT_NO_ERROR: {
            if (ctx.hasError && ctx.hasError(ctx.userData)) {
                std::cerr << "ASSERT FAILED (error overlay is showing)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT ERROR CONTAINS ──────────────────────────────────────────
        case CmdType::ASSERT_ERROR_CONTAINS: {
            if (!ctx.hasError || !ctx.hasError(ctx.userData)) {
                std::cerr << "ASSERT FAILED (expected error containing \""
                          << c.arg << "\" but no error is showing)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            const std::string& msg = ctx.getErrorMessage(ctx.userData);
            if (msg.find(c.arg) == std::string::npos) {
                std::cerr << "ASSERT FAILED (error '" << msg
                          << "' does not contain \"" << c.arg << "\")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT SONG NAME ───────────────────────────────────────────────
        case CmdType::ASSERT_SONG_NAME: {
            if (!ctx.getSongName) break;
            const std::string& name = ctx.getSongName(ctx.userData);
            if (name.find(c.arg) == std::string::npos) {
                std::cerr << "ASSERT FAILED (song name '" << name
                          << "' does not contain \"" << c.arg << "\")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT NO OVERLAP ──────────────────────────────────────────────
        case CmdType::ASSERT_NO_OVERLAP: {
            if (ctx.renderer && ctx.renderer->hasOverlap()) {
                std::cerr << "ASSERT FAILED (overlap detected: a cell was written more than once this frame)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT CELL COLOR ──────────────────────────────────────────────
        case CmdType::ASSERT_CELL_COLOR: {
            int row = c.intArg;
            int col = std::stoi(c.arg);
            uint32_t expected = 0;
            {
                std::string h = c.arg2;
                if (h.size() == 8) {
                    expected = std::stoul(h, nullptr, 16);
                } else {
                    std::cerr << "ASSERT FAILED (bad hex color '" << c.arg2 << "')\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
            }
            if (ctx.renderer) {
                const auto& vram = ctx.renderer->getVram();
                if (row < 0 || row >= Renderer::kGridH || col < 0 || col >= Renderer::kGridW) {
                    std::cerr << "ASSERT FAILED (row " << row << " col " << col << " out of bounds)\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
                if (vram[row][col].color != expected) {
                    char actualHex[9];
                    snprintf(actualHex, sizeof(actualHex), "%08X", vram[row][col].color);
                    std::cerr << "ASSERT FAILED (cell [" << row << "][" << col
                              << "] color " << actualHex
                              << " != expected " << c.arg2 << ")\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT ROW MATCHES ─────────────────────────────────────────────
        case CmdType::ASSERT_ROW_MATCHES: {
            int row = c.intArg;
            if (ctx.renderer) {
                const auto& vram = ctx.renderer->getVram();
                if (row < 0 || row >= Renderer::kGridH) {
                    std::cerr << "ASSERT FAILED (row " << row << " out of bounds)\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
                std::string rowStr;
                rowStr.reserve(Renderer::kGridW);
                for (int x = 0; x < Renderer::kGridW; ++x)
                    rowStr += vram[row][x].ch;
                try {
                    std::regex re(c.arg);
                    if (!std::regex_search(rowStr, re)) {
                        std::cerr << "ASSERT FAILED (row " << row
                                  << " does not match /" << c.arg << "/)\n";
                        autoDump(ctx);
                        m_exitCode = 1;
                        m_assertFailed = true;
                        m_done = true;
                        return false;
                    }
                } catch (const std::regex_error& e) {
                    std::cerr << "ASSERT FAILED (bad regex '" << c.arg << "': " << e.what() << ")\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT SLIDER ──────────────────────────────────────────────────
        case CmdType::ASSERT_SLIDER: {
            int row = c.intArg;
            int col = std::stoi(c.arg);
            int fill = std::stoi(c.arg2);
            if (ctx.renderer) {
                const auto& vram = ctx.renderer->getVram();
                if (row < 0 || row >= Renderer::kGridH || col < 0 || col >= Renderer::kGridW) {
                    std::cerr << "ASSERT FAILED (row " << row << " col " << col << " out of bounds)\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
                if (vram[row][col].slider != static_cast<uint8_t>(fill)) {
                    std::cerr << "ASSERT FAILED (cell [" << row << "][" << col
                              << "] slider " << (int)vram[row][col].slider
                              << " != expected " << fill << ")\n";
                    autoDump(ctx);
                    m_exitCode = 1;
                    m_assertFailed = true;
                    m_done = true;
                    return false;
                }
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        default:
            break;
        }
    }

    // Check if script is exhausted
    if (m_cmdIndex >= m_commands.size() && !m_holdActive && m_waitFrames <= 0) {
        m_done = true;
    }

    return !m_done;
}

// ─── Assert helpers (read from VRAM) ────────────────────────────────────────

bool ScriptRunner::assertScreenContains(const Renderer& r, const std::string& text) {
    const auto& vram = r.getVram();
    for (int y = 0; y < Renderer::kGridH; ++y) {
        // Build row string
        std::string row;
        row.reserve(Renderer::kGridW);
        for (int x = 0; x < Renderer::kGridW; ++x)
            row += vram[y][x].ch;
        if (row.find(text) != std::string::npos) return true;
    }
    return false;
}

bool ScriptRunner::assertScreenRowContains(const Renderer& r, int row,
                                           const std::string& text) {
    if (row < 0 || row >= Renderer::kGridH) return false;
    const auto& vram = r.getVram();
    std::string rowStr;
    rowStr.reserve(Renderer::kGridW);
    for (int x = 0; x < Renderer::kGridW; ++x)
        rowStr += vram[row][x].ch;
    return rowStr.find(text) != std::string::npos;
}

bool ScriptRunner::assertScreenNotContains(const Renderer& r,
                                           const std::string& text) {
    return !assertScreenContains(r, text);
}

// ─── Auto-dump on assertion failure ─────────────────────────────────────────

void ScriptRunner::autoDump(const ScriptAppContext& ctx) {
    if (!ctx.renderer) return;
    std::string base = m_outDir.empty() ? "." : m_outDir;
    fs::create_directories(base);

    std::string txtPath  = outPath("assert_fail_screen.txt");
    std::string jsonPath = outPath("assert_fail_screen.json");
    std::string bmpPath  = outPath("assert_fail_screen.bmp");

    ctx.renderer->dumpScreenText(txtPath.c_str());
    ctx.renderer->dumpJson(jsonPath.c_str(), "FAIL", 0);

    SDL_Renderer* sdlR = ctx.renderer->getSDLRenderer();
    if (sdlR) {
        SDL_Surface* surf = SDL_RenderReadPixels(sdlR, nullptr);
        if (surf) {
            SDL_SaveBMP(surf, bmpPath.c_str());
            SDL_DestroySurface(surf);
        }
    }
    std::cerr << "Auto-dump written to: " << txtPath << "\n"
              << "                     " << jsonPath << "\n"
              << "                     " << bmpPath  << "\n";
}
