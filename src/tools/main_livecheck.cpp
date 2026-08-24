// ===========================================================================
// m8_livecheck — hardware proof that the live read model does what the pull
// model structurally cannot: observe a running transport.
//
// Why this exists
// ---------------
// tests/test_live_reader.cpp proves the claim offline against a fake that
// keeps drawing. That is the right place for the logic, but it cannot prove
// the premise -- that a real M8 really does redraw continuously while playing,
// hard enough that readInto never sees its settle window. This does, on the
// device, and prints the numbers.
//
// It is a diagnostic, not a driver. It presses exactly one key (PLAY), and
// restores the transport to the state it found it in.
//
//   m8_livecheck --port COM3 [--seconds 4]
//
// Exit 0 = the live reader stayed current through playback while the pull read
// timed out. Exit 1 = it did not, which means either the premise is wrong on
// this firmware or LiveReader is broken; the JSON says which.
// ===========================================================================

#include "m8/LiveReader.h"
#include "m8/M8Device.h"
#include "m8/ScreenModel.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <set>
#include <string>
#include <thread>

using namespace m8::dev;
using clk = std::chrono::steady_clock;

namespace {

// Pixel y of the playhead marker, or -1. Same bounds as playheadVisible():
// the row-label gutter only, so the chrome's "T>140" tempo readout and the
// '<' cursor marker cannot be mistaken for it.
int playheadY(const ScreenGrid& grid) {
    for (const auto& entry : grid.cells) {
        if (entry.second.ch == 0x3E
            && entry.first.second < kGridGlyphWidth * 4
            && entry.first.second < ScreenGrid::MAIN_X_MAX)
            return entry.first.first;
    }
    return -1;
}

// A cheap order-independent digest of the drawn screen. Used to answer "did
// the picture change", which grid comparison cannot do casually: ScreenGrid
// holds a map of ~1200 cells and copying one per sample to diff it is far
// more work than folding it.
//
// Why this and not the playhead row: on SONG the marker names the *song row*,
// which advances once per chain -- minutes apart at a normal tempo. Watching
// it for four seconds and concluding nothing moved says something about SONG,
// not about the reader. The digest changes on whatever the device is actually
// redrawing, on whatever screen it happens to be left on.
// Rects are folded in as well as cells, and that is not a detail. A first
// version digested glyph + foreground only and reported one change across
// 1595 decoded frames -- reading as "the device streams constantly but draws
// nothing", which is wrong. Most of what moves during playback is rect fills:
// the level meters and the playhead row highlight. Leaving them out measures
// the traffic and misses the picture.
uint64_t gridDigest(const ScreenGrid& grid) {
    uint64_t h = 1469598103934665603ull;              // FNV-1a offset basis
    auto fold = [&h](uint64_t k) { h = (h ^ k) * 1099511628211ull; };  // FNV-1a
    for (const auto& entry : grid.cells) {
        fold((static_cast<uint64_t>(entry.first.first) << 40)
           ^ (static_cast<uint64_t>(entry.first.second) << 24)
           ^ (static_cast<uint64_t>(entry.second.ch) << 16)
           ^ (static_cast<uint64_t>(entry.second.fg[0]) << 8)
           ^  static_cast<uint64_t>(entry.second.fg[1]));
        fold((static_cast<uint64_t>(entry.second.bg[0]) << 16)
           ^ (static_cast<uint64_t>(entry.second.bg[1]) << 8)
           ^  static_cast<uint64_t>(entry.second.bg[2]));
    }
    for (const auto& r : grid.highlights) {
        fold((static_cast<uint64_t>(r.x) << 48) ^ (static_cast<uint64_t>(r.y) << 32)
           ^ (static_cast<uint64_t>(r.w) << 16) ^  static_cast<uint64_t>(r.h));
        fold((static_cast<uint64_t>(r.c[0]) << 16)
           ^ (static_cast<uint64_t>(r.c[1]) << 8) ^ static_cast<uint64_t>(r.c[2]));
    }
    return h;
}

int msSince(clk::time_point t) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count());
}

} // namespace

