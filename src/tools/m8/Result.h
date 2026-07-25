#pragma once
#include <string>
#include <vector>
#include "M8Device.h"

namespace m8 {
namespace dev {

enum class ExitCode {
    SUCCESS            = 0,
    DEVICE_NOT_FOUND   = 1,
    UNKNOWN_ARG        = 2,
    UNSETTLED_DISPLAY  = 3,
    COMMAND_FAILED     = 4,
    TIMED_OUT          = 5,
    AMBIGUOUS_MATCH    = 6,
    TARGET_UNREACHABLE = 7,
    NOT_FOUND          = 8,
    NO_DATA            = 9,
};

static inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

struct Envelope {
    ExitCode    code        = ExitCode::SUCCESS;
    std::string action;
    std::string message;
    ReadStats   readStats;
    std::string screenName;
    std::string cursorField;
    std::string cursorText;
    // Pre-formatted "key":<json-value> pairs appended to the emitted object.
    std::vector<std::string> extra;

    bool ok() const { return code == ExitCode::SUCCESS; }
    int  exitCode() const { return static_cast<int>(code); }

    void emit(FILE* out = stdout) const {
        std::fprintf(out, "M8NAV_RESULT {");
        std::fprintf(out, "\"ok\":%s", ok() ? "true" : "false");
        std::fprintf(out, ",\"code\":%d", exitCode());
        if (!action.empty()) std::fprintf(out, ",\"action\":\"%s\"", jsonEscape(action).c_str());
        if (!message.empty()) std::fprintf(out, ",\"message\":\"%s\"", jsonEscape(message).c_str());
        if (!screenName.empty()) std::fprintf(out, ",\"screen\":\"%s\"", jsonEscape(screenName).c_str());
        if (!cursorField.empty()) std::fprintf(out, ",\"cursor_field\":\"%s\"", jsonEscape(cursorField).c_str());
        if (!cursorText.empty()) std::fprintf(out, ",\"cursor_text\":\"%s\"", jsonEscape(cursorText).c_str());
        std::fprintf(out, ",\"settled\":%s", readStats.settled ? "true" : "false");
        std::fprintf(out, ",\"read_ms\":%d", readStats.elapsedMs);
        for (const auto& e : extra) std::fprintf(out, ",%s", e.c_str());
        std::fprintf(out, "}\n");
    }
};

} // namespace dev
} // namespace m8
