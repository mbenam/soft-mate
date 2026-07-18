#include "ScriptRunner.h"
#include "Renderer.h"
#include "../analysis/AudioMetrics.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <regex>
#include <cstdio>
#include <filesystem>

// dr_wav's implementation is compiled once, in FileBrowser.cpp (part of the
// same m8_clone binary) -- do NOT define DR_WAV_IMPLEMENTATION here too, or
// the linker sees duplicate symbols. This include only pulls in declarations.
#include "../engine/dr_wav.h"

namespace fs = std::filesystem;

// ─── Tier 3: assert_wav metric lookup ───────────────────────────────────────
// Maps a metric name to its value in an already-computed Metrics struct. Free
// function (no ScriptRunner state needed) so it doesn't need a header entry.
static bool metricValue(const m8::analysis::Metrics& m, const std::string& name, double& out) {
    if (name == "peak")        { out = m.peak;              return true; }
    if (name == "rms")         { out = m.rms;                return true; }
    if (name == "crest")       { out = m.crestDb;            return true; }
    if (name == "dc_l")        { out = m.dcL;                return true; }
    if (name == "dc_r")        { out = m.dcR;                return true; }
    if (name == "dc_worst")    { out = m.dcWorstWindow;      return true; }
    if (name == "clipped")     { out = m.clipped;            return true; }
    if (name == "nonfinite")   { out = m.nonFinite;          return true; }
    if (name == "silence")     { out = m.longestSilenceSec;  return true; }
    if (name == "mid_rms")     { out = m.midRms;             return true; }
    if (name == "side_rms")    { out = m.sideRms;            return true; }
    if (name == "correlation") { out = m.correlation;        return true; }
    return false;
}

