// ===========================================================================
// main_diffcheck.cpp — Device-vs-golden diff checker.
//
// Mode 1 (original): run a script on the device and compare the screen text
// grid against a golden reference.
//
//   m8_diffcheck --port COM3 --script test.m8script
//   m8_diffcheck --port COM3 --script test.m8script --golden ref.txt
//   m8_diffcheck --port COM3 --script test.m8script --save out.json --golden ref.txt
//
// Mode 2 (C5): compare two UiCapture JSON files (device vs clone).
//
//   m8_diffcheck --diff-capture device.json clone.json [--max-diffs N]
//
// Serial only. No engine, no SDL, no audio.
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <algorithm>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/Gestures.h"
#include "m8/DeviceScriptRunner.h"
#include "m8/UiCapture.h"

using namespace m8::dev;

// ---------------------------------------------------------------------------
// UiCapture diff (C5)
// ---------------------------------------------------------------------------

// Resolve a style id to its RGB triple from the palette.
static std::array<uint8_t, 3> resolveRGB(const UiCapture& c, int styleId) {
    if (styleId >= 0 && styleId < (int)c.palette.size())
        return c.palette[styleId];
    return {0, 0, 0};
}

// Build a canonical label for each distinct RGB in a capture: collect all
// resolved RGBs from cells, sort them canonically, and assign label 0, 1, 2,
// ... in that sorted order. This makes the labels independent of palette order
// and first-appearance order — two captures that use the same set of colours
// (regardless of how their palettes or cell traversals are ordered) get the
// same canonical labels.
static std::map<std::array<uint8_t,3>, int> buildCanonicalLabels(const UiCapture& c) {
    // Collect all distinct RGBs that appear in any cell's fg or bg.
    std::set<std::array<uint8_t,3>> rgbSet;
    for (const auto& cell : c.cells) {
        rgbSet.insert(resolveRGB(c, cell.fgStyle));
        rgbSet.insert(resolveRGB(c, cell.bgStyle));
    }

    // Sort canonically (r, g, b) and assign labels in that order.
    std::map<std::array<uint8_t,3>, int> rgbToLabel;
    int label = 0;
    for (const auto& rgb : rgbSet) {
        rgbToLabel[rgb] = label++;
    }
    return rgbToLabel;
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Returns 0 on match, 1 on diff, 2 on refusal.
static int diffCaptures(const std::string& pathA, const std::string& pathB, int maxDiffs) {
    // Load both captures.
    UiCapture a, b;
    std::string errA, errB;
    std::string textA = readFile(pathA);
    std::string textB = readFile(pathB);
    if (textA.empty()) { std::fprintf(stderr, "cannot read: %s\n", pathA.c_str()); return 2; }
    if (textB.empty()) { std::fprintf(stderr, "cannot read: %s\n", pathB.c_str()); return 2; }
    if (!fromJson(textA, a, errA)) { std::fprintf(stderr, "parse error in %s: %s\n", pathA.c_str(), errA.c_str()); return 2; }
    if (!fromJson(textB, b, errB)) { std::fprintf(stderr, "parse error in %s: %s\n", pathB.c_str(), errB.c_str()); return 2; }

    // Refuse to compare when guard conditions are not met.
    if (!a.settled) { std::fprintf(stderr, "REFUSE: %s has settled=false\n", pathA.c_str()); return 2; }
    if (!b.settled) { std::fprintf(stderr, "REFUSE: %s has settled=false\n", pathB.c_str()); return 2; }
    if (a.fontMode != b.fontMode) {
        std::fprintf(stderr, "REFUSE: font_mode mismatch: %s=%d vs %s=%d\n",
                     pathA.c_str(), a.fontMode, pathB.c_str(), b.fontMode);
        return 2;
    }
    if (!a.themeId.empty() && !b.themeId.empty() && a.themeId != b.themeId) {
        std::fprintf(stderr, "REFUSE: theme_id mismatch: %s=\"%s\" vs %s=\"%s\"\n",
                     pathA.c_str(), a.themeId.c_str(), pathB.c_str(), b.themeId.c_str());
        return 2;
    }
    if (a.screen != b.screen) {
        std::fprintf(stderr, "WARNING: screen name mismatch: \"%s\" vs \"%s\" — comparing anyway\n",
                     a.screen.c_str(), b.screen.c_str());
    }

    // Build (col,row) -> UiCell maps.
    auto cellKey = [](const UiCell& c) { return (c.row << 16) | c.col; };
    std::map<int, UiCell> mapA, mapB;
    for (const auto& c : a.cells) mapA[cellKey(c)] = c;
    for (const auto& c : b.cells) mapB[cellKey(c)] = c;

    std::printf("comparing: %s  (%zu cells)  vs  %s  (%zu cells)\n",
                pathA.c_str(), a.cells.size(), pathB.c_str(), b.cells.size());

    // Build canonical label maps (first-appearance of resolved RGB in row-major scan).
    auto labelsA = buildCanonicalLabels(a);
    auto labelsB = buildCanonicalLabels(b);

    int diffs = 0;
    // Collect all keys.
    std::set<int> allKeys;
    for (auto& [k, _] : mapA) allKeys.insert(k);
    for (auto& [k, _] : mapB) allKeys.insert(k);

    for (int k : allKeys) {
        if (diffs >= maxDiffs) { std::printf("  ... (stopped after %d diffs)\n", maxDiffs); break; }
        bool inA = mapA.count(k) > 0;
        bool inB = mapB.count(k) > 0;
        if (!inA) {
            const auto& c = mapB[k];
            std::printf("  [%d,%d] only in B: ch='%c'\n", c.col, c.row, c.ch);
            ++diffs;
        } else if (!inB) {
            const auto& c = mapA[k];
            std::printf("  [%d,%d] only in A: ch='%c'\n", c.col, c.row, c.ch);
            ++diffs;
        } else {
            const auto& ca = mapA[k];
            const auto& cb = mapB[k];
            if (ca.ch != cb.ch) {
                std::printf("  [%d,%d] ch: A='%c' B='%c'\n", ca.col, ca.row, ca.ch, cb.ch);
                ++diffs;
            } else {
                // Compare colour partition: resolve to RGB, then canonical label.
                auto fgA = resolveRGB(a, ca.fgStyle);
                auto bgA = resolveRGB(a, ca.bgStyle);
                auto fgB = resolveRGB(b, cb.fgStyle);
                auto bgB = resolveRGB(b, cb.bgStyle);
                int canFgA = labelsA[fgA];
                int canBgA = labelsA[bgA];
                int canFgB = labelsB[fgB];
                int canBgB = labelsB[bgB];
                if (canFgA != canFgB || canBgA != canBgB) {
                    std::printf("  [%d,%d] style: A[fg=%d rgb=(%d,%d,%d),bg=%d rgb=(%d,%d,%d)]  B[fg=%d rgb=(%d,%d,%d),bg=%d rgb=(%d,%d,%d)]\n",
                                ca.col, ca.row,
                                ca.fgStyle, fgA[0], fgA[1], fgA[2],
                                ca.bgStyle, bgA[0], bgA[1], bgA[2],
                                cb.fgStyle, fgB[0], fgB[1], fgB[2],
                                cb.bgStyle, bgB[0], bgB[1], bgB[2]);
                    ++diffs;
                }
            }
        }
    }

    if (diffs == 0) {
        std::printf("MATCH: no glyph differences in %zu cells\n", allKeys.size());
        return 0;
    }
    std::fprintf(stderr, "DIFF: %d difference(s) found\n", diffs);
    return 1;
}


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
    std::string diffCaptureA, diffCaptureB;
    int holdMs = 15;
    int maxDiffs = 20;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--port")          port = next();
        else if (a == "--script")        scriptPath = next();
        else if (a == "--golden")        goldenPath = next();
        else if (a == "--save")          savePath = next();
        else if (a == "--hold-ms")       holdMs = std::atoi(next().c_str());
        else if (a == "--diff-capture") { diffCaptureA = next(); diffCaptureB = next(); }
        else if (a == "--max-diffs")    maxDiffs = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    // Mode 2: compare two UiCapture JSON files — no device needed.
    if (!diffCaptureA.empty()) {
        return diffCaptures(diffCaptureA, diffCaptureB, maxDiffs);
    }

    if (port.empty() || scriptPath.empty()) {
        std::fprintf(stderr,
            "usage:\n"
            "  m8_diffcheck --port COM3 --script FILE.m8script\n"
            "               [--golden ref.txt] [--save out.json] [--hold-ms 15]\n"
            "\n"
            "  m8_diffcheck --diff-capture A.json B.json [--max-diffs N]\n");
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
