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
};

struct Envelope {
    ExitCode    code        = ExitCode::SUCCESS;
    std::string message;
    ReadStats   readStats;
    std::string screenName;
    std::string cursorField;
    std::string cursorText;

    bool ok() const { return code == ExitCode::SUCCESS; }
    int  exitCode() const { return static_cast<int>(code); }
};

} // namespace dev
} // namespace m8