// ─── Tier 3: `goto <screen>` targets ────────────────────────────────────────
// (tx, ty) grid coordinates and the header substring to verify against, taken
// directly from ViewManager.cpp's getViewAt() ladder (SONG at (0,0); x runs
// right through CHAIN/PHRASE/INSTRUMENT/TABLE; y runs up through the two
// upper rows; MIXER/EFFECTS sit at y=-1/-2, valid at any x) and confirmed
// against nav.m8script's already-passing header assertions.
struct GotoTarget { const char* name; int tx; int ty; const char* verify; };
static const GotoTarget kGotoTargets[] = {
    {"SONG",       0,  0, "SONG"},
    {"CHAIN",      1,  0, "CHAIN"},
    {"PHRASE",     2,  0, "PHRASE"},
    {"INSTRUMENT", 3,  0, "INST."},
    {"TABLE",      4,  0, "TABLE"},
    {"PROJECT",    0,  1, "PROJECT"},
    {"GROOVE",     2,  1, "GROOVE"},
    {"INST_MOD",   3,  1, "INST. MODS"},
    {"SCALE",      2,  2, "SCALE"},
    {"INST_POOL",  3,  2, "INSTRUMENT POOL"},
    {"MIXER",      0, -1, "MIXER"},
    {"EFFECTS",    0, -2, "EFFECT SETTINGS"},
};

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

    // ── wait_until <predicate> [timeout_frames] ────────────────────────────
    // Forms:
    //   wait_until playing [timeout]
    //   wait_until stopped [timeout]
    //   wait_until playhead_row >=|== <n> [timeout]
    //   wait_until screen contains "<text>" [timeout]
    // Default timeout is 300 frames (5s @ 60fps); exceeding it fails the
    // assertion (auto-dumped) rather than hanging. Re-checked every frame in
    // onFrameEnd, so — unlike a bare `wait N` — it never over- or under-shoots
    // the moment the condition actually becomes true.
    if (verb == "wait_until") {
        std::istringstream iss(rest);
        std::string kind;
        iss >> kind;
        out.type = CmdType::WAIT_UNTIL;
        out.intArg2 = 300;

        if (kind == "playing" || kind == "stopped") {
            out.arg = kind;
            std::string tail;
            if (iss >> tail) { try { out.intArg2 = std::stoi(tail); } catch (...) {} }
            return true;
        }
        if (kind == "playhead_row") {
            std::string op;
            int target = 0;
            if (!(iss >> op >> target) || (op != ">=" && op != "==")) {
                std::cerr << "Script parse error line " << lineNum
                          << ": wait_until playhead_row needs '>=|== <n> [timeout]'\n";
                return false;
            }
            out.arg = "playhead_row";
            out.arg2 = op;
            out.intArg = target;
            std::string tail;
            if (iss >> tail) { try { out.intArg2 = std::stoi(tail); } catch (...) {} }
            return true;
        }
        if (kind == "screen") {
            std::string sub;
            iss >> sub;
            if (sub != "contains") {
                std::cerr << "Script parse error line " << lineNum
                          << ": wait_until screen needs 'contains \"<text>\"'\n";
                return false;
            }
            std::string remainder;
            std::getline(iss, remainder);
            auto s = remainder.find_first_not_of(' ');
            remainder = (s == std::string::npos) ? "" : remainder.substr(s);
            if (remainder.empty() || remainder.front() != '"') {
                std::cerr << "Script parse error line " << lineNum
                          << ": wait_until screen contains needs a quoted \"<text>\"\n";
                return false;
            }
            auto endq = remainder.find('"', 1);
            if (endq == std::string::npos) {
                std::cerr << "Script parse error line " << lineNum
                          << ": unterminated quote in wait_until screen contains\n";
                return false;
            }
            out.arg  = "screen_contains";
            out.arg2 = remainder.substr(1, endq - 1);
            std::string trailing = remainder.substr(endq + 1);
            auto ts = trailing.find_first_not_of(' ');
            if (ts != std::string::npos) {
                try { out.intArg2 = std::stoi(trailing.substr(ts)); } catch (...) {}
            }
            return true;
        }
        std::cerr << "Script parse error line " << lineNum
                  << ": unknown wait_until predicate '" << kind << "'\n";
        return false;
    }

    // ── checkpoint_playhead / assert_playhead_row / assert_playhead_advanced ─
    if (verb == "checkpoint_playhead") {
        out.type = CmdType::CHECKPOINT_PLAYHEAD;
        return true;
    }
    if (verb == "assert_playhead_row") {
        int row = -1;
        try { row = std::stoi(rest); }
        catch (...) {
            std::cerr << "Script parse error line " << lineNum
                      << ": bad row '" << rest << "'\n";
            return false;
        }
        out.type = CmdType::ASSERT_PLAYHEAD_ROW;
        out.intArg = row;
        return true;
    }
    if (verb == "assert_playhead_advanced") {
        out.type = CmdType::ASSERT_PLAYHEAD_ADVANCED;
        return true;
    }

    // ── assert_field <label> <value> ────────────────────────────────────────
    // <label> is a bare token, or a "quoted string" if it contains spaces
    // (e.g. "LOOP ST"). <value> is the rest of the line verbatim (may itself
    // contain spaces, e.g. `assert_field LIM 01 SIN` -- no quoting needed
    // since it's always the tail of the line). Compared for exact match
    // against the value text found immediately after the label on the shadow
    // grid (see readField).
    if (verb == "assert_field") {
        std::string label, expected;
        if (!rest.empty() && rest.front() == '"') {
            auto endq = rest.find('"', 1);
            if (endq == std::string::npos) {
                std::cerr << "Script parse error line " << lineNum
                          << ": unterminated quote in assert_field label\n";
                return false;
            }
            label = rest.substr(1, endq - 1);
            std::string remainder = rest.substr(endq + 1);
            auto s = remainder.find_first_not_of(' ');
            expected = (s == std::string::npos) ? "" : remainder.substr(s);
        } else {
            auto sp = rest.find(' ');
            if (sp == std::string::npos) {
                std::cerr << "Script parse error line " << lineNum
                          << ": assert_field needs <label> <value>\n";
                return false;
            }
            label = rest.substr(0, sp);
            expected = rest.substr(sp + 1);
        }
        while (!expected.empty() && expected.back() == ' ') expected.pop_back();
        if (label.empty()) {
            std::cerr << "Script parse error line " << lineNum
                      << ": assert_field label is empty\n";
            return false;
        }
        out.type = CmdType::ASSERT_FIELD;
        out.arg  = label;
        out.arg2 = expected;
        return true;
    }

    // ── goto <SCREEN> ────────────────────────────────────────────────────────
    // Sugar over `key SHIFT+...` + a header assertion (see kGotoTargets above):
    // normalizes to SONG via an unconditionally-valid press sequence, then
    // walks right to the target column and up/down to the target row, then
    // verifies the header. Multi-frame (driven by onFrameStart); does not
    // require knowing the current cursor position.
    if (verb == "goto") {
        const GotoTarget* found = nullptr;
        for (auto& t : kGotoTargets) if (rest == t.name) { found = &t; break; }
        if (!found) {
            std::cerr << "Script parse error line " << lineNum
                      << ": unknown goto screen '" << rest << "'\n";
            return false;
        }
        out.type    = CmdType::GOTO_SCREEN;
        out.intArg  = found->tx;
        out.intArg2 = found->ty;
        out.arg     = found->verify;
        return true;
    }

    // ── assert_wav <file> <metric> <op> <value> ─────────────────────────────
    // Inline numeric gate over a rendered WAV via AudioMetrics (the same
    // library m8_analyze uses), so a render->assert loop lives in one script
    // instead of a shell pipeline. <file> is resolved relative to the
    // script's --out-dir, same as `render`/`dump_screen`. <op> is one of
    // < <= > >= == !=. Metrics: peak, rms, crest, dc_l, dc_r, dc_worst,
    // clipped, nonfinite, silence, mid_rms, side_rms, correlation.
    if (verb == "assert_wav") {
        std::istringstream iss(rest);
        std::string file, metric, op, valueStr;
        if (!(iss >> file >> metric >> op >> valueStr)) {
            std::cerr << "Script parse error line " << lineNum
                      << ": assert_wav needs <file> <metric> <op> <value>\n";
            return false;
        }
        int opCode = -1;
        if      (op == "<")  opCode = 0;
        else if (op == "<=") opCode = 1;
        else if (op == ">")  opCode = 2;
        else if (op == ">=") opCode = 3;
        else if (op == "==") opCode = 4;
        else if (op == "!=") opCode = 5;
        else {
            std::cerr << "Script parse error line " << lineNum
                      << ": assert_wav operator must be one of < <= > >= == !=\n";
            return false;
        }
        double val = 0.0;
        try { val = std::stod(valueStr); }
        catch (...) {
            std::cerr << "Script parse error line " << lineNum
                      << ": bad assert_wav value '" << valueStr << "'\n";
            return false;
        }
        out.type   = CmdType::ASSERT_WAV;
        out.arg    = file;
        out.arg2   = metric;
        out.intArg = opCode;
        out.dblArg = val;
        return true;
    }

    // ── assert_matches_golden <name> ────────────────────────────────────────
    // Compares the current shadow-grid VRAM against tests/ui/golden/<name>.txt
    // (glyph, fg/bg color, slider, bracket per cell -- no bpm/playhead, since
    // those aren't part of Renderer::writeGolden's per-cell format at all).
    // In --update-goldens mode, writes the golden file instead of comparing
    // and always passes.
    if (verb == "assert_matches_golden") {
        if (rest.empty()) {
            std::cerr << "Script parse error line " << lineNum
                      << ": assert_matches_golden needs a <name>\n";
            return false;
        }
        out.type = CmdType::ASSERT_MATCHES_GOLDEN;
        out.arg  = rest;
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

// Tier 3 `repeat` block detection: comment-strip + trim, used only to spot
// "repeat N {" / "}" markers. parseLine has its own equivalent inline logic
// for ordinary command lines -- left untouched here to avoid changing its
// (already-tested) existing parse-error detection.
static std::string stripCommentAndTrim(const std::string& raw) {
    std::string line = raw;
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    line = line.substr(start);
    auto end = line.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) line = line.substr(0, end + 1);
    return line;
}

