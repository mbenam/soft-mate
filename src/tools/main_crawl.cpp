// ===========================================================================
// m8_crawl — walk a screen's cursor chain exhaustively and write down what is
// really there.
//
// Why this exists
// ---------------
// The field maps in ScreenModel.h are hand-typed coordinates that nothing ever
// checks against a device, and M8_DRIVER_BUGS.md is full of the consequences:
// #9 (every EFFECTS row off by one), #11 (SCALE LOAD/SAVE columns wrong), #17
// (a PROJECT field that does not exist on this firmware), #21 (TYPE's row),
// #31 (a cursor on LOAD reported as TYPE). Each was found by hand, one at a
// time, usually after it had already caused a wrong edit.
//
// #20 is the same fault and the clearest case. Its entry blames "hidden state"
// and calls the widget unfixable, but a sweep on 2026-08-24 showed the chain is
// perfectly deterministic and that kMixerFields simply points MST_CHO/DEL/REV
// at column 72 -- which the cursor never visits. The coordinates named the
// "MX DE RE" column header instead of the send-return values a row above it.
// The fields were not unreachable; the driver was aiming at nothing.
//
// So stop typing coordinates and go and look. From a homed cursor this presses
// every direction from every stop it discovers, until the set closes, and
// writes out:
//
//   - every real cursor stop on the screen
//   - the navigation graph: which key moves from which stop to which
//
// The first gives ground truth to diff the field maps against (--check). The
// second is what turns moveCursorTo from a pile of heuristics -- axis fallback,
// "greatest col <= gridCol", kMaxFieldSpan, all of which have their own bug
// numbers -- into a path search over measured edges.
//
//   m8_crawl --port COM3 --screen MIXER --out mixer.json
//   m8_crawl --check mixer.json                     (offline, no device)
//
// Exit 0 = crawled, or checked clean. 1 = --check found a disagreement.
// 2 = setup failed.
// ===========================================================================

#include "m8/CrawlCheck.h"
#include "m8/M8Device.h"
#include "m8/Primitives.h"
#include "m8/ScreenModel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace m8::dev;

