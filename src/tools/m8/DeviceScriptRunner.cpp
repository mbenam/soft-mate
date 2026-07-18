// ===========================================================================
// DeviceScriptRunner.cpp — device-side script runner for m8_nav.
//
// Tier 4 of M8_DEVICE_CONTROL_SPEC.md.
// ===========================================================================

#include "DeviceScriptRunner.h"
#include "Gestures.h"
#include "Primitives.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cctype>

namespace m8 {
namespace dev {

// ---- Helpers ---------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

// ---- Button name → M8 key mask --------------------------------------------

uint8_t DeviceScriptRunner::mapButton(const std::string& name) {
    std::string u = toUpper(name);
    if (u == "UP")    return Key::UP;
    if (u == "DOWN")  return Key::DOWN;
    if (u == "LEFT")  return Key::LEFT;
    if (u == "RIGHT") return Key::RIGHT;
    if (u == "X")     return Key::EDIT;
    if (u == "Z")     return Key::OPT;
    if (u == "SHIFT") return Key::SHIFT;
    if (u == "SPACE") return Key::PLAY;
    return 0;  // unknown
}

// ---- Screen name → enum ----------------------------------------------------

Screen DeviceScriptRunner::mapScreen(const std::string& name) {
    std::string u = toUpper(name);
    if (u == "SONG")          return Screen::SONG;
    if (u == "CHAIN")         return Screen::CHAIN;
    if (u == "PHRASE")        return Screen::PHRASE;
    if (u == "INSTRUMENT" || u == "INST" || u == "INST.")
        return Screen::INSTRUMENT;
    if (u == "TABLE")         return Screen::TABLE;
    if (u == "PROJECT")       return Screen::PROJECT;
    if (u == "GROOVE")        return Screen::GROOVE;
    if (u == "SCALE")         return Screen::SCALE;
    if (u == "MODS" || u == "MOD") return Screen::MODS;
    if (u == "INST_POOL" || u == "INSTPOOL")
        return Screen::INST_POOL;
    if (u == "MIXER")         return Screen::MIXER;
    if (u == "EFFECTS")       return Screen::EFFECTS;
    return Screen::UNKNOWN;
}

// ---- Script loading --------------------------------------------------------

bool DeviceScriptRunner::loadScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        setError(0, "cannot open script file: " + path);
        return false;
    }

    m_commands.clear();
    m_exitCode = 0;
    m_lastError.clear();
    m_lastErrorLine = 0;

    std::string line;
    int lineNum = 0;
    while (std::getline(in, line)) {
        lineNum++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        // Parse: verb [arg1] [arg2] [arg3...]
        std::istringstream iss(trimmed);
        Command cmd;
        cmd.lineNum = lineNum;
        if (!(iss >> cmd.verb)) continue;

        // For compound key commands (e.g. "key X+UP"), read the rest as-is
        // and split on '+'
        if (toUpper(cmd.verb) == "KEY" || toUpper(cmd.verb) == "HOLD") {
            std::string rest;
            iss >> rest;
            // Split on '+'
            std::istringstream rs(rest);
            std::string part;
            while (std::getline(rs, part, '+')) {
                if (cmd.arg1.empty()) cmd.arg1 = part;
                else if (cmd.arg2.empty()) cmd.arg2 = part;
                else cmd.extraArgs.push_back(part);
            }
        } else if (toUpper(cmd.verb) == "GOTO") {
            iss >> cmd.arg1;
        } else if (toUpper(cmd.verb) == "SET") {
            iss >> cmd.arg1 >> cmd.arg2;
        } else if (toUpper(cmd.verb) == "NOTE") {
            iss >> cmd.arg1;
            // optional velocity
            std::string vel;
            if (iss >> vel) cmd.arg2 = vel;
        } else if (toUpper(cmd.verb) == "WAIT") {
            iss >> cmd.arg1;
        } else if (toUpper(cmd.verb) == "LOAD") {
            iss >> cmd.arg1;
        } else if (toUpper(cmd.verb) == "DUMP_SCREEN") {
            // no args needed for device dump
        } else if (toUpper(cmd.verb) == "DUMP_JSON") {
            iss >> cmd.arg1;
        } else if (toUpper(cmd.verb) == "CURSOR") {
            iss >> cmd.arg1;
        } else if (toUpper(cmd.verb) == "ASSERT_SCREEN") {
            iss >> cmd.arg1 >> cmd.arg2;
            cmd.arg2 = stripQuotes(cmd.arg2);
        } else if (toUpper(cmd.verb) == "ASSERT_FIELD") {
            iss >> cmd.arg1 >> cmd.arg2;
            cmd.arg2 = stripQuotes(cmd.arg2);
        } else if (toUpper(cmd.verb) == "ASSERT_ROW_MATCHES") {
            iss >> cmd.arg1;
            // Read the quoted string
            std::string rest;
            std::getline(iss, rest);
            rest = trim(rest);
            // Strip quotes
            if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
                char q = rest[0];
                rest = rest.substr(1);
                size_t end = rest.find(q);
                if (end != std::string::npos) rest = rest.substr(0, end);
            }
            cmd.arg2 = rest;
        } else if (toUpper(cmd.verb) == "ASSERT_PLAYING") {
            // no args
        } else if (toUpper(cmd.verb) == "PLAY") {
            // no args
        } else if (toUpper(cmd.verb) == "STOP") {
            // no args
        } else {
            setError(lineNum, "unknown command: " + cmd.verb);
            return false;
        }

