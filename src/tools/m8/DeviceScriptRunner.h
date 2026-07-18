#pragma once

// ===========================================================================
// ScriptRunner.h — device-side script runner for m8_nav.
//
// Tier 4 of M8_DEVICE_CONTROL_SPEC.md. Parses .m8script files (the same
// dialect as the clone's ScriptRunner) and executes them against a real M8
// headless via M8Device. Supports key, hold, wait, goto, set, note, load,
// save, dump_screen, dump_json, and assert_* commands.
//
// Exit codes: 0 = pass, 1 = assertion fail, 2 = parse error.
// ===========================================================================

#include "M8Device.h"
#include "ScreenModel.h"
#include <string>
#include <vector>
#include <cstdint>

namespace m8 {
namespace dev {

class DeviceScriptRunner {
public:
    // Load a script file. Returns false on parse error or file-not-found.
    bool loadScript(const std::string& path);

    // Execute all loaded commands against the device.
    // Returns the exit code (0=pass, 1=assert fail, 2=parse error).
    int run(M8Device& dev, int holdMs = 40);

    // Accessors for diagnostics.
    int exitCode() const { return m_exitCode; }
    const std::string& lastError() const { return m_lastError; }
    int lastErrorLine() const { return m_lastErrorLine; }
    size_t loadScript_count() const { return m_commands.size(); }

private:
    struct Command {
        int lineNum = 0;
        std::string verb;
        std::string arg1;
        std::string arg2;
        std::string arg3;
        std::vector<std::string> extraArgs;  // for compound key: X+UP etc.
    };

    std::vector<Command> m_commands;
    int m_exitCode = 0;
    std::string m_lastError;
    int m_lastErrorLine = 0;

    // Map a button name (UP/DOWN/LEFT/RIGHT/X/Z/SHIFT/SPACE) to an M8 key mask.
    static uint8_t mapButton(const std::string& name);

    // Map a screen name string to the Screen enum.
    static Screen mapScreen(const std::string& name);

    // Execute one command. Returns false on assertion failure.
    bool execCommand(M8Device& dev, const Command& cmd, int holdMs);

    // Auto-dump screen on assertion failure.
    void autoDump(M8Device& dev, const std::string& tag);

    void setError(int line, const std::string& msg) {
        m_exitCode = 2;
        m_lastError = msg;
        m_lastErrorLine = line;
    }

    void assertFail(M8Device& dev, int line, const std::string& msg) {
        m_exitCode = 1;
        m_lastError = msg;
        m_lastErrorLine = line;
        autoDump(dev, "assert_fail");
    }
};

} // namespace dev
} // namespace m8