namespace {

struct Stop {
    int row = -1;      // pixel row of the cursor
    int col = -1;      // pixel col
    std::string text;  // accent text seen there, for the human reading the file
    bool operator<(const Stop& o) const {
        return row != o.row ? row < o.row : col < o.col;
    }
};

struct Edge {
    Stop from, to;
    std::string key;
};

const char* kDirNames[4] = { "UP", "DOWN", "LEFT", "RIGHT" };
const uint8_t kDirMasks[4] = { Key::UP, Key::DOWN, Key::LEFT, Key::RIGHT };

Stop readStop(M8Device& dev) {
    Stop s;
    auto cf = dev.cursorField();
    if (cf) { s.row = cf->row; s.col = cf->col; }
    s.text = cursorValueText(dev.grid());
    return s;
}

// Drive the cursor to a known stop by replaying the shortest recorded path from
// home. Re-homing every time is slow but it is the only way to be certain where
// we are: the M8 remembers a per-screen cursor position, so "press UP a lot"
// from an unknown place is not a reset (M8_DRIVER_BUGS.md #20).
void goHome(M8Device& dev, Screen screen, int holdMs) {
    gotoScreen(dev, screen, holdMs);
    panicHome(dev, holdMs);
    gotoScreen(dev, screen, holdMs);
    dev.readSettled(0, 200, 1500);
}

bool replay(M8Device& dev, Screen screen, const std::vector<std::string>& path, int holdMs) {
    goHome(dev, screen, holdMs);
    for (const std::string& k : path) {
        for (int d = 0; d < 4; ++d) {
            if (k == kDirNames[d]) { dev.press(kDirMasks[d], holdMs); break; }
        }
        dev.readSettled(0, 120, 900);
    }
    return true;
}

std::string escJson(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

// ---- offline check --------------------------------------------------------

struct Loaded {
    std::string screen;
    std::set<Stop> stops;
};

bool loadCrawl(const std::string& path, Loaded& out) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    size_t p = all.find("\"screen\"");
    if (p != std::string::npos) {
        size_t a = all.find('"', all.find(':', p)) + 1;
        size_t b = all.find('"', a);
        out.screen = all.substr(a, b - a);
    }
    // Stops are written one per line as {"row":N,"col":N,...}; parse positionally
    // rather than pulling in a JSON library for a file this tool wrote itself.
    size_t q = all.find("\"stops\"");
    while (q != std::string::npos) {
        q = all.find("{\"row\":", q);
        if (q == std::string::npos) break;
        Stop s;
        s.row = std::atoi(all.c_str() + q + 7);
        size_t c = all.find("\"col\":", q);
        if (c == std::string::npos) break;
        s.col = std::atoi(all.c_str() + c + 6);
        out.stops.insert(s);
        q = all.find('}', c);
    }
    return !out.stops.empty();
}

int runCheck(const std::string& path) {
    Loaded L;
    if (!loadCrawl(path, L)) return 2;

    std::set<CrawlStop> stops;
    for (const Stop& s : L.stops) stops.insert({ s.row / 10 - 3, s.col / 8 });

    // The comparison itself lives in m8/CrawlCheck.h so tests can run it
    // offline against a committed artifact -- see that header for why.
    const Screen screen = identifyScreen(toUpper(L.screen));
    const CrawlCheckResult r = checkCrawl(screen, stops);

    if (!r.checkable) {
        std::printf("{\n  \"screen\": \"%s\",\n  \"checked\": false,\n"
                    "  \"reason\": \"grid screen or no field map\"\n}\n", L.screen.c_str());
        return 0;
    }

    std::printf("{\n  \"screen\": \"%s\",\n  \"stops\": %zu,\n  \"mapped_fields\": %zu,\n",
                L.screen.c_str(), stops.size(), r.mappedFields);
    std::printf("  \"phantom_fields\": [");
    for (size_t i = 0; i < r.phantomFields.size(); ++i)
        std::printf("%s\"%s\"", i ? ", " : "", r.phantomFields[i].c_str());
    std::printf("],\n  \"unclaimed_stops\": [");
    for (size_t i = 0; i < r.unclaimedStops.size(); ++i)
        std::printf("%s\"row %d col %d\"", i ? ", " : "",
                    r.unclaimedStops[i].gridRow, r.unclaimedStops[i].gridCol);
    std::printf("],\n  \"ok\": %s\n}\n", r.ok() ? "true" : "false");
    return r.ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string port = "COM3", screenName = "MIXER", out = "crawl.json", checkPath;
    int holdMs = 20, maxStops = 200;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (!std::strcmp(a, "--port")   && i + 1 < argc) port = argv[++i];
        else if (!std::strcmp(a, "--screen") && i + 1 < argc) screenName = argv[++i];
        else if (!std::strcmp(a, "--out")    && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(a, "--check")  && i + 1 < argc) checkPath = argv[++i];
        else if (!std::strcmp(a, "--hold-ms")&& i + 1 < argc) holdMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--max-stops") && i + 1 < argc) maxStops = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--help")) {
            std::printf("usage: m8_crawl --port COM3 --screen MIXER --out crawl.json\n"
                        "       m8_crawl --check crawl.json          (offline)\n"
                        "       [--hold-ms 20] [--max-stops 200]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown argument: %s\n", a); return 2; }
    }

    if (!checkPath.empty()) return runCheck(checkPath);

    const Screen screen = identifyScreen(toUpper(screenName));
    if (screen == Screen::UNKNOWN) {
        std::fprintf(stderr, "unknown --screen %s\n", screenName.c_str());
        return 2;
    }

    M8Device dev;
    if (!dev.open(port.c_str())) {
        std::fprintf(stderr, "could not open %s\n", port.c_str());
        return 2;
    }

    // Breadth-first over the chain. Each node is reached by replaying its path
    // from home rather than by walking back, because the M8's remembered cursor
    // position makes "undo the last press" unreliable.
    std::map<Stop, std::vector<std::string>> pathTo;
    std::vector<Edge> edges;
    std::deque<Stop> queue;

    goHome(dev, screen, holdMs);
    const Stop start = readStop(dev);
    if (start.row < 0) {
        std::fprintf(stderr, "no cursor on %s after homing\n", screenName.c_str());
        dev.close();
        return 2;
    }
    pathTo[start] = {};
    queue.push_back(start);

    int visited = 0;
    while (!queue.empty() && static_cast<int>(pathTo.size()) < maxStops) {
        const Stop cur = queue.front();
        queue.pop_front();
        ++visited;

        for (int d = 0; d < 4; ++d) {
            std::vector<std::string> path = pathTo[cur];
            replay(dev, screen, path, holdMs);
            const Stop before = readStop(dev);
            if (!(before < cur) && !(cur < before)) {
                // Landed where we expected. Press and see.
            } else {
                // Replay drifted -- record and skip rather than pollute the graph.
                std::fprintf(stderr, "replay drift at row %d col %d (wanted %d/%d)\n",
                             before.row, before.col, cur.row, cur.col);
                continue;
            }

            dev.press(kDirMasks[d], holdMs);
            dev.readSettled(0, 150, 1000);
            const Stop next = readStop(dev);
            if (next.row < 0) continue;
            if (!(next < cur) && !(cur < next)) continue;   // no movement

            edges.push_back({ cur, next, kDirNames[d] });
            if (!pathTo.count(next)) {
                std::vector<std::string> np = pathTo[cur];
                np.push_back(kDirNames[d]);
                pathTo[next] = np;
                queue.push_back(next);
            }
        }
        std::fprintf(stderr, "  visited %d, known %zu, queued %zu\n",
                     visited, pathTo.size(), queue.size());
    }

    std::ofstream o(out);
    o << "{\n  \"screen\": \"" << escJson(screenName) << "\",\n";
    o << "  \"port\": \"" << escJson(port) << "\",\n";
    o << "  \"stops\": [\n";
    bool first = true;
    for (const auto& kv : pathTo) {
        if (!first) o << ",\n";
        first = false;
        o << "    {\"row\":" << kv.first.row << ",\"col\":" << kv.first.col
          << ",\"grid_row\":" << (kv.first.row / 10 - 3)
          << ",\"grid_col\":" << (kv.first.col / 8)
          << ",\"text\":\"" << escJson(kv.first.text) << "\",\"path\":[";
        for (size_t i = 0; i < kv.second.size(); ++i)
            o << (i ? "," : "") << "\"" << kv.second[i] << "\"";
        o << "]}";
    }
    o << "\n  ],\n  \"edges\": [\n";
    for (size_t i = 0; i < edges.size(); ++i) {
        o << (i ? ",\n" : "") << "    {\"from\":[" << edges[i].from.row << "," << edges[i].from.col
          << "],\"key\":\"" << edges[i].key << "\",\"to\":[" << edges[i].to.row << ","
          << edges[i].to.col << "]}";
    }
    o << "\n  ]\n}\n";
    o.close();

    std::printf("crawled %s: %zu stops, %zu edges -> %s\n",
                screenName.c_str(), pathTo.size(), edges.size(), out.c_str());
    dev.close();
    return 0;
}
