// Discovered UI-script runner (M8_APP_AUTOMATION_SPEC.md Tier 1).
//
// Every tests/ui/*.m8script is found and run automatically via
// `m8_clone --script <file> --headless --out-dir test_out_ui/<name>`, gating
// the whole suite in CI instead of the single hand-picked script this file
// used to run (originally just groovetest.m8script, a regression test for the
// GROOVE screen crash — HexU8(0) null-pointer UB when a groove step == 0xFF).
//
// tests/ui/manifest.txt gives a script extra verification (e.g. render then
// m8_analyze) and fixes execution order for the couple of scripts with a
// cross-script file dependency (closed_loop_glitch -> closed_loop_fix). A
// script not listed there defaults to policy "pass": exit 0, nothing more.
//
// Each DYNAMIC_SECTION is one script, so a failure names the exact file.

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Windows/MSVC only (matches the rest of this project's tooling). Hardcoded
// to the Release build dir, same convention this file already used -- if you
// run under build_asan, point these elsewhere or extend this later.
constexpr const char* kClone   = "build\\Release\\m8_clone.exe";
constexpr const char* kAnalyze = "build\\Release\\m8_analyze.exe";
constexpr const char* kRender  = "build\\Release\\m8_render.exe";

struct ScriptPolicy {
    std::string kind = "pass";        // pass | skip | analyze_pass | analyze_fail | diff
    std::string skipReason;
    std::string arg1, arg2, arg3;     // policy-specific (see manifest.txt header)
};

ScriptPolicy parsePolicy(const std::string& spec) {
    ScriptPolicy p;
    auto colon = spec.find(':');
    p.kind = (colon == std::string::npos) ? spec : spec.substr(0, colon);
    std::string rest = (colon == std::string::npos) ? "" : spec.substr(colon + 1);

    if (p.kind == "skip") {
        p.skipReason = rest;
    } else if (p.kind == "analyze_pass" || p.kind == "analyze_fail") {
        p.arg1 = rest;  // wav filename, relative to the script's out-dir
    } else if (p.kind == "diff") {
        auto c1 = rest.find(':');
        auto c2 = (c1 == std::string::npos) ? std::string::npos : rest.find(':', c1 + 1);
        if (c1 != std::string::npos && c2 != std::string::npos) {
            p.arg1 = rest.substr(0, c1);              // app-rendered wav (from the script)
            p.arg2 = rest.substr(c1 + 1, c2 - c1 - 1); // song .m8s to render via m8_render
            p.arg3 = rest.substr(c2 + 1);              // seconds
        }
    }
    return p;
}

// Manifest-declared scripts, in file order (comments '#' and blank lines skipped).
std::vector<std::pair<std::string, ScriptPolicy>> loadManifest(const std::string& path) {
    std::vector<std::pair<std::string, ScriptPolicy>> ordered;
    std::ifstream f(path);
    if (!f.is_open()) return ordered;

    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        std::string name, policySpec;
        if (!(iss >> name >> policySpec)) continue;
        ordered.push_back({name, parsePolicy(policySpec)});
    }
    return ordered;
}

// All tests/ui/*.m8script, by stem, sorted alphabetically.
std::vector<std::string> discoverScripts(const std::string& dir) {
    std::vector<std::string> names;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".m8script")
            names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Manifest-listed scripts first (in file order, so declared dependencies like
// closed_loop_glitch -> closed_loop_fix run in the right order), then any
// scripts the manifest doesn't mention, alphabetically, defaulting to "pass".
std::vector<std::pair<std::string, ScriptPolicy>> buildRunOrder() {
    auto order = loadManifest("tests/ui/manifest.txt");

    std::vector<std::string> known;
    known.reserve(order.size());
    for (auto& [name, policy] : order) known.push_back(name);

    for (auto& name : discoverScripts("tests/ui")) {
        if (std::find(known.begin(), known.end(), name) == known.end())
            order.push_back({name, ScriptPolicy{}});
    }
    return order;
}

int runProcess(const std::string& cmd) {
    return std::system(cmd.c_str());
}

} // namespace

TEST_CASE("UI scripts", "[ui]") {
    auto order = buildRunOrder();
    // A broken discovery path (wrong CWD, missing tests/ui) must fail loudly,
    // not silently run zero sections and report green.
    REQUIRE(!order.empty());

    for (auto& [name, policy] : order) {
        DYNAMIC_SECTION(name) {
            if (policy.kind == "skip") {
                WARN("skipped '" << name << "': " << policy.skipReason);
                continue;
            }

            const std::string outDir = "test_out_ui\\" + name;
            const std::string runCmd = std::string(kClone) +
                " --script tests\\ui\\" + name + ".m8script" +
                " --headless --out-dir " + outDir;
            INFO("script: " << runCmd);
            REQUIRE(runProcess(runCmd) == 0);

            if (policy.kind == "analyze_pass" || policy.kind == "analyze_fail") {
                const std::string wavPath = outDir + "\\" + policy.arg1;
                const std::string analyzeCmd = std::string(kAnalyze) + " " + wavPath;
                INFO("analyze: " << analyzeCmd);
                int rc = runProcess(analyzeCmd);
                if (policy.kind == "analyze_pass")
                    REQUIRE(rc == 0);
                else
                    REQUIRE(rc == 1);  // deliberate hard-check FAIL, not a tool error (2)
            } else if (policy.kind == "diff") {
                const std::string appWav = outDir + "\\" + policy.arg1;
                const std::string refOut = outDir + "\\m8render_ref";
                const std::string renderCmd = std::string(kRender) +
                    " --load " + policy.arg2 +
                    " --song --seconds " + policy.arg3 +
                    " --out " + refOut;
                INFO("render: " << renderCmd);
                REQUIRE(runProcess(renderCmd) == 0);

                const std::string diffCmd = std::string(kAnalyze) +
                    " --diff " + appWav + " " + refOut + ".wav";
                INFO("diff: " << diffCmd);
                REQUIRE(runProcess(diffCmd) == 0);
            }
        }
    }
}
