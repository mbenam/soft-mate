// ===========================================================================
// src/tools/main_nav.cpp
//
// Decodes the M8 headless serial *display* stream (the same SLIP-framed draw
// protocol m8c speaks) into a text grid, so the harness can read the on-device
// screen — the foundation for framebuffer-verified file-browser navigation
// (M8_HARDWARE_TEST_SPEC.md §8.2b).
//
//   m8_nav --port COM3 --dump-screen        # decode one frame, print it
//   m8_nav --port COM3 --json screen.json   # + machine-readable cells w/ colors
//   m8_nav --port COM3 --load-file probe.m8s  # closed-loop load
//   m8_nav --port COM3 --goto-screen INSTRUMENT  # navigate to a screen
//   m8_nav --port COM3 --read-field CUTOFF   # read a field value
//   m8_nav --port COM3 --record-frames out.bin  # record decoded cell frames
//
// Serial only. No engine, no SDL, no audio.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <cctype>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/Gestures.h"
#include "m8/DeviceScriptRunner.h"
#include "m8/Result.h"

#include "m8/Semantic.h"
#include "m8/Daemon.h"
#include "m8/UiCapture.h"
#include "m8/DeviceTheme.h"

using namespace m8::dev;

static const char* kDefaultGesturePath = "hw_buttons.json";
static const char* kDefaultThemePath   = "hw_theme.json";


static std::string alnumUpper(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)))
            o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

// ---- Pin gestures mode (Tier 2: discover edit masks empirically) -----------

struct GestureResult {
    std::string name;
    uint8_t mask;
    std::string description;
    bool changed;       // the cursor text changed
    bool fieldMoved;    // the cursor moved to a different field (first line differs)
    bool valueEdited;   // same field, different value = actual edit
    std::string before;
    std::string after;
};

static std::string firstLine(const std::string& s) {
    // Return the first non-empty line (skip blank rows from cursor highlight).
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        std::string line = (nl != std::string::npos) ? s.substr(pos, nl - pos) : s.substr(pos);
        // Trim spaces
        size_t start = 0;
        while (start < line.size() && line[start] == ' ') ++start;
        if (start < line.size()) return line.substr(start);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return "";  // all blank
}

// Extract the field name (first word) from a cursor label line.
// "TEMPO        140" -> "TEMPO", " 2    " -> "2", "1     " -> "1"
static std::string fieldName(const std::string& label) {
    size_t start = 0;
    while (start < label.size() && label[start] == ' ') ++start;
    size_t end = start;
    while (end < label.size() && label[end] != ' ') ++end;
    return label.substr(start, end - start);
}

