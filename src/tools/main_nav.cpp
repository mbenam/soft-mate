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
//   m8_nav --port COM3 --record-frames out.bin  # record raw SLIP frames
//
// Serial only. No engine, no SDL, no audio.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/Gestures.h"
#include "m8/DeviceScriptRunner.h"

using namespace m8::dev;

static const char* kDefaultGesturePath = "hw_buttons.json";

static std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
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
            return 2;
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

    // Step 6: Undo any edits — navigate back to the original field.
    std::printf("\n[5] Restoring state...\n");
    // Navigate away and back to reset.
    gotoScreen(dev, Screen::SONG, holdMs);
    gotoScreen(dev, targetScreen, holdMs);
    moveCursorTo(dev, field, holdMs);
    dev.readScreen(200, 300);
    std::string restoredFull = dev.grid().cursorMainText();
    std::printf("  after reset: \"%s\"\n", firstLine(restoredFull).c_str());

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
    std::printf("recording SLIP frames for %d ms to %s\n", durationMs, outPath.c_str());
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

// ---- main -----------------------------------------------------------------

int main(int argc, char** argv) {
    std::string port, jsonPath, keysArg, loadFilePath, gotoScreenArg, readFieldArg;
    std::string recordFramesPath, pinGesturesField, scriptPath;
    bool dumpScreen = false, noReset = false;
    int maxMs = 2000, settleMs = 250, minMs = 700;
    int holdMs = 40, gapMs = 120;
    int recordDurationMs = 5000;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--port")            port = next();
        else if (a == "--dump-screen")     dumpScreen = true;
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
        && scriptPath.empty())
        dumpScreen = true;

    // Parse key sequence.
    std::vector<uint8_t> keys;
    if (!keysArg.empty()) {
        size_t pos = 0;
        while (pos < keysArg.size()) {
            size_t comma = keysArg.find(',', pos);
            std::string tok = keysArg.substr(pos, comma == std::string::npos
                               ? std::string::npos : comma - pos);
            if (!tok.empty())
                keys.push_back(static_cast<uint8_t>(std::strtol(tok.c_str(), nullptr, 0)));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }

    // Open device.
    M8Device dev;
    if (noReset) {
        if (!dev.openNoReset(port.c_str())) return 2;
    } else {
        if (!dev.open(port.c_str())) return 2;
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

    dev.readScreen(minMs, settleMs);
    Firmware fw = dev.firmware();
    std::printf("device: hw_type=%d  firmware=%d.%d.%d  font_mode=%d\n",
                fw.hwType, fw.major, fw.minor, fw.patch, fw.fontMode);

    int rc = 0;

    // --record-frames mode.
    if (!recordFramesPath.empty()) {
        rc = recordFrames(dev, recordFramesPath, recordDurationMs);
        dev.close();
        return rc;
    }

    // --pin-gestures mode (Tier 2: discover edit masks empirically).
    if (!pinGesturesField.empty()) {
        rc = pinGestures(dev, pinGesturesField, holdMs);
        dev.close();
        return rc;
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
            std::fprintf(stderr, "unknown screen: %s\n", gotoScreenArg.c_str());
            dev.close();
            return 1;
        }
        auto result = gotoScreen(dev, target, holdMs);
        if (!result.ok) {
            std::fprintf(stderr, "goto-screen failed: %s\n", result.error.c_str());
            rc = 1;
        }
        dev.grid().printText(stdout);
        dev.close();
        return rc;
    }

    // --script mode.
    if (!scriptPath.empty()) {
        m8::dev::DeviceScriptRunner runner;
        if (!runner.loadScript(scriptPath)) {
            std::fprintf(stderr, "script parse error (line %d): %s\n",
                         runner.lastErrorLine(), runner.lastError().c_str());
            dev.close();
            return 2;
        }
        std::printf("script: loaded %s (%zu commands)\n", scriptPath.c_str(),
                     runner.loadScript_count());
        rc = runner.run(dev, holdMs);
        if (rc != 0) {
            std::fprintf(stderr, "script FAILED (line %d): %s\n",
                         runner.lastErrorLine(), runner.lastError().c_str());
        } else {
            std::printf("script: PASSED\n");
        }
        dev.close();
        return rc;
    }

    // --read-field mode.
    if (!readFieldArg.empty()) {
        auto val = readField(dev, readFieldArg, holdMs);
        if (val) {
            std::printf("%s = %s\n", readFieldArg.c_str(), val->c_str());
        } else {
            std::fprintf(stderr, "could not read field: %s\n", readFieldArg.c_str());
            rc = 1;
        }
        dev.close();
        return rc;
    }

    // --load-file mode.
    if (!loadFilePath.empty()) {
        rc = loadFile(dev, loadFilePath, holdMs);
        std::printf("nav: %s (rc=%d), final header=\"%s\"\n",
                    rc == 0 ? "LOADED" : "FAILED", rc, dev.grid().topHeader().c_str());
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
        dev.readScreen(150, settleMs);
        dev.grid().printText(stdout);
    }

    if (!jsonPath.empty()) {
        dev.grid().printJson(jsonPath);
        std::printf("wrote %s\n", jsonPath.c_str());
    }
    if (dev.grid().cells.empty()) {
        std::fprintf(stderr, "WARNING: no characters decoded — is the device connected and streaming?\n");
        return 3;
    }

    dev.close();
    return rc;
}