bool ScriptRunner::loadScript(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Script error: cannot open '" << path << "'\n";
        m_exitCode = 2;
        m_done = true;
        return false;
    }

    std::vector<std::string> rawLines;
    {
        std::string line;
        while (std::getline(file, line)) rawLines.push_back(line);
    }

    m_commands.clear();
    int lineNum = 0;
    for (size_t i = 0; i < rawLines.size(); ) {
        ++lineNum;
        const std::string& raw = rawLines[i];
        std::string trimmed = stripCommentAndTrim(raw);

        // ── repeat <n> { ... } ───────────────────────────────────────────────
        // Load-time unrolling: the body is parsed ONCE via the normal parseLine
        // path (so every existing verb, including goto/wait_until, works
        // inside it unmodified), then the resulting Commands are appended N
        // times. No new runtime state -- the script is just longer.
        if (trimmed.rfind("repeat ", 0) == 0 && !trimmed.empty() && trimmed.back() == '{') {
            std::string countStr = trimmed.substr(7, trimmed.size() - 7 - 1);
            auto cs = countStr.find_first_not_of(" \t");
            auto ce = countStr.find_last_not_of(" \t");
            countStr = (cs == std::string::npos) ? "" : countStr.substr(cs, ce - cs + 1);
            int n = 0;
            try { n = std::stoi(countStr); }
            catch (...) {
                std::cerr << "Script parse error line " << lineNum
                          << ": bad repeat count '" << countStr << "'\n";
                m_exitCode = 2;
                m_done = true;
                return false;
            }

            std::vector<Command> body;
            size_t j = i + 1;
            int bodyLineNum = lineNum;
            bool closed = false;
            for (; j < rawLines.size(); ++j) {
                ++bodyLineNum;
                std::string btrim = stripCommentAndTrim(rawLines[j]);
                if (btrim.empty()) continue;
                if (btrim == "}") { closed = true; break; }
                if (btrim.rfind("repeat ", 0) == 0) {
                    std::cerr << "Script parse error line " << bodyLineNum
                              << ": nested repeat is not supported\n";
                    m_exitCode = 2;
                    m_done = true;
                    return false;
                }
                Command cmd;
                if (!parseLine(rawLines[j], bodyLineNum, cmd)) {
                    m_exitCode = 2;
                    m_done = true;
                    return false;
                }
                body.push_back(cmd);
            }
            if (!closed) {
                std::cerr << "Script parse error line " << lineNum
                          << ": repeat block missing closing '}'\n";
                m_exitCode = 2;
                m_done = true;
                return false;
            }
            for (int r = 0; r < n; ++r)
                for (const auto& c : body) m_commands.push_back(c);

            i = j + 1;
            lineNum = bodyLineNum;
            continue;
        }

        Command cmd;
        if (parseLine(raw, lineNum, cmd)) {
            m_commands.push_back(cmd);
        } else if (!raw.empty() && raw.find('#') == std::string::npos) {
            // Non-blank, non-comment line that failed to parse
            m_exitCode = 2;
            m_done = true;
            return false;
        }
        ++i;
    }

    m_cmdIndex = 0;
    m_waitFrames = 0;
    m_holdActive = false;
    m_done = false;
    m_quit = false;
    m_exitCode = 0;
    m_assertFailed = false;
    m_playheadCheckpoint = 0;
    m_playheadCheckpointSet = false;
    m_waitUntilFramesLeft = -1;
    m_gotoPhase = -1;
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

    // ── GOTO SCREEN ─────────────────────────────────────────────────────────
    // Multi-frame state machine: normalize to SONG (phases 0-2, unconditionally
    // valid from any grid position -- see ViewManager.cpp's getViewAt), then
    // walk to the target column (phase 3) and row (phase 4), one compound
    // SHIFT+direction press per frame, matching how a plain `key SHIFT+RIGHT`
    // line is pressed. Does NOT advance m_cmdIndex -- onFrameEnd does that
    // once phase 5 (navigation done) and the header has been verified.
    case CmdType::GOTO_SCREEN: {
        if (m_gotoPhase < 0) {
            m_gotoPhase = 0;
            m_gotoCount = 4;  // normalize: DOWN x4 (worst case y=2 -> y=-2)
            m_gotoTargetX = cmd.intArg;
            m_gotoTargetY = cmd.intArg2;
        }
        auto pressShift = [&](SDL_Keycode dir) {
            pushKeyEvent(SDLK_LSHIFT, true);
            pushKeyEvent(dir, true);
            pushKeyEvent(dir, false);
            pushKeyEvent(SDLK_LSHIFT, false);
        };
        switch (m_gotoPhase) {
        case 0:  // normalize: DOWN x4, guaranteed to reach y=-2 (MIXER/EFFECTS'
                 // row, valid at any column) from any starting y in [-2,2];
                 // extra presses once already at -2 are safe no-ops.
            pressShift(SDLK_DOWN);
            if (--m_gotoCount <= 0) { m_gotoPhase = 1; m_gotoCount = 2; }
            break;
        case 1:  // normalize: UP x2 (back to y=0, same column)
            pressShift(SDLK_UP);
            if (--m_gotoCount <= 0) { m_gotoPhase = 2; m_gotoCount = 4; }
            break;
        case 2:  // normalize: LEFT x4 (walk to column 0 = SONG)
            pressShift(SDLK_LEFT);
            if (--m_gotoCount <= 0) { m_gotoPhase = 3; m_gotoCount = m_gotoTargetX; }
            break;
        case 3:  // move right to the target column
            if (m_gotoCount > 0) { pressShift(SDLK_RIGHT); --m_gotoCount; }
            if (m_gotoCount <= 0) {
                m_gotoPhase = 4;
                m_gotoCount = (m_gotoTargetY >= 0) ? m_gotoTargetY : -m_gotoTargetY;
            }
            break;
        case 4:  // move to the target row
            if (m_gotoCount > 0) {
                pressShift(m_gotoTargetY >= 0 ? SDLK_UP : SDLK_DOWN);
                --m_gotoCount;
            }
            if (m_gotoCount <= 0) m_gotoPhase = 5;  // ready for onFrameEnd to verify
            break;
        default:
            break;  // phase 5: waiting for onFrameEnd to verify + advance
        }
        break;
    }

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
    case CmdType::ASSERT_NO_OVERLAP:
    case CmdType::ASSERT_CELL_COLOR:
    case CmdType::ASSERT_ROW_MATCHES:
    case CmdType::ASSERT_SLIDER:
    case CmdType::WAIT_UNTIL:
    case CmdType::CHECKPOINT_PLAYHEAD:
    case CmdType::ASSERT_PLAYHEAD_ROW:
    case CmdType::ASSERT_PLAYHEAD_ADVANCED:
    case CmdType::ASSERT_FIELD:
    case CmdType::ASSERT_WAV:
    case CmdType::ASSERT_MATCHES_GOLDEN:
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

        // ── CHECKPOINT PLAYHEAD ────────────────────────────────────────────
        case CmdType::CHECKPOINT_PLAYHEAD: {
            m_playheadCheckpoint = ctx.getPlayheadState ? ctx.getPlayheadState(ctx.userData) : 0;
            m_playheadCheckpointSet = true;
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT PLAYHEAD ROW ────────────────────────────────────────────
        case CmdType::ASSERT_PLAYHEAD_ROW: {
            int actual = ctx.getPlayheadRow ? ctx.getPlayheadRow(ctx.userData) : -1;
            if (actual != c.intArg) {
                std::cerr << "ASSERT FAILED (playhead row " << actual
                          << " != expected " << c.intArg << ")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT PLAYHEAD ADVANCED ───────────────────────────────────────
        // Compares against the state captured by the most recent
        // checkpoint_playhead -- proves the playhead actually moved, unlike
        // assert_playing, which only proves the transport flag is set.
        case CmdType::ASSERT_PLAYHEAD_ADVANCED: {
            if (!m_playheadCheckpointSet) {
                std::cerr << "ASSERT FAILED (assert_playhead_advanced needs a "
                             "prior checkpoint_playhead)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            uint32_t now = ctx.getPlayheadState ? ctx.getPlayheadState(ctx.userData) : 0;
            if (now == m_playheadCheckpoint) {
                std::cerr << "ASSERT FAILED (playhead did not advance since checkpoint)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── WAIT UNTIL ──────────────────────────────────────────────────────
        // Re-checked every frame (unlike a bare `wait N`, which just counts
        // frames and hopes the state has settled by then -- the root cause of
        // the playhead.m8script ASan/Release timing race this replaces).
        case CmdType::WAIT_UNTIL: {
            bool satisfied = false;
            if (c.arg == "playing") {
                satisfied = ctx.isPlaying && ctx.isPlaying(ctx.userData);
            } else if (c.arg == "stopped") {
                satisfied = !ctx.isPlaying || !ctx.isPlaying(ctx.userData);
            } else if (c.arg == "playhead_row") {
                int actual = ctx.getPlayheadRow ? ctx.getPlayheadRow(ctx.userData) : -1;
                satisfied = (c.arg2 == ">=") ? (actual >= c.intArg) : (actual == c.intArg);
            } else if (c.arg == "screen_contains") {
                satisfied = ctx.renderer && assertScreenContains(*ctx.renderer, c.arg2);
            }

            if (satisfied) {
                m_waitUntilFramesLeft = -1;
                ++m_cmdIndex; progress = true;
                break;
            }

            if (m_waitUntilFramesLeft < 0) m_waitUntilFramesLeft = c.intArg2;  // arm on first check
            if (m_waitUntilFramesLeft <= 0) {
                std::cerr << "ASSERT FAILED (wait_until '" << c.arg
                          << "' timed out after " << c.intArg2 << " frames)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                m_waitUntilFramesLeft = -1;
                return false;
            }
            --m_waitUntilFramesLeft;
            // Not satisfied and not timed out: leave m_cmdIndex and progress
            // alone so onFrameEnd re-enters this same command next frame.
            break;
        }

        // ── ASSERT FIELD ───────────────────────────────────────────────────
        case CmdType::ASSERT_FIELD: {
            std::string actual;
            bool found = ctx.renderer && readField(*ctx.renderer, c.arg, actual);
            if (!found) {
                std::cerr << "ASSERT FAILED (field '" << c.arg << "' not found on screen)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            if (actual != c.arg2) {
                std::cerr << "ASSERT FAILED (field '" << c.arg << "' = \"" << actual
                          << "\" != expected \"" << c.arg2 << "\")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── GOTO SCREEN (verify + advance; presses happen in onFrameStart) ──
        case CmdType::GOTO_SCREEN: {
            if (m_gotoPhase < 5) break;  // still navigating this frame
            bool ok = ctx.renderer && assertScreenContains(*ctx.renderer, c.arg);
            m_gotoPhase = -1;  // reset for the next goto, whether this one passed or not
            if (!ok) {
                std::cerr << "ASSERT FAILED (goto: expected screen containing \""
                          << c.arg << "\")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT WAV ──────────────────────────────────────────────────────
        case CmdType::ASSERT_WAV: {
            std::string path = outPath(c.arg);
            unsigned int channels = 0, sr = 0;
            drwav_uint64 totalFrames = 0;
            float* pcm = drwav_open_file_and_read_pcm_frames_f32(
                path.c_str(), &channels, &sr, &totalFrames, nullptr);
            if (!pcm) {
                std::cerr << "ASSERT FAILED (assert_wav: cannot read '" << path << "')\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            if (channels != 2) {
                drwav_free(pcm, nullptr);
                std::cerr << "ASSERT FAILED (assert_wav: '" << path << "' is not stereo)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }

            m8::analysis::Metrics metrics =
                m8::analysis::analyze(pcm, static_cast<size_t>(totalFrames), static_cast<int>(sr));
            drwav_free(pcm, nullptr);

            double actual = 0.0;
            if (!metricValue(metrics, c.arg2, actual)) {
                std::cerr << "ASSERT FAILED (assert_wav: unknown metric '" << c.arg2 << "')\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }

            bool pass = false;
            switch (c.intArg) {
            case 0: pass = actual <  c.dblArg; break;
            case 1: pass = actual <= c.dblArg; break;
            case 2: pass = actual >  c.dblArg; break;
            case 3: pass = actual >= c.dblArg; break;
            case 4: pass = std::fabs(actual - c.dblArg) < 1e-6; break;
            case 5: pass = std::fabs(actual - c.dblArg) >= 1e-6; break;
            }
            if (!pass) {
                static const char* kOpNames[] = {"<", "<=", ">", ">=", "==", "!="};
                std::cerr << "ASSERT FAILED (assert_wav '" << c.arg << "': " << c.arg2
                          << " = " << actual << " " << kOpNames[c.intArg]
                          << " " << c.dblArg << " is false)\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
            }
            ++m_cmdIndex; progress = true;
            break;
        }

        // ── ASSERT MATCHES GOLDEN ──────────────────────────────────────────
        case CmdType::ASSERT_MATCHES_GOLDEN: {
            if (!ctx.renderer) { ++m_cmdIndex; progress = true; break; }
            std::string goldenPath = "tests/ui/golden/" + c.arg + ".txt";

            if (m_updateGoldens) {
                fs::create_directories("tests/ui/golden");
                ctx.renderer->writeGolden(goldenPath);
                std::cerr << "golden updated: " << goldenPath << "\n";
                ++m_cmdIndex; progress = true;
                break;
            }

            std::string mismatch;
            if (!ctx.renderer->compareGolden(goldenPath, mismatch)) {
                std::cerr << "ASSERT FAILED (assert_matches_golden '" << c.arg
                          << "': " << mismatch << ")\n";
                autoDump(ctx);
                m_exitCode = 1;
                m_assertFailed = true;
                m_done = true;
                return false;
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

// Tier 3: find `label` as a whole token (bounded by spaces or row edges, so
// e.g. searching "OFF" can't accidentally match inside "00OFF") and read the
// value text immediately after it, stopping at the first run of >=2 spaces
// (the M8 screens' column gap between adjacent fields) or end of row.
bool ScriptRunner::readField(const Renderer& r, const std::string& label, std::string& outValue) {
    const auto& vram = r.getVram();
    for (int y = 0; y < Renderer::kGridH; ++y) {
        std::string row;
        row.reserve(Renderer::kGridW);
        for (int x = 0; x < Renderer::kGridW; ++x) row += vram[y][x].ch;

        size_t pos = row.find(label);
        while (pos != std::string::npos) {
            bool leftOk  = (pos == 0) || (row[pos - 1] == ' ');
            size_t after = pos + label.size();
            bool rightOk = (after >= row.size()) || (row[after] == ' ');
            if (leftOk && rightOk) {
                size_t vstart = after;
                while (vstart < row.size() && row[vstart] == ' ') ++vstart;
                size_t vend = vstart;
                int spaceRun = 0;
                while (vend < row.size()) {
                    if (row[vend] == ' ') {
                        if (++spaceRun >= 2) break;
                    } else {
                        spaceRun = 0;
                    }
                    ++vend;
                }
                std::string val = row.substr(vstart, vend - vstart);
                while (!val.empty() && val.back() == ' ') val.pop_back();
                outValue = val;
                return true;
            }
            pos = row.find(label, pos + 1);
        }
    }
    return false;
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