static int pinGestures(M8Device& dev, const std::string& field, int holdMs) {
    std::printf("=== PIN GESTURES ===\n");
    std::printf("target field: %s\n", field.c_str());

    // Auto-detect which screen the field lives on.
    Screen targetScreen = findScreenForField(field);
    if (targetScreen == Screen::UNKNOWN) {
        std::fprintf(stderr, "field '%s' not found in any screen field map\n", field.c_str());
        return 1;
    }
    const char* screenName = "?";
    for (auto& si : kScreenTable) {
        if (si.id == targetScreen) { screenName = si.canonHeader; break; }
    }
    std::printf("field lives on screen: %s\n", screenName);

    // Step 1: Navigate to the correct screen.
    std::printf("\n[1] Navigating to %s screen...\n", screenName);
    auto navResult = gotoScreen(dev, targetScreen, holdMs);
    if (!navResult.ok) {
        std::fprintf(stderr, "FAILED: %s\n", navResult.error.c_str());
        return 1;
    }
    std::printf("  on screen (header=\"%s\")\n", dev.grid().topHeader().c_str());

    // Step 2: Move cursor to the target field.
    std::printf("\n[2] Moving cursor to %s...\n", field.c_str());
    auto cursorResult = moveCursorTo(dev, field, holdMs);
    if (!cursorResult.ok) {
        std::printf("  moveCursorTo failed, using SHIFT+DOWN fallback...\n");
        gotoScreen(dev, targetScreen, holdMs);
        std::string targetUpper = field;
        for (auto& c : targetUpper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        int presses = pressUntil(dev, [&](const ScreenGrid& g) -> bool {
            auto rows = g.mainRows();
            for (auto& [y, text] : rows) {
                std::string t = text;
                for (auto& c : t)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (t.find(targetUpper) != std::string::npos) return true;
            }
            return false;
        }, Key::SHIFT | Key::DOWN, 30, holdMs);
        if (presses < 0) {
            std::fprintf(stderr, "FAILED: could not find field '%s' on screen\n", field.c_str());
            return 1;
        }
        std::printf("  found %s after %d SHIFT+DOWN presses\n", field.c_str(), presses);
    }
    std::printf("  cursor on \"%s\"\n", dev.grid().cursorMainText().c_str());

    // Step 3: Read starting value.
    std::printf("\n[3] Reading starting value...\n");
    dev.readScreen(200, 300);
    std::string startFull = dev.grid().cursorMainText();
    std::string startLabel = firstLine(startFull);
    std::printf("  label: \"%s\"\n", startLabel.c_str());
    std::printf("  full:  \"%s\"\n", startFull.c_str());

    // Step 4: Define candidate masks to test.
    // Only test masks that are likely to edit values (not pure navigation).
    struct Candidate { const char* name; uint8_t mask; const char* desc; };
    std::vector<Candidate> candidates = {
        // Value editing candidates (MOD + arrow is the standard M8 pattern)
        {"SHIFT_UP",       (uint8_t)(Key::SHIFT | Key::UP),     "SHIFT+UP"},
        {"SHIFT_DOWN",     (uint8_t)(Key::SHIFT | Key::DOWN),   "SHIFT+DOWN"},
        {"EDIT_UP",        (uint8_t)(Key::EDIT | Key::UP),      "EDIT+UP"},
        {"EDIT_DOWN",      (uint8_t)(Key::EDIT | Key::DOWN),    "EDIT+DOWN"},
        {"EDIT_LEFT",      (uint8_t)(Key::EDIT | Key::LEFT),    "EDIT+LEFT"},
        {"EDIT_RIGHT",     (uint8_t)(Key::EDIT | Key::RIGHT),   "EDIT+RIGHT"},
        {"OPT_UP",         (uint8_t)(Key::OPT | Key::UP),       "OPT+UP"},
        {"OPT_DOWN",       (uint8_t)(Key::OPT | Key::DOWN),     "OPT+DOWN"},
        {"SHIFT_OPT_UP",   (uint8_t)(Key::SHIFT | Key::OPT | Key::UP),   "SHIFT+OPT+UP"},
        {"SHIFT_OPT_DOWN", (uint8_t)(Key::SHIFT | Key::OPT | Key::DOWN), "SHIFT+OPT+DOWN"},
        {"SHIFT_EDIT_UP",  (uint8_t)(Key::SHIFT | Key::EDIT | Key::UP),  "SHIFT+EDIT+UP"},
        {"SHIFT_EDIT_DOWN",(uint8_t)(Key::SHIFT | Key::EDIT | Key::DOWN),"SHIFT+EDIT+DOWN"},
        {"OPT_EDIT",       (uint8_t)(Key::OPT | Key::EDIT),   "OPT+EDIT"},
        // Navigation (expected to move cursor, included for completeness)
        {"UP",             Key::UP,              "UP arrow"},
        {"DOWN",           Key::DOWN,            "DOWN arrow"},
        {"LEFT",           Key::LEFT,            "LEFT arrow"},
        {"RIGHT",          Key::RIGHT,           "RIGHT arrow"},
    };

    std::vector<GestureResult> results;

    // Step 5: Test each candidate.
    std::printf("\n[4] Testing %zu candidate masks...\n\n", candidates.size());

    for (auto& cand : candidates) {
        // Re-navigate to target field before each test (previous test may have moved cursor).
        gotoScreen(dev, targetScreen, holdMs);
        // Try moveCursorTo first, fall back to pressUntil.
        auto mc = moveCursorTo(dev, field, holdMs);
        if (!mc.ok) {
            std::string targetUpper = field;
            for (auto& c : targetUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            pressUntil(dev, [&](const ScreenGrid& g) -> bool {
                auto rows = g.mainRows();
                for (auto& [y, text] : rows) {
                    std::string t = text;
                    for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    if (t.find(targetUpper) != std::string::npos) return true;
                }
                return false;
            }, Key::SHIFT | Key::DOWN, 30, holdMs);
        }
        dev.readScreen(200, 300);
        std::string beforeFull = dev.grid().cursorMainText();
        std::string beforeLabel = firstLine(beforeFull);

        // Press the candidate mask.
        dev.press(cand.mask, holdMs);
        dev.readScreen(200, 300);
        std::string afterFull = dev.grid().cursorMainText();
        std::string afterLabel = firstLine(afterFull);

        bool changed = (beforeFull != afterFull);
        bool fieldMoved = (fieldName(beforeLabel) != fieldName(afterLabel));
        bool valueEdited = changed && !fieldMoved;

        results.push_back({cand.name, cand.mask, cand.desc, changed, fieldMoved, valueEdited,
                           beforeFull, afterFull});

        const char* icon = valueEdited ? "EDITED" : (fieldMoved ? "MOVED " : "same  ");
        std::printf("  %-20s [0x%02X]  %s  label: \"%s\" -> \"%s\"\n",
                    cand.desc, cand.mask, icon, beforeLabel.c_str(), afterLabel.c_str());
    }

    // Step 6: Re-navigate to field and inspect value post-test.
    std::printf("\n[5] Re-navigating to target field...\n");
    // Navigate away and back to reset cursor position.
    gotoScreen(dev, Screen::SONG, holdMs);
    gotoScreen(dev, targetScreen, holdMs);
    moveCursorTo(dev, field, holdMs);
    dev.readScreen(200, 300);
    std::string restoredFull = dev.grid().cursorMainText();
    std::string restoredLabel = firstLine(restoredFull);
    std::printf("  after reset: \"%s\"\n", restoredLabel.c_str());
    if (restoredFull != startFull) {
        std::printf("\nWARNING: Field '%s' value mutated!\n  Initial: \"%s\"\n  Current: \"%s\"\n",
                    field.c_str(), startFull.c_str(), restoredFull.c_str());
    }

    // Step 7: Analyze and print results.
    std::printf("\n=== RESULTS ===\n");
    std::printf("\nValue edits (same field, value changed):\n");
    int editCount = 0;
    for (auto& r : results) {
        if (r.valueEdited) {
            editCount++;
            std::printf("  %-20s [0x%02X]\n", r.description.c_str(), r.mask);
        }
    }
    if (editCount == 0) {
        std::printf("  (none detected)\n");
    }

    std::printf("\nCursor movements (moved to different field):\n");
    for (auto& r : results) {
        if (r.fieldMoved) {
            std::printf("  %-20s [0x%02X]\n", r.description.c_str(), r.mask);
        }
    }

    std::printf("\nNo change:\n");
    for (auto& r : results) {
        if (!r.changed) {
            std::printf("  %-20s [0x%02X]\n", r.description.c_str(), r.mask);
        }
    }

    // Print the screen for inspection.
    std::printf("\n=== FINAL SCREEN ===\n");
    dev.grid().printText(stdout);

    return 0;
}

static int recordFrames(M8Device& dev, const std::string& outPath, int durationMs) {
    std::printf("recording decoded cell frames for %d ms to %s\n", durationMs, outPath.c_str());
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }

    // Read raw serial data and dump to file.
    auto start = std::chrono::steady_clock::now();
    uint8_t buf[4096];
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (elapsed >= durationMs) break;

        // Use the device's internal port via a read pass.
        dev.read(50, 200);
        // Dump the raw cells as text for now (a proper SLIP recording would
        // capture the raw serial stream, but for offline replay the decoded
        // grid is sufficient).
        const auto& g = dev.grid();
        for (auto& [pos, c] : g.cells) {
            // Write a simple binary record: y(2) x(2) ch(1) fg(3) bg(3)
            uint16_t y = static_cast<uint16_t>(pos.first);
            uint16_t x = static_cast<uint16_t>(pos.second);
            out.write(reinterpret_cast<const char*>(&y), 2);
            out.write(reinterpret_cast<const char*>(&x), 2);
            out.write(reinterpret_cast<const char*>(&c.ch), 1);
            out.write(reinterpret_cast<const char*>(c.fg), 3);
            out.write(reinterpret_cast<const char*>(c.bg), 3);
        }
        // Write a frame separator.
        uint32_t sentinel = 0xFFFFFFFF;
        out.write(reinterpret_cast<const char*>(&sentinel), 4);
    }
    std::printf("recording complete\n");
    return 0;
}