        m_commands.push_back(std::move(cmd));
    }
    return true;
}

// ---- Auto-dump on failure --------------------------------------------------

void DeviceScriptRunner::autoDump(M8Device& dev, const std::string& tag) {
    std::string base = tag;
    dev.grid().printText(stdout);
    fprintf(stderr, "[script] auto-dump triggered at line %d\n", m_lastErrorLine);
}

// ---- Execute one command ---------------------------------------------------

bool DeviceScriptRunner::execCommand(M8Device& dev, const Command& cmd, int holdMs) {
    std::string verb = toUpper(cmd.verb);

    if (verb == "KEY") {
        // Parse button names, compose mask
        uint8_t mask = 0;
        std::string btn = toUpper(cmd.arg1);
        if (!btn.empty()) {
            mask |= mapButton(btn);
        }
        if (!cmd.arg2.empty()) {
            mask |= mapButton(toUpper(cmd.arg2));
        }
        for (auto& extra : cmd.extraArgs) {
            mask |= mapButton(toUpper(extra));
        }
        if (mask == 0) {
            setError(cmd.lineNum, "unknown button in key command: " + cmd.arg1);
            return false;
        }
        dev.press(mask, holdMs);
        dev.step(150, 500);
        return true;
    }

    if (verb == "HOLD") {
        uint8_t mask = mapButton(toUpper(cmd.arg1));
        if (mask == 0) {
            setError(cmd.lineNum, "unknown button in hold command: " + cmd.arg1);
            return false;
        }
        int frames = 0;
        try { frames = std::stoi(cmd.arg2); } catch (...) {
            setError(cmd.lineNum, "invalid frame count: " + cmd.arg2);
            return false;
        }
        // Hold for N * 16ms (approximate frame time)
        dev.press(mask, frames * 16);
        dev.step(150, 500);
        return true;
    }

    if (verb == "WAIT") {
        int ms = 0;
        try { ms = std::stoi(cmd.arg1); } catch (...) {
            setError(cmd.lineNum, "invalid wait value: " + cmd.arg1);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        dev.step(100, 500);
        return true;
    }

    if (verb == "GOTO") {
        Screen target = mapScreen(cmd.arg1);
        if (target == Screen::UNKNOWN) {
            setError(cmd.lineNum, "unknown screen: " + cmd.arg1);
            return false;
        }
        auto result = gotoScreen(dev, target, holdMs);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, "goto failed: " + result.error);
            return false;
        }
        return true;
    }

    if (verb == "CURSOR") {
        auto result = moveCursorTo(dev, cmd.arg1, holdMs);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, "cursor failed: " + result.error);
            return false;
        }
        return true;
    }

    if (verb == "SET") {
        auto result = editValue(dev, cmd.arg1, cmd.arg2, holdMs);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, "set failed: " + result.error);
            return false;
        }
        return true;
    }

    if (verb == "NOTE") {
        uint8_t vel = 0xFF;
        if (!cmd.arg2.empty()) {
            try { vel = static_cast<uint8_t>(std::stoi(cmd.arg2)); } catch (...) {}
        }
        auto result = enterNote(dev, cmd.arg1, vel, holdMs);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, "note failed: " + result.error);
            return false;
        }
        return true;
    }

    if (verb == "LOAD") {
        int rc = loadFile(dev, cmd.arg1, holdMs);
        if (rc != 0) {
            setError(cmd.lineNum, "load failed (rc=" + std::to_string(rc) + "): " + cmd.arg1);
            return false;
        }
        return true;
    }

    if (verb == "PLAY") {
        dev.playToggle();
        dev.step(200, 500);
        return true;
    }

    if (verb == "STOP") {
        dev.playToggle();  // toggle stops if playing
        dev.step(200, 500);
        return true;
    }

    if (verb == "DUMP_SCREEN") {
        dev.grid().printText(stdout);
        return true;
    }

    if (verb == "DUMP_JSON") {
        std::string path = cmd.arg1.empty() ? "screen.json" : cmd.arg1;
        dev.grid().printJson(path);
        fprintf(stderr, "[script] wrote %s\n", path.c_str());
        return true;
    }

    if (verb == "ASSERT_SCREEN") {
        std::string subcmd = toUpper(cmd.arg1);
        if (subcmd == "CONTAINS") {
            dev.readScreen();
            std::string header = dev.grid().canon();
            std::string target = toUpper(cmd.arg2);
            if (header.find(target) == std::string::npos) {
                assertFail(dev, cmd.lineNum,
                    "assert_screen contains: '" + target + "' not found, got '" + header + "'");
                return false;
            }
            return true;
        }
        // assert_screen <header> — check header matches
        Screen expected = mapScreen(cmd.arg1);
        if (expected == Screen::UNKNOWN) {
            // Try as substring
            dev.readScreen();
            std::string header = dev.grid().topHeader();
            std::string target = toUpper(cmd.arg1);
            if (toUpper(header).find(target) == std::string::npos) {
                assertFail(dev, cmd.lineNum,
                    "assert_screen: expected '" + target + "', got '" + header + "'");
                return false;
            }
            return true;
        }
        auto result = assertScreen(dev, expected);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, result.error);
            return false;
        }
        return true;
    }

    if (verb == "ASSERT_FIELD") {
        auto result = assertField(dev, cmd.arg1, cmd.arg2, holdMs);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, result.error);
            return false;
        }
        return true;
    }

    if (verb == "ASSERT_ROW_MATCHES") {
        int row = -1;
        try { row = std::stoi(cmd.arg1); } catch (...) {
            setError(cmd.lineNum, "invalid row number: " + cmd.arg1);
            return false;
        }
        auto result = assertRowMatches(dev, row, cmd.arg2);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, result.error);
            return false;
        }
        return true;
    }

    if (verb == "ASSERT_PLAYING") {
        auto result = assertPlaying(dev);
        if (!result.ok) {
            assertFail(dev, cmd.lineNum, result.error);
            return false;
        }
        return true;
    }

    setError(cmd.lineNum, "unhandled command: " + cmd.verb);
    return false;
}

// ---- Run all commands ------------------------------------------------------

int DeviceScriptRunner::run(M8Device& dev, int holdMs) {
    if (m_commands.empty()) {
        m_exitCode = 0;
        return 0;
    }

    for (auto& cmd : m_commands) {
        bool ok = execCommand(dev, cmd, holdMs);
        if (!ok) return m_exitCode;
    }

    return 0;  // all passed
}

} // namespace dev
} // namespace m8