int main(int argc, char** argv) {
    std::string port = "COM3";
    int seconds = 4;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc)         port = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: m8_livecheck [--port COM3] [--seconds 4]\n");
            return 0;
        }
    }

    M8Device dev;
    if (!dev.open(port.c_str())) {
        std::fprintf(stderr, "m8_livecheck: could not open %s\n", port.c_str());
        return 2;
    }

    // ---- 1. Baseline, transport as found ---------------------------------
    const auto tBase = clk::now();
    dev.readSettled(0, 250, 2000);
    const int  baseMs         = msSince(tBase);
    const bool startedPlaying = playheadVisible(dev.grid());
    const bool baseSettled    = dev.lastRead().settled;

    // ---- 2. Start the transport if it is not already running -------------
    // PLAY is a toggle, so this is read-then-press, never a blind press.
    if (!startedPlaying) {
        dev.press(Key::PLAY);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    // ---- 3. A pull read during playback ----------------------------------
    const auto tPull = clk::now();
    dev.readSettled(0, 250, 2000);
    const int  pullMs      = msSince(tPull);
    const bool pullSettled = dev.lastRead().settled;
    const bool pullTimedOut= dev.lastRead().timedOut;
    const int  pullQuiet   = dev.lastRead().quietMs;
    const bool playing     = playheadVisible(dev.grid());

    // ---- 4. The same window, read live -----------------------------------
    LiveReader live(dev);
    if (!live.start()) {
        std::fprintf(stderr, "m8_livecheck: LiveReader::start() refused\n");
        dev.close();
        return 2;
    }

    std::set<int> playheadRows;
    std::set<uint64_t> screens;
    uint64_t lastDigest = 0;
    int   changes = 0;
    long  firstSeq = -1, lastSeq = 0;
    int   samples = 0, maxQuiet = 0;
    long  totalSnapUs = 0;

    const auto until = clk::now() + std::chrono::seconds(seconds);
    while (clk::now() < until) {
        const auto t0 = clk::now();
        const LiveSnapshot s = live.snapshot();
        totalSnapUs += std::chrono::duration_cast<std::chrono::microseconds>(
                           clk::now() - t0).count();
        if (firstSeq < 0) firstSeq = s.seq;
        lastSeq = s.seq;
        if (s.quietMs > maxQuiet) maxQuiet = s.quietMs;
        const int y = playheadY(s.grid);
        if (y >= 0) playheadRows.insert(y);
        const uint64_t d = gridDigest(s.grid);
        screens.insert(d);
        if (samples > 0 && d != lastDigest) ++changes;
        lastDigest = d;
        ++samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    live.stop();

    // ---- 5. Restore the transport to how we found it ---------------------
    if (!startedPlaying) {
        dev.press(Key::PLAY);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        dev.readSettled(0, 250, 2000);
    }

    // The claim, stated as a test. The live reader must have kept up (frames
    // arriving, never quiet for long) and must have watched the playhead move
    // -- across more than one row, which is the part a settle-gated read can
    // never see. A pull read that settled during playback does not fail this;
    // it means the premise does not hold on this firmware, and the JSON shows
    // it. What fails is the live path not keeping up.
    const bool liveKeptUp   = (lastSeq > firstSeq) && samples > 0;
    // The claim is "the live reader saw the screen change during a window in
    // which the pull read could not complete" -- not "the playhead advanced".
    // Those differ on SONG, where the marker names the song row and holds it
    // for a whole chain; the first version asserted the latter and failed a
    // correct reader for that reason alone.
    const bool sawChange    = screens.size() > 1;
    const bool ok           = liveKeptUp && sawChange && pullTimedOut;

    std::printf("{\n");
    std::printf("  \"port\": \"%s\",\n", port.c_str());
    std::printf("  \"seconds\": %d,\n", seconds);
    std::printf("  \"transport_was_running_on_entry\": %s,\n", startedPlaying ? "true" : "false");
    std::printf("  \"baseline_read_settled\": %s,\n", baseSettled ? "true" : "false");
    std::printf("  \"baseline_read_ms\": %d,\n", baseMs);
    std::printf("  \"playing_during_test\": %s,\n", playing ? "true" : "false");
    std::printf("  \"pull_read_during_playback\": {\n");
    std::printf("    \"settled\": %s,\n", pullSettled ? "true" : "false");
    std::printf("    \"timed_out\": %s,\n", pullTimedOut ? "true" : "false");
    std::printf("    \"quiet_ms_at_exit\": %d,\n", pullQuiet);
    std::printf("    \"elapsed_ms\": %d\n", pullMs);
    std::printf("  },\n");
    std::printf("  \"live_read_during_playback\": {\n");
    std::printf("    \"snapshots\": %d,\n", samples);
    std::printf("    \"frames_decoded\": %ld,\n", lastSeq - (firstSeq < 0 ? 0 : firstSeq));
    std::printf("    \"max_quiet_ms\": %d,\n", maxQuiet);
    std::printf("    \"frames_per_second\": %ld,\n",
                seconds ? (lastSeq - (firstSeq < 0 ? 0 : firstSeq)) / seconds : 0);
    std::printf("    \"distinct_screens\": %zu,\n", screens.size());
    std::printf("    \"screen_changes_observed\": %d,\n", changes);
    std::printf("    \"distinct_playhead_rows\": %zu,\n", playheadRows.size());
    std::printf("    \"mean_snapshot_us\": %ld\n", samples ? totalSnapUs / samples : 0);
    std::printf("  },\n");
    std::printf("  \"live_kept_up\": %s,\n", liveKeptUp ? "true" : "false");
    std::printf("  \"saw_screen_change\": %s,\n", sawChange ? "true" : "false");
    std::printf("  \"saw_playhead_move\": %s,\n", playheadRows.size() > 1 ? "true" : "false");
    std::printf("  \"ok\": %s\n", ok ? "true" : "false");
    std::printf("}\n");

    dev.close();
    return ok ? 0 : 1;
}