static int emitExit(M8Device& dev, Envelope env, const std::string& jsonPath = "") {
    if (dev.isOpen()) {
        env.readStats = dev.lastRead();
        env.screenName = dev.grid().canon();
        auto cf = dev.cursorField();
        if (cf) {
            env.cursorField = cf->name;
            auto val = dev.valueOf(*cf);
            if (val) env.cursorText = *val;
        }
        if (!jsonPath.empty()) {
            dev.grid().printJson(jsonPath);
        }
        dev.close();
    }
    if (!env.ok() && !env.message.empty()) {
        std::fprintf(stderr, "error (%d): %s\n", env.exitCode(), env.message.c_str());
    }
    env.emit(stdout);
    return env.exitCode();
}

// ---- main -----------------------------------------------------------------


// ---------------------------------------------------------------------------
// --pin-theme: learn the cursor accent by watching a key press.
//
// Pinning by observation rather than assumption. The M8 draws its cursor in the
// theme accent and the display protocol never says which cells those are, so
// every cursor query in the driver is ultimately a colour comparison against a
// value we had to *know* in advance -- and when that value was wrong, the whole
// driver reported a device that ignores keys (hw_findings.md UI-14). Asking the
// device instead removes the class: press a direction key, and the colour whose
// cells move is the cursor, whatever the user has themed it to.
//
// Two colours move, not one: the row the cursor leaves reverts to normal text,
// and the row it arrives at becomes accent. They are told apart by extent -- a
// cursor marks one field or row, while the normal text colour covers most of the
// screen -- so the smallest changed colour is the accent. Candidates are printed
// with their cell counts so the choice is auditable rather than trusted.
// ---------------------------------------------------------------------------
namespace {

using CellKey = std::pair<int, int>;                 // (row, col)
using ColorKey = std::array<uint8_t, 3>;

std::map<ColorKey, std::set<CellKey>> colorCellSets(const m8::dev::UiCapture& cap) {
    std::map<ColorKey, std::set<CellKey>> out;
    for (const auto& c : cap.cells) {
        if (c.fgStyle >= 0 && c.fgStyle < (int)cap.palette.size())
            out[cap.palette[c.fgStyle]].insert({c.row, c.col});
        if (c.bgStyle >= 0 && c.bgStyle < (int)cap.palette.size())
            out[cap.palette[c.bgStyle]].insert({c.row, c.col});
    }
    return out;
}

} // namespace

