// tests/test_ui_fuzz.cpp — Tier 7 (M8_APP_AUTOMATION_SPEC.md): random-walk UI
// fuzzing. Generates a legal-command .m8script (goto/key/wait sequences from
// a fixed vocabulary -- never malformed syntax, since a parse error would
// just test the parser, not the UI) with a deterministic PRNG (same
// convention as B4.9 "Fuzz the walker" in test_sequencer_walk.cpp), asserts
// assert_no_overlap/assert_no_error after every step, and a finite/
// non-clipped audio tail at the end.
//
// Tagged [fuzz][.] -- Catch2's leading-dot hidden-tag convention excludes it
// from the default (no-filter) run; invoke explicitly with
// `m8_tests.exe "[fuzz]"`. Deliberately NOT part of the per-commit
// discovered [ui] suite (test_ui_scripts.cpp only globs tests/ui/*.m8script,
// and this test generates its scripts into test_out_ui/, not tests/ui/) --
// a longer, nightly-cadence check, per the spec.
//
// Reproducing a failure: the seed is printed via INFO at the start of each
// run (and again in the failure message); override the base seed with the
// M8_FUZZ_SEED environment variable to replay one exact family of runs, and
// the generated script is left on disk (not cleaned up) for inspection.

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char* kClone = "build\\Release\\m8_clone.exe";

// MIXER is deliberately excluded: its volume bars render at sub-cell pixel
// precision, which the cell-granularity overlap check flags as a false
// positive (see nav_all_screens.m8script's comment on the same issue) --
// not a real layout bug, so it shouldn't make the fuzzer cry wolf.
const char* kScreens[] = {
    "SONG", "CHAIN", "PHRASE", "INSTRUMENT", "TABLE", "PROJECT",
    "GROOVE", "INST_MOD", "SCALE", "INST_POOL", "EFFECTS"
};
const char* kButtons[] = { "UP", "DOWN", "LEFT", "RIGHT", "X", "Z", "SHIFT" };
// Only plain directions make sense as the second half of a compound X+<dir>
// edit attempt (X+SHIFT/X+X/X+Z don't correspond to any real edit gesture).
const char* kDirections[] = { "UP", "DOWN", "LEFT", "RIGHT" };

// One random-walk script: `steps` legal actions, each followed by the two
// cheap invariant checks, plus a final render + audio-health check.
std::string generateFuzzScript(std::mt19937& rng, int steps) {
    std::uniform_int_distribution<int> actionPick(0, 3);
    std::uniform_int_distribution<int> screenPick(0, static_cast<int>(std::size(kScreens)) - 1);
    std::uniform_int_distribution<int> buttonPick(0, static_cast<int>(std::size(kButtons)) - 1);
    std::uniform_int_distribution<int> dirPick(0, static_cast<int>(std::size(kDirections)) - 1);
    std::uniform_int_distribution<int> waitPick(1, 3);

    std::ostringstream s;
    s << "# auto-generated fuzz script -- see tests/test_ui_fuzz.cpp\n";
    for (int i = 0; i < steps; ++i) {
        switch (actionPick(rng)) {
        case 0:
            s << "goto " << kScreens[screenPick(rng)] << "\n";
            break;
        case 1:
            s << "key " << kButtons[buttonPick(rng)] << "\n";
            break;
        case 2:
            // Compound edit attempt: harmless no-op/navigation if the
            // current field doesn't accept an edit there.
            s << "key X+" << kDirections[dirPick(rng)] << "\n";
            break;
        case 3:
            s << "wait " << waitPick(rng) << "\n";
            break;
        }
        s << "assert_no_overlap\n";
        s << "assert_no_error\n";
    }
    s << "render 1 fuzz_tail.wav\n";
    s << "assert_wav fuzz_tail.wav nonfinite == 0\n";
    s << "assert_wav fuzz_tail.wav clipped == 0\n";
    return s.str();
}

int runProcess(const std::string& cmd) {
    return std::system(cmd.c_str());
}

} // namespace

TEST_CASE("UI fuzz: random-walk nav+edit invariants", "[fuzz][.]") {
    // Fixed default (like B4.9's rng(42)) so a bare run is still
    // deterministic and diffable across commits; M8_FUZZ_SEED overrides it
    // to replay a specific failure.
    unsigned long seed = 20260718;
    if (const char* envSeed = std::getenv("M8_FUZZ_SEED")) {
        try { seed = std::stoul(envSeed); } catch (...) {}
    }

    constexpr int kRuns = 5;
    constexpr int kStepsPerRun = 60;

    for (int run = 0; run < kRuns; ++run) {
        unsigned long runSeed = seed + static_cast<unsigned long>(run) * 7919;  // prime stride
        DYNAMIC_SECTION("run " << run << " (seed " << runSeed << ")") {
            std::mt19937 rng(runSeed);
            std::string script = generateFuzzScript(rng, kStepsPerRun);

            std::string outDir = "test_out_ui\\fuzz_" + std::to_string(runSeed);
            fs::create_directories(outDir);
            std::string scriptPath = outDir + "\\fuzz.m8script";
            {
                std::ofstream f(scriptPath);
                f << script;
            }

            INFO("seed: " << runSeed << " (reproduce with M8_FUZZ_SEED=" << runSeed << ")");
            INFO("script: " << scriptPath);

            std::string cmd = std::string(kClone) +
                " --script " + scriptPath +
                " --headless --out-dir " + outDir;
            int rc = runProcess(cmd);
            REQUIRE(rc == 0);
        }
    }
}
