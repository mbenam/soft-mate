// ===========================================================================
// test_field_guard.cpp — the four ways a capture guard lies.
//
// Why this exists
// ---------------
// m8_watchcapture's whole value is that it refuses a capture whose conditions
// moved underneath it (M8_DRIVER_BUGS.md #34). A guard that never fires and a
// guard that cannot fire look identical from the outside, and its first live
// hardware run passed — which proves the plumbing and says nothing about the
// decision. These pin the decision:
//
//   1. drift is caught                     (the #34 case; too loose fails here)
//   2. accent-driven spacing is not drift   (too strict, and observed for real)
//   3. the playhead marker is not drift     (too strict, and constant during playback)
//   4. a mid-repaint empty row is not drift (too strict, and random)
//
// Cases 2-4 all fail *closed* if written naively — they abort good captures —
// which is worse than useless, because the operator learns to pass whatever
// flag turns the guard off.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "m8/FieldGuard.h"

using namespace m8::dev;

TEST_CASE("the guard catches the drift it exists for", "[hwdecode][guard]") {
    // #34, exactly: an AMP sweep point taken after LIM silently moved from 04
    // to 08. The number looked like a real measurement and had to be thrown
    // away only because a later read happened to notice.
    FieldWatch w{"LIM", canonRow(" LIM     04  "), "", false};
    const std::string moved = canonRow(" LIM     08  ");

    CHECK(rowDrifted(w.baseline, moved));
    CHECK(w.baseline == "LIM04");
    CHECK(moved == "LIM08");
}

TEST_CASE("accent-driven spacing is not drift", "[hwdecode][guard]") {
    // Which cells carry the theme accent varies between frames, so the same
    // untouched field arrives with different whitespace. Observed on MIXER:
    // " OUTPUT VOL  F0" one read, "OUTPUTVOLF0" the next. A guard that
    // respects spacing rejects a screen that never moved.
    const std::string a = canonRow(" OUTPUT VOL  F0");
    const std::string b = canonRow("OUTPUTVOLF0");
    CHECK(a == b);
    CHECK_FALSE(rowDrifted(a, b));
}

TEST_CASE("the playhead marker is not drift", "[hwdecode][guard]") {
    // The M8 marks the playing step with '>' in the row-label gutter. During
    // a capture the transport runs through every guarded row in turn, so a
    // guard that saw this as a change would abort essentially every capture
    // it was asked to make.
    const std::string idle    = canonRow(" 04 -- 20 20 1E");
    const std::string playing = canonRow(" 04>-- 20 20 1E");
    CHECK(idle == playing);
    CHECK_FALSE(rowDrifted(idle, playing));

    // But a real change on that same row still lands.
    CHECK(rowDrifted(idle, canonRow(" 04>-- 20 20 1F")));
}

TEST_CASE("a row caught mid-repaint is not drift", "[hwdecode][guard]") {
    // The live grid is sampled without waiting for quiet -- that is the point
    // of it -- so a row can be read between the erase and the redraw. Treating
    // an empty read as a changed field aborts captures at random.
    const std::string base = canonRow(" AMP     00");
    CHECK_FALSE(rowDrifted(base, ""));
    CHECK(rowDrifted(base, canonRow(" AMP     FF")));
}

TEST_CASE("sampleWatch is sticky and reports the first disagreement",
          "[hwdecode][guard]") {
    // A grid whose AMP row changes under it, built by hand rather than through
    // ScreenGrid so the test stays about the decision, not the decode.
    auto gridWith = [](const std::string& text) {
        ScreenGrid g;
        for (size_t i = 0; i < text.size(); ++i) {
            Cell c;
            c.ch = static_cast<uint8_t>(text[i]);
            c.fg[0] = 200; c.fg[1] = 200; c.fg[2] = 200;
            g.cells[{30, static_cast<int>(i) * 8}] = c;   // pixel y 30, pitch 8
        }
        return g;
    };

    FieldWatch w{"AMP", canonRow("AMP 00"), "", false};

    CHECK_FALSE(sampleWatch(w, gridWith("AMP 00")));
    CHECK_FALSE(w.drifted);

    CHECK(sampleWatch(w, gridWith("AMP FF")));
    CHECK(w.drifted);
    CHECK(w.sawInstead == "AMPFF");

    // A later, different disagreement must not overwrite the first one --
    // what the capture was invalidated by is the first thing that moved.
    sampleWatch(w, gridWith("AMP 7F"));
    CHECK(w.sawInstead == "AMPFF");
}