static int pinTheme(m8::dev::M8Device& dev, m8::dev::Envelope& env,
                    const std::string& path, int minMs, int settleMs, int maxMs) {
    using namespace m8::dev;

    // Direction pairs to try, each with the key that undoes it. The cursor can
    // sit at the end of a chain where DOWN does nothing, so more than one pair
    // is needed before concluding the screen has no cursor at all.
    const struct { const char* name; uint8_t go; uint8_t back; } kProbes[] = {
        {"DOWN", Key::DOWN,  Key::UP},
        {"UP",   Key::UP,    Key::DOWN},
        {"RIGHT",Key::RIGHT, Key::LEFT},
        {"LEFT", Key::LEFT,  Key::RIGHT},
    };

    for (const auto& probe : kProbes) {
        dev.readSettled(minMs, settleMs, maxMs);
        if (dev.grid().cells.empty()) {
            env.code = ExitCode::NO_DATA;
            env.message = "pin-theme: no display data decoded";
            return emitExit(dev, env);
        }
        const UiCapture before = captureFromGrid(dev.grid(), true);

        dev.press(probe.go);
        dev.readSettled(minMs, settleMs, maxMs);
        const UiCapture after = captureFromGrid(dev.grid(), true);

        // Put the cursor back before doing anything else with the result. This
        // matters more than it looks: a cursor left somewhere unexpected is how
        // a later keyjazz capture recorded a note into the wrong phrase row.
        dev.press(probe.back);
        dev.readSettled(minMs, settleMs, maxMs);

        const auto a = colorCellSets(before);
        const auto b = colorCellSets(after);

        std::vector<std::pair<size_t, ColorKey>> moved;   // (extent, colour)
        std::set<ColorKey> seen;
        for (const auto& kv : a) seen.insert(kv.first);
        for (const auto& kv : b) seen.insert(kv.first);
        for (const auto& col : seen) {
            const auto ia = a.find(col);
            const auto ib = b.find(col);
            const std::set<CellKey> empty;
            const std::set<CellKey>& sa = (ia == a.end()) ? empty : ia->second;
            const std::set<CellKey>& sb = (ib == b.end()) ? empty : ib->second;
            if (sa != sb) moved.push_back({sa.empty() ? sb.size() : sa.size(), col});
        }

        if (moved.empty()) {
            std::printf("pin-theme: %s moved nothing; trying the next direction\n",
                        probe.name);
            continue;
        }

        std::sort(moved.begin(), moved.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });

        std::printf("pin-theme: %s moved %zu colour(s):\n", probe.name, moved.size());
        for (const auto& m : moved)
            std::printf("    [%3d,%3d,%3d]  %zu cells%s\n",
                        m.second[0], m.second[1], m.second[2], m.first,
                        (&m == &moved.front()) ? "   <- accent (smallest extent)" : "");

        auto& theme = getTheme();
        theme.accent[0] = moved.front().second[0];
        theme.accent[1] = moved.front().second[1];
        theme.accent[2] = moved.front().second[2];
        theme.pinned = true;
        theme.source = AccentSource::CALIBRATED;
        theme.themeId = before.themeId;
        const Firmware fw = dev.firmware();
        theme.pinnedFwMajor = fw.major;
        theme.pinnedFwMinor = fw.minor;
        theme.pinnedFwPatch = fw.patch;
        theme.method = std::string("pressed ") + probe.name +
                       " and took the smallest colour whose cells moved";

        if (!theme.saveToFile(path)) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = "pin-theme: cannot write " + path;
            return emitExit(dev, env);
        }
        std::printf("pin-theme: wrote %s\n", path.c_str());
        env.message = "theme accent pinned";
        return emitExit(dev, env);
    }

    env.code = ExitCode::COMMAND_FAILED;
    env.message = "pin-theme: no direction key moved any colour -- either the "
                  "screen has no cursor, or the presses are not reaching the device";
    return emitExit(dev, env);
}


