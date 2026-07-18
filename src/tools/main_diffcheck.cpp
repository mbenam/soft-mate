// ===========================================================================
// main_diffcheck.cpp — Device-vs-golden diff checker.
//
// Runs a script on the device, dumps the final screen, and compares against
// a golden reference (text grid). Reports the first divergence.
//
//   m8_diffcheck --port COM3 --script test.m8script
//   m8_diffcheck --port COM3 --script test.m8script --golden ref.txt
//   m8_diffcheck --port COM3 --script test.m8script --save out.json --golden ref.txt
//
// Serial only. No engine, no SDL, no audio.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/Gestures.h"
#include "m8/DeviceScriptRunner.h"

using namespace m8::dev;

// Read a text file into a vector of lines.
static std::vector<std::string> readTextFile(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Dump the screen grid as a plain text grid (no colors, no cursor markers).
// This is the canonical form for comparison.
static std::vector<std::string> dumpTextGrid(const ScreenGrid& grid) {
    if (grid.cells.empty()) return {};

    std::set<int> xs, ys;
    for (auto& [pos, c] : grid.cells) { ys.insert(pos.first); xs.insert(pos.second); }
    const int minX = *xs.begin(), minY = *ys.begin();
    // Detect pitch from the cell positions.
    auto detectPitch = [](const std::set<int>& vals, int def) -> int {
        if (vals.size() < 2) return def;
        int prev = *vals.begin();
        int minGap = def;
        for (auto it = std::next(vals.begin()); it != vals.end(); ++it) {
            int gap = *it - prev;
            if (gap > 1 && gap < minGap) minGap = gap;
            prev = *it;
        }
        return minGap;
    };
    const int cw = detectPitch(xs, 8);
    const int ch = detectPitch(ys, 10);
    int maxCol = 0, maxRow = 0;
    for (int x : xs) maxCol = std::max(maxCol, (x - minX) / cw);
    for (int y : ys) maxRow = std::max(maxRow, (y - minY) / ch);

    std::vector<std::string> gridRows(maxRow + 1, std::string(maxCol + 1, ' '));
    for (auto& [pos, c] : grid.cells) {
        int col = (pos.second - minX) / cw;
        int row = (pos.first  - minY) / ch;
        if (row >= 0 && row <= maxRow && col >= 0 && col <= maxCol) {
            char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
            gridRows[row][col] = g;
        }
    }
    return gridRows;
}

// Compare two text grids line-by-line. Returns empty string on match,
// or a description of the first divergence.
static std::string diffGrids(const std::vector<std::string>& a,
                             const std::vector<std::string>& b) {
    int maxRows = std::max(a.size(), b.size());
    for (int r = 0; r < maxRows; ++r) {
        const std::string& la = (r < (int)a.size()) ? a[r] : "";
        const std::string& lb = (r < (int)b.size()) ? b[r] : "";
        int maxCols = std::max(la.size(), lb.size());
        for (int c = 0; c < maxCols; ++c) {
            char ca = (c < (int)la.size()) ? la[c] : ' ';
            char cb = (c < (int)lb.size()) ? lb[c] : ' ';
            if (ca != cb) {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "row %d col %d: expected '%c' (0x%02X), got '%c' (0x%02X)",
                    r, c, cb, (unsigned char)cb, ca, (unsigned char)ca);
                return buf;
            }
        }
    }
    // Also check line count.
    if (a.size() != b.size()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "line count: expected %d, got %d",
                      (int)b.size(), (int)a.size());
        return buf;
    }
    return "";
}

int main(int argc, char** argv) {
    std::string port, scriptPath, goldenPath, savePath;
    int holdMs = 15;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--port")    port = next();
        else if (a == "--script")  scriptPath = next();
        else if (a == "--golden")  goldenPath = next();
        else if (a == "--save")    savePath = next();
        else if (a == "--hold-ms") holdMs = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (port.empty() || scriptPath.empty()) {
        std::fprintf(stderr,
            "usage: m8_diffcheck --port COM3 --script FILE.m8script\n"
            "              [--golden ref.txt] [--save out.json] [--hold-ms 15]\n");
        return 1;
    }

    // Open device.
    M8Device dev;
    if (!dev.open(port.c_str())) return 2;
    std::printf("serial: %s opened\n", port.c_str());

    // Load gestures.
    auto& gestures = m8::dev::getGestures();
    gestures.loadFromFile("hw_buttons.json");

    // Load and run script.
    m8::dev::DeviceScriptRunner runner;
    if (!runner.loadScript(scriptPath)) {
        std::fprintf(stderr, "script parse error (line %d): %s\n",
                     runner.lastErrorLine(), runner.lastError().c_str());
        dev.close();
        return 2;
    }
    std::printf("script: %s (%zu commands)\n", scriptPath.c_str(), runner.loadScript_count());

    int rc = runner.run(dev, holdMs);
    if (rc != 0) {
        std::fprintf(stderr, "script FAILED (line %d): %s\n",
                     runner.lastErrorLine(), runner.lastError().c_str());
        dev.close();
        return rc;
    }
    std::printf("script: PASSED\n");

    // Capture final screen.
    dev.readScreen();
    auto deviceGrid = dumpTextGrid(dev.grid());

    // Print device screen.
    std::printf("\n--- device screen ---\n");
    for (auto& line : deviceGrid) std::printf("%s\n", line.c_str());

    // Save JSON if requested.
    if (!savePath.empty()) {
        dev.grid().printJson(savePath);
        std::printf("saved: %s\n", savePath.c_str());
    }

    // Compare against golden if provided.
    if (!goldenPath.empty()) {
        auto golden = readTextFile(goldenPath);
        std::string diff = diffGrids(deviceGrid, golden);
        if (diff.empty()) {
            std::printf("\n*** MATCH *** (vs %s)\n", goldenPath.c_str());
        } else {
            std::fprintf(stderr, "\n*** DIFF *** (vs %s): %s\n", goldenPath.c_str(), diff.c_str());
            dev.close();
            return 1;
        }
    }

    dev.close();
    return 0;
}
