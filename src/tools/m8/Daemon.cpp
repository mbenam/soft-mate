#include "Daemon.h"
#include "Semantic.h"
#include "Primitives.h"
#include "Result.h"
#include "DeviceScriptRunner.h"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>

namespace m8 {
namespace dev {

static std::string toUpperLocal(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::map<std::string, std::string> parseKvParams(const std::string& line, std::string& verbOut) {
    std::map<std::string, std::string> params;
    std::istringstream iss(line);
    iss >> verbOut;
    std::string tok;
    while (iss >> tok) {
        size_t eq = tok.find('=');
        if (eq != std::string::npos) {
            std::string k = tok.substr(0, eq);
            std::string v = tok.substr(eq + 1);
            params[k] = v;
        } else {
            params[tok] = "";
        }
    }
    return params;
}

static uint8_t parseKeyMask(const std::string& val) {
    if (val.empty()) return 0;
    if (val.find_first_not_of("0123456789abcdefABCDEFxX") == std::string::npos) {
        return static_cast<uint8_t>(std::strtol(val.c_str(), nullptr, 0));
    }
    std::string u = toUpperLocal(val);
    uint8_t m = 0;
    if (u.find("SHIFT") != std::string::npos) m |= Key::SHIFT;
    if (u.find("EDIT")  != std::string::npos) m |= Key::EDIT;
    if (u.find("OPT")   != std::string::npos) m |= Key::OPT;
    if (u.find("UP")    != std::string::npos) m |= Key::UP;
    if (u.find("DOWN")  != std::string::npos) m |= Key::DOWN;
    if (u.find("LEFT")  != std::string::npos) m |= Key::LEFT;
    if (u.find("RIGHT") != std::string::npos) m |= Key::RIGHT;
    return m;
}

int runDaemon(M8Device& dev, int defaultHoldMs, int defaultGapMs, int defaultSettleMs) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::string verb;
        auto params = parseKvParams(line, verb);
        std::string verbU = toUpperLocal(verb);

        if (verbU == "QUIT" || verbU == "EXIT") {
            std::printf("{\"ok\":true,\"message\":\"quitting\"}\n");
            std::fflush(stdout);
            break;
        }

        ExitCode code = ExitCode::SUCCESS;
        std::string errMessage;

        int holdMs = defaultHoldMs;
        if (params.count("hold")) holdMs = std::atoi(params["hold"].c_str());

        if (verbU == "PRESS") {
            uint8_t k = parseKeyMask(params["key"]);
            if (k == 0 && params["key"] != "0" && params["key"] != "0x00") {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "invalid key parameter: " + params["key"];
            } else {
                dev.press(k, holdMs);
                dev.readSettled(120, 150, defaultSettleMs);
            }
        } else if (verbU == "GOTO") {
            std::string scName = params["screen"];
            Screen target = identifyScreen(toUpperLocal(scName));
            if (target == Screen::UNKNOWN) {
                code = ExitCode::AMBIGUOUS_MATCH;
                errMessage = "unknown target screen: " + scName;
            } else {
                auto res = gotoScreen(dev, target, holdMs);
                if (!res.ok) {
                    code = ExitCode::TARGET_UNREACHABLE;
                    errMessage = res.error;
                }
            }
        } else if (verbU == "CURSOR") {
            std::string field = params["field"];
            auto res = moveCursorTo(dev, field, holdMs);
            if (!res.ok) {
                code = ExitCode::TARGET_UNREACHABLE;
                errMessage = res.error;
            }
        } else if (verbU == "READ") {
            std::string field = params["field"];
            auto val = readField(dev, field, holdMs);
            if (!val) {
                code = ExitCode::TARGET_UNREACHABLE;
                errMessage = "field not found: " + field;
            }
        } else if (verbU == "LOAD") {
            std::string path = params["path"];
            if (path.empty()) path = params["file"];
            int rc = loadFile(dev, path, holdMs);
            if (rc != 0) {
                code = ExitCode::COMMAND_FAILED;
                errMessage = "loadFile failed with rc=" + std::to_string(rc);
            }
        } else if (verbU == "SCRIPT") {
            std::string path = params["path"];
            DeviceScriptRunner runner;
            if (!runner.loadScript(path)) {
                code = ExitCode::COMMAND_FAILED;
                errMessage = "script parse error: " + runner.lastError();
            } else {
                int rc = runner.run(dev, holdMs);
                if (rc != 0) {
                    code = ExitCode::COMMAND_FAILED;
                    errMessage = "script execution failed: " + runner.lastError();
                }
            }
        } else if (verbU == "STATE" || verbU == "SEMANTIC_STATE") {
            // Refreshes read state and outputs semantic state
            dev.readSettled(120, 150, defaultSettleMs);
        } else {
            code = ExitCode::UNKNOWN_ARG;
            errMessage = "unknown daemon verb: " + verb;
        }

        auto semState = semanticState(dev);
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"ok\": " << (code == ExitCode::SUCCESS ? "true" : "false") << ",\n";
        ss << "  \"code\": " << static_cast<int>(code) << ",\n";
        if (!errMessage.empty()) {
            ss << "  \"error\": \"" << errMessage << "\",\n";
        }
        ss << "  \"state\": " << semState.toJson() << "\n";
        ss << "}";
        std::printf("%s\n", ss.str().c_str());
        std::fflush(stdout);
    }
    return 0;
}

} // namespace dev
} // namespace m8