int main(int argc, char** argv) {
    std::string port, jsonPath, keysArg, loadFilePath, gotoScreenArg, readFieldArg;
    std::string recordFramesPath, pinGesturesField, scriptPath, findFileArg, loadSongArg, uiCapturePath;
    std::string cursorColorArg;   // "R,G,B" -- theme accent override, see ScreenGrid::cursorColor
    bool pinThemeFlag = false;    // learn the accent from the device instead of assuming it
    std::string recordRawPath;    // dump the pre-SLIP byte stream, for #32
    bool dumpScreen = false, noReset = false, semanticStateFlag = false, serveMode = false, allowMutation = false;
    int maxMs = 2000, settleMs = 250, minMs = 700;
    int holdMs = 40, gapMs = 120;
    int recordDurationMs = 5000;

    int keyjazzNote = -1, keyjazzVel = 0x7F;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--port")            port = next();
        else if (a == "--dump-screen")     dumpScreen = true;
        else if (a == "--semantic-state")  semanticStateFlag = true;
        else if (a == "--serve")           serveMode = true;
        else if (a == "--allow-mutation")  allowMutation = true;
        else if (a == "--find-file")       findFileArg = next();
        else if (a == "--load-song")       loadSongArg = next();
        else if (a == "--ui-capture")      uiCapturePath = next();
        else if (a == "--cursor-color")    cursorColorArg = next();
        else if (a == "--pin-theme")       pinThemeFlag = true;
        else if (a == "--record-raw")      recordRawPath = next();
        else if (a == "--keyjazz")         keyjazzNote = static_cast<int>(std::strtol(next().c_str(), nullptr, 0));
        else if (a == "--keyjazz-vel")     keyjazzVel = static_cast<int>(std::strtol(next().c_str(), nullptr, 0));
        else if (a == "--json")            jsonPath = next();
        else if (a == "--keys")            keysArg = next();
        else if (a == "--load-file")       loadFilePath = next();
        else if (a == "--goto-screen")     gotoScreenArg = next();
        else if (a == "--read-field")      readFieldArg = next();
        else if (a == "--record-frames")   recordFramesPath = next();
        else if (a == "--pin-gestures")    pinGesturesField = next();
        else if (a == "--script")          scriptPath = next();
        else if (a == "--record-duration") recordDurationMs = std::atoi(next().c_str());
        else if (a == "--hold-ms")         holdMs = std::atoi(next().c_str());
        else if (a == "--gap-ms")          gapMs = std::atoi(next().c_str());
        else if (a == "--no-reset")        noReset = true;
        else if (a == "--max-ms")          maxMs = std::atoi(next().c_str());
        else if (a == "--settle-ms")       settleMs = std::atoi(next().c_str());
        else if (a == "--min-ms")          minMs = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (settleMs >= maxMs) {
        std::fprintf(stderr, "error (2): --settle-ms (%d) must be less than --max-ms (%d)\n", settleMs, maxMs);
        return static_cast<int>(ExitCode::UNKNOWN_ARG);
    }

    if (!loadFilePath.empty() && holdMs > 20) holdMs = 15;
    if (port.empty()) {
        std::fprintf(stderr,
            "usage: m8_nav --port COM3 [--dump-screen] [--json out.json]\n"
            "              [--keys 0x40,0x40,0x08] [--hold-ms 40] [--no-reset]\n"
            "              [--load-file probe.m8s] [--goto-screen SCREEN]\n"
            "              [--read-field FIELD] [--record-frames out.bin]\n"
            "              [--pin-gestures FIELD]  # discover edit masks (Tier 2)\n"
            "              [--script FILE.m8script]  # run script against device\n");
        return 1;
    }
    if (!dumpScreen && jsonPath.empty() && keysArg.empty()
        && loadFilePath.empty() && gotoScreenArg.empty() && readFieldArg.empty()
        && recordFramesPath.empty() && pinGesturesField.empty()
        && recordRawPath.empty()
        && scriptPath.empty())
        dumpScreen = true;

    // Parse key sequence.
    std::vector<uint8_t> keys;
    bool keysBad = false;
    std::string keysBadTok;
    if (!keysArg.empty()) {
        // Accept names as well as raw masks. strtol() alone returned 0 for any
        // name, so `--keys UP,UP` pressed mask 0x00 twice -- a bare key release --
        // and reported success. A no-op that claims to have moved the cursor is
        // the same failure shape as the accent bug: the tool is confidently
        // wrong about what it did, and the caller believes it.
        const struct { const char* name; uint8_t mask; } kNamed[] = {
            {"LEFT",  Key::LEFT},  {"UP",    Key::UP},
            {"DOWN",  Key::DOWN},  {"SHIFT", Key::SHIFT},
            {"PLAY",  Key::PLAY},  {"RIGHT", Key::RIGHT},
            {"OPT",   Key::OPT},   {"EDIT",  Key::EDIT},
        };
        size_t pos = 0;
        while (pos < keysArg.size()) {
            size_t comma = keysArg.find(',', pos);
            std::string tok = keysArg.substr(pos, comma == std::string::npos
                               ? std::string::npos : comma - pos);
            if (!tok.empty()) {
                std::string up;
                for (char ch : tok)
                    up += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                uint8_t mask = 0;
                bool found = false;
                for (const auto& k : kNamed) {
                    if (up == k.name) { mask = k.mask; found = true; break; }
                }
                if (!found) {
                    char* endp = nullptr;
                    const long v = std::strtol(tok.c_str(), &endp, 0);
                    // A token must consume entirely, or it was never a number.
                    if (endp && *endp == '\0' && endp != tok.c_str()
                        && v >= 0 && v <= 255) {
                        mask = static_cast<uint8_t>(v);
                        found = true;
                    }
                }
                if (!found) { keysBad = true; keysBadTok = tok; break; }
                keys.push_back(mask);
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }

    Envelope env;

    // Open device.
    M8Device dev;
    if (keysBad) {
        env.code = ExitCode::UNKNOWN_ARG;
        env.message = "--keys: unrecognised key '" + keysBadTok +
                      "' (want a name like UP/DOWN/EDIT or a mask like 0x21)";
        return emitExit(dev, env);
    }
    if (noReset) {
        if (!dev.openNoReset(port.c_str())) {
            env.code = ExitCode::DEVICE_NOT_FOUND;
            env.message = "could not open device port " + port;
            return emitExit(dev, env);
        }
    } else {
        if (!dev.open(port.c_str())) {
            env.code = ExitCode::DEVICE_NOT_FOUND;
            env.message = "could not open device port " + port;
            return emitExit(dev, env);
        }
    }
    std::printf("serial: %s opened @115200\n", port.c_str());

    // Load gesture table from hw_buttons.json (if it exists).
    auto& gestures = getGestures();
    if (gestures.loadFromFile(kDefaultGesturePath)) {
        std::printf("gestures: loaded from %s (fw %d.%d.%d, populated=%s)\n",
                    kDefaultGesturePath,
                    gestures.pinnedFwMajor, gestures.pinnedFwMinor, gestures.pinnedFwPatch,
                    gestures.isReady() ? "true" : "false");
    } else {
        std::printf("gestures: not loaded (file missing or no edit gestures pinned)\n");
    }

    // Theme accent, in precedence order: --cursor-color, then hw_theme.json,
    // then the compiled-in stock value. That last one is a guess and says so --
    // it is the state in which a themed device silently reads as one that has
    // stopped accepting keys, so it must never look like a confirmed setting.
    auto& theme = getTheme();
    if (theme.loadFromFile(kDefaultThemePath)) {
        std::printf("theme: accent [%d,%d,%d] from %s (fw %d.%d.%d, theme_id \"%s\")\n",
                    theme.accent[0], theme.accent[1], theme.accent[2], kDefaultThemePath,
                    theme.pinnedFwMajor, theme.pinnedFwMinor, theme.pinnedFwPatch,
                    theme.themeId.c_str());
    }
    if (!cursorColorArg.empty()) {
        int rgb[3] = {-1, -1, -1};
        if (std::sscanf(cursorColorArg.c_str(), "%d,%d,%d", &rgb[0], &rgb[1], &rgb[2]) != 3
            || rgb[0] < 0 || rgb[0] > 255 || rgb[1] < 0 || rgb[1] > 255
            || rgb[2] < 0 || rgb[2] > 255) {
            env.code = ExitCode::UNKNOWN_ARG;
            env.message = "--cursor-color wants R,G,B with each 0-255, got: " + cursorColorArg;
            return emitExit(dev, env);
        }
        theme.accent[0] = static_cast<uint8_t>(rgb[0]);
        theme.accent[1] = static_cast<uint8_t>(rgb[1]);
        theme.accent[2] = static_cast<uint8_t>(rgb[2]);
        theme.pinned = true;
        theme.source = AccentSource::FLAG;
    }
    if (!theme.isPinned()) {
        std::printf("theme: accent [%d,%d,%d] is the built-in default and is NOT "
                    "confirmed for this device -- run m8_nav --pin-theme\n",
                    theme.accent[0], theme.accent[1], theme.accent[2]);
    }
    dev.setCursorColor(theme.accent[0], theme.accent[1], theme.accent[2]);
    dev.setCursorTolerance(theme.tolerance);

    // Install the raw tap BEFORE the first read, not after.
    //
    // The M8 sends only what changed. open() does E / 500ms / R, and the full
    // repaint that R provokes arrives during the FIRST read -- so a tap
    // installed later sees only the incremental animation and reports "zero
    // draw-character frames", which reads as damning evidence about the device
    // and is entirely an artifact of when the instrument was switched on. That
    // mistake was made and acted on once already; the ordering is the fix.
    std::vector<uint8_t> rawCapture;
    if (!recordRawPath.empty()) dev.setRawTap(&rawCapture);
    dev.readSettled(minMs, settleMs, maxMs);
    if (dev.grid().cells.empty()) {
        env.code = ExitCode::NO_DATA;
        env.message = "port opened but no display data was decoded";
        return emitExit(dev, env);
    }

    Firmware fw = dev.firmware();
    std::printf("device: hw_type=%d  firmware=%d.%d.%d  font_mode=%d\n",
                fw.hwType, fw.major, fw.minor, fw.patch, fw.fontMode);

    if (pinThemeFlag)
        return pinTheme(dev, env, kDefaultThemePath, minMs, settleMs, maxMs);

    // Blindness check. If the accent appears nowhere on a screen that decoded
    // cells, the accent is wrong -- the M8 has not stopped drawing a cursor.
    // Saying so here is the whole point: the failure is otherwise silent, and
    // every downstream symptom (cursor row -1, "the press is not landing")
    // points at the device instead of at this one value. hw_findings UI-14.
    // Two independent health checks, because they catch different failures and
    // the cheap one catches almost nothing. Out-of-range coordinates would mean
    // a stream so broken it produces non-coordinates; the observed corruption
    // (#32) instead produced wrong-but-legal ones, and sailed straight through.
    // The layout check is the one that sees it.
    if (!dev.grid().decodeLooksSane()) {
        std::printf("decode: WARNING -- %d cell(s) far outside the panel, %d short "
                    "frame(s); first was ch=0x%02X at (%d,%d)\n",
                    dev.grid().offPanelCells, dev.grid().shortFrames,
                    dev.grid().firstOffPanelCh, dev.grid().firstOffPanelX,
                    dev.grid().firstOffPanelY);
    }
    {
        const Screen shp = identifyScreen(dev.grid());
        const std::string hint = (shp == Screen::INSTRUMENT)
                               ? readInstrumentType(dev.grid()) : std::string();
        const double match = layoutMatchRatio(dev.grid(), shp, hint);
        if (match >= 0.0 && match < 0.6) {
            std::printf("decode: WARNING -- only %.0f%% of this screen's labels are where "
                        "the map says. The frames are well-formed and our decode is faithful "
                        "to them; the DEVICE is transmitting a displaced framebuffer. Field "
                        "reads will fail in ways that read as navigation bugs. Try navigating "
                        "away and back to force a full repaint. M8_DRIVER_BUGS.md #32.\n",
                        match * 100.0);
        }
    }
    if (!dev.grid().accentPresent()) {
        std::printf("theme: WARNING -- accent [%d,%d,%d] (%s) appears on no cell of "
                    "this screen. Cursor reads will all fail and will look like the "
                    "device is ignoring keys. Fix with: m8_nav --port <PORT> --pin-theme\n",
                    theme.accent[0], theme.accent[1], theme.accent[2],
                    accentSourceName(theme.source));
    }

    if (semanticStateFlag) {
        std::printf("%s\n", semanticState(dev).toJson().c_str());
        return emitExit(dev, env, jsonPath);
    }

    if (serveMode) {
        int daemonRc = runDaemon(dev, holdMs, gapMs, settleMs);
        if (daemonRc != 0) env.code = ExitCode::COMMAND_FAILED;
        return emitExit(dev, env);
    }

    // --find-file mode.
    if (!findFileArg.empty()) {
        env.action = "find-file";
        auto gotoRes = openLoadModal(dev, holdMs);
        if (!gotoRes.ok) {
            env.code = ExitCode::TARGET_UNREACHABLE;
            env.message = "could not reach LOAD PROJECT modal";
            return emitExit(dev, env);
        }
        auto sres = searchTree(dev, findFileArg, 4, 64, holdMs);
        std::printf("find-file \"%s\": %zu matches (dirs_visited=%d, truncated=%s)\n",
                    findFileArg.c_str(), sres.matches.size(), sres.dirsVisited, sres.truncated ? "true" : "false");
        for (const auto& m : sres.matches) {
            std::printf("  - %s\n", m.path.c_str());
        }

        std::string matchesJson = "\"matches\":[";
        for (size_t i = 0; i < sres.matches.size(); ++i) {
            matchesJson += "\"" + jsonEscape(sres.matches[i].path) + "\"";
            if (i + 1 < sres.matches.size()) matchesJson += ",";
        }
        matchesJson += "]";
        env.extra.push_back(matchesJson);
        env.extra.push_back("\"dirs_visited\":" + std::to_string(sres.dirsVisited));
        env.extra.push_back(std::string("\"truncated\":") + (sres.truncated ? "true" : "false"));

        if (sres.matches.size() > 1) {
            env.code = ExitCode::AMBIGUOUS_MATCH;
            env.message = "ambiguous match (" + std::to_string(sres.matches.size()) + " candidates found for '" + findFileArg + "')";
        } else if (sres.matches.empty() && sres.error.empty()) {
            env.code = ExitCode::NOT_FOUND;
            env.message = "no matches found for '" + findFileArg + "'";
        } else if (!sres.error.empty()) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = sres.error;
        }
        return emitExit(dev, env);
    }

    // --load-song mode.
    if (!loadSongArg.empty()) {
        int rc = searchAndLoad(dev, loadSongArg, holdMs);
        if (rc != 0) env.code = static_cast<ExitCode>(rc);
        return emitExit(dev, env);
    }

    // --keyjazz mode.
    if (keyjazzNote >= 0) {
        // Keyjazz is not only audible: on a screen with note cells, the M8
        // writes the note into the cell under the cursor. On 2026-08-18 four
        // capture runs did exactly that -- the cursor happened to be on PHRASE
        // row 3 after an earlier probe, and the last capture velocity 0x7F
        // landed there over the user data (hw_findings.md UI-14).
        //
        // The rule was written down that day and enforced by nothing, which is
        // the same state that let the inverse-video cursor hole sit in a
        // docstring for weeks. So it is a refusal now, opted out of with the
        // flag that already means "I intend to change the device".
        const Screen cur = identifyScreen(dev.grid());
        if (!allowMutation && (cur == Screen::PHRASE || cur == Screen::TABLE)) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message =
                "refusing keyjazz on a note-entry screen: the M8 records the note "
                "into the cell under the cursor, so this would edit the loaded "
                "project. Move to INSTRUMENT or SONG first, or pass --allow-mutation "
                "if overwriting that cell is the intent.";
            return emitExit(dev, env);
        }
        dev.keyjazz(static_cast<uint8_t>(keyjazzNote), static_cast<uint8_t>(keyjazzVel));
        std::printf("keyjazz: note 0x%02X (%d), vel 0x%02X (%d)\n", keyjazzNote, keyjazzNote, keyjazzVel, keyjazzVel);
        return emitExit(dev, env);
    }

    // --ui-capture mode.
    if (!uiCapturePath.empty()) {
        // Confirm the display is stable with a double-read (confirmRead pattern).
        dev.readScreen();
        std::string header1 = dev.grid().topHeader();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        dev.readScreen();
        std::string header2 = dev.grid().topHeader();
        bool isSettled = (header1 == header2) && !dev.grid().cells.empty();
        if (!isSettled) {
            env.code = ExitCode::UNSETTLED_DISPLAY;
            env.message = "cannot capture: display did not settle (screen changed or no cells)";
            return emitExit(dev, env);
        }
        // Settling is not soundness. The check above compares topHeader() only,
        // and on the corrupt read that produced bug #32 the header sat in the
        // undamaged top rows and matched twice -- so a garbled screen was
        // written out with settled: true and believed by everything downstream.
        // Write the file even when the decode is unhealthy, and report the
        // failure through the exit code instead. Refusing outright was the
        // first instinct and it was wrong: it denied the artifact to the one
        // person who needs it -- whoever is diagnosing the corruption -- and a
        // diagnostic tool that goes silent exactly when something is broken is
        // the pattern this whole bug list is about.
        const Screen capScreen = identifyScreen(dev.grid());
        const std::string capHint = (capScreen == Screen::INSTRUMENT)
                                  ? readInstrumentType(dev.grid()) : std::string();
        const double capMatch = layoutMatchRatio(dev.grid(), capScreen, capHint);
        const bool decodeBad = !dev.grid().decodeLooksSane()
                            || (capMatch >= 0.0 && capMatch < 0.6);
        auto cap = captureFromGrid(dev.grid(), true);
        std::ofstream out(uiCapturePath);
        if (!out) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = "cannot open output path: " + uiCapturePath;
            return emitExit(dev, env);
        }
        out << toJson(cap);
        std::printf("wrote ui capture: %s  (screen=%s  cells=%zu  rects=%zu  palette=%zu)\n",
                    uiCapturePath.c_str(), cap.screen.c_str(), cap.cells.size(), cap.rects.size(), cap.palette.size());
        if (decodeBad) {
            env.code = ExitCode::NO_DATA;
            env.message = "capture written, but the decode is unhealthy ("
                        + std::to_string(dev.grid().offPanelCells) + " off-panel, "
                        + std::to_string(dev.grid().shortFrames) + " short frames, "
                        + std::to_string(static_cast<int>(capMatch * 100)) + "% of labels "
                          "in place) -- treat its contents as suspect";
        }
        return emitExit(dev, env);
    }

    // --record-frames mode.
    if (!recordRawPath.empty()) {
        // rawCapture already holds the startup read -- the one carrying the
        // post-reset full repaint. Keep going for a few more so the file also
        // spans the steady-state incremental traffic.
        std::vector<uint8_t>& raw = rawCapture;
        for (int i = 0; i < 5; ++i) dev.readSettled(minMs, settleMs, maxMs);
        dev.setRawTap(nullptr);
        std::ofstream rawOut(recordRawPath, std::ios::binary);
        if (!rawOut) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = "cannot write " + recordRawPath;
            return emitExit(dev, env);
        }
        rawOut.write(reinterpret_cast<const char*>(raw.data()),
                     static_cast<std::streamsize>(raw.size()));
        const Screen rawScreen = identifyScreen(dev.grid());
        const std::string rawHint = (rawScreen == Screen::INSTRUMENT)
                                  ? readInstrumentType(dev.grid()) : std::string();
        const char* rawName = "UNKNOWN";
        for (auto& si : kScreenTable)
            if (si.id == rawScreen) { rawName = si.canonHeader; break; }
        const double rawMatch = layoutMatchRatio(dev.grid(), rawScreen, rawHint);
        char matchTxt[32];
        if (rawMatch < 0.0) std::snprintf(matchTxt, sizeof(matchTxt), "n/a (grid screen)");
        else std::snprintf(matchTxt, sizeof(matchTxt), "%.0f%% of labels in place",
                           rawMatch * 100.0);
        std::printf("wrote %zu raw bytes to %s (screen=%s, %s)\n",
                    raw.size(), recordRawPath.c_str(), rawName, matchTxt);
        return emitExit(dev, env);
    }

    if (!recordFramesPath.empty()) {
        int rc = recordFrames(dev, recordFramesPath, recordDurationMs);
        if (rc != 0) env.code = ExitCode::COMMAND_FAILED;
        return emitExit(dev, env);
    }

    // --pin-gestures mode (Tier 2: discover edit masks empirically).
    if (!pinGesturesField.empty()) {
        if (!allowMutation) {
            env.code = ExitCode::UNKNOWN_ARG;
            env.message = "--pin-gestures requires --allow-mutation flag as it sends edit commands to the device";
            return emitExit(dev, env);
        }
        int rc = pinGestures(dev, pinGesturesField, holdMs);
        if (rc != 0) env.code = ExitCode::COMMAND_FAILED;
        return emitExit(dev, env);
    }

    // --goto-screen mode.
    if (!gotoScreenArg.empty()) {
        Screen target = identifyScreen(toUpper(gotoScreenArg));
        if (target == Screen::UNKNOWN) {
            // Try matching by partial name.
            for (auto& si : kScreenTable) {
                std::string name = toUpper(si.canonHeader);
                if (name.find(toUpper(gotoScreenArg)) != std::string::npos) {
                    target = si.id;
                    break;
                }
            }
        }
        if (target == Screen::UNKNOWN) {
            env.code = ExitCode::AMBIGUOUS_MATCH;
            env.message = "unknown screen: " + gotoScreenArg;
            return emitExit(dev, env);
        }
        auto result = gotoScreen(dev, target, holdMs);
        if (!result.ok) {
            env.code = ExitCode::TARGET_UNREACHABLE;
            env.message = "goto-screen failed: " + result.error;
        }
        dev.grid().printText(stdout);
        return emitExit(dev, env);
    }

    // --script mode.
    if (!scriptPath.empty()) {
        m8::dev::DeviceScriptRunner runner;
        if (!runner.loadScript(scriptPath)) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = "script parse error (line " + std::to_string(runner.lastErrorLine()) + "): " + runner.lastError();
            return emitExit(dev, env);
        }
        std::printf("script: loaded %s (%zu commands)\n", scriptPath.c_str(),
                     runner.loadScript_count());
        int rc = runner.run(dev, holdMs);
        if (rc != 0) {
            env.code = ExitCode::COMMAND_FAILED;
            env.message = "script FAILED (line " + std::to_string(runner.lastErrorLine()) + "): " + runner.lastError();
        } else {
            std::printf("script: PASSED\n");
        }
        return emitExit(dev, env);
    }

    // --read-field mode.
    if (!readFieldArg.empty()) {
        auto val = readField(dev, readFieldArg, holdMs);
        if (val) {
            std::printf("%s = %s\n", readFieldArg.c_str(), val->c_str());
        } else {
            env.code = ExitCode::NOT_FOUND;
            env.message = "could not read field: " + readFieldArg;
        }
        return emitExit(dev, env);
    }

    // --load-file mode.
    if (!loadFilePath.empty()) {
        int rc = loadFile(dev, loadFilePath, holdMs);
        std::printf("nav: %s (rc=%d), final header=\"%s\"\n",
                    rc == 0 ? "LOADED" : "FAILED", rc, dev.grid().topHeader().c_str());
        if (rc != 0) env.code = ExitCode::COMMAND_FAILED;
        if (dumpScreen) dev.grid().printText(stdout);
    }

    // Default: dump screen.
    if (dumpScreen && keys.empty() && loadFilePath.empty()) {
        dev.grid().printText(stdout);
    }

    // --keys mode (manual key sequence).
    for (size_t k = 0; k < keys.size(); ++k) {
        std::printf("\n=== press 0x%02X (%zu/%zu) ===\n", keys[k], k + 1, keys.size());
        dev.press(keys[k], holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
        dev.readSettled(120, settleMs, maxMs);
        dev.grid().printText(stdout);
    }

    if (dev.grid().cells.empty()) {
        env.code = ExitCode::NO_DATA;
        env.message = "no characters decoded — is the device connected and streaming?";
    }

    return emitExit(dev, env, jsonPath);
}
