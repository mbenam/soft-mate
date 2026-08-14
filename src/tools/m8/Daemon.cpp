#include "Daemon.h"
#include "Semantic.h"
#include "Primitives.h"
#include "Result.h"
#include "DeviceScriptRunner.h"
#include "UiCapture.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <thread>
#include <chrono>

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
                JsonResult res = (target == Screen::LOAD_PROJECT_MODAL)
                    ? openLoadModal(dev, holdMs)
                    : gotoScreen(dev, target, holdMs);
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
                code = ExitCode::NOT_FOUND;
                errMessage = "field not found: " + field;
            }
        } else if (verbU == "SET") {
            std::string field = params["field"];
            std::string value = params.count("value") ? params["value"] : params["to"];
            if (field.empty() || value.empty()) {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "SET requires field= and value=";
            } else {
                auto res = editValue(dev, field, value, holdMs);
                if (!res.ok) {
                    code = ExitCode::COMMAND_FAILED;
                    errMessage = res.error;
                }
            }
        } else if (verbU == "NOTE") {
            std::string name = params.count("name") ? params["name"] : params["note"];
            uint8_t vel = 0xFF;
            if (params.count("vel")) vel = static_cast<uint8_t>(std::atoi(params["vel"].c_str()));
            if (name.empty()) {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "NOTE requires name=";
            } else {
                auto res = enterNote(dev, name, vel, holdMs);
                if (!res.ok) {
                    code = ExitCode::COMMAND_FAILED;
                    errMessage = res.error;
                }
            }
        } else if (verbU == "KEYJAZZ") {
            if (!params.count("note")) {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "KEYJAZZ requires note=";
            } else {
                int note = std::atoi(params["note"].c_str());
                int vel  = params.count("vel") ? std::atoi(params["vel"].c_str()) : 0x7F;
                if (note < 0 || note > 127 || vel < 0 || vel > 255) {
                    code = ExitCode::UNKNOWN_ARG;
                    errMessage = "KEYJAZZ note must be 0..127 and vel 0..255";
                } else {
                    dev.keyjazz(static_cast<uint8_t>(note), static_cast<uint8_t>(vel));
                    dev.readSettled(120, 150, defaultSettleMs);
                }
            }
        } else if (verbU == "HOME") {
            // Watchdog recovery. `confirm=1` opts into pressing EDIT on a modal
            // that will not cancel; the default refuses to, so an unattended
            // driver can never commit an edit it did not intend.
            const bool confirmModals = params.count("confirm")
                                    && params["confirm"] != "0";
            int maxUp = params.count("maxup") ? std::atoi(params["maxup"].c_str()) : 12;
            auto res = panicHome(dev, holdMs, maxUp, confirmModals);
            if (!res.ok) {
                code = ExitCode::COMMAND_FAILED;
                errMessage = res.error;
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
        } else if (verbU == "CAPTURE") {
            std::string path = params["path"];
            if (path.empty()) path = params["out"];
            if (path.empty()) {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "CAPTURE requires path parameter";
            } else {
                dev.readScreen();
                std::string header1 = dev.grid().topHeader();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                dev.readScreen();
                std::string header2 = dev.grid().topHeader();
                bool isSettled = (header1 == header2) && !dev.grid().cells.empty();
                if (!isSettled) {
                    code = ExitCode::UNSETTLED_DISPLAY;
                    errMessage = "cannot capture: display did not settle";
                } else {
                    auto cap = captureFromGrid(dev.grid(), true);
                    std::ofstream out(path);
                    if (!out) {
                        code = ExitCode::COMMAND_FAILED;
                        errMessage = "cannot open output path: " + path;
                    } else {
                        out << toJson(cap);
                    }
                }
            }
        } else if (verbU == "STATE" || verbU == "SEMANTIC_STATE") {
            // Refreshes read state and outputs semantic state
            dev.readSettled(120, 150, defaultSettleMs);
        } else if (verbU == "FIELDS") {
            std::string scName = params["screen"];
            if (scName.empty()) scName = dev.grid().canon();
            Screen target = identifyScreen(toUpperLocal(scName));
            if (target == Screen::UNKNOWN) {
                code = ExitCode::UNKNOWN_ARG;
                errMessage = "unknown screen: " + scName;
            } else {
                auto fm = getFieldMap(target);
                std::vector<std::string> fnames;
                if (fm.isGrid) {
                    for (int step = 0; step < 16; ++step) {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "STEP%X", step);
                        fnames.push_back(buf);
                    }
                } else if (fm.fields != nullptr) {
                    for (size_t i = 0; i < fm.count; ++i) {
                        fnames.push_back(fm.fields[i].name);
                    }
                }
                std::string fieldsJson = "\"fields\":[";
                for (size_t i = 0; i < fnames.size(); ++i) {
                    fieldsJson += "\"" + jsonEscape(fnames[i]) + "\"";
                    if (i + 1 < fnames.size()) fieldsJson += ",";
                }
                fieldsJson += "]";

                auto semState = semanticState(dev);
                std::ostringstream ss;
                ss << "{\n";
                ss << "  \"ok\": true,\n";
                ss << "  \"code\": 0,\n";
                ss << "  " << fieldsJson << ",\n";
                ss << "  \"state\": " << semState.toJson() << "\n";
                ss << "}";
                std::printf("%s\n", ss.str().c_str());
                std::fflush(stdout);
                continue;
            }
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
