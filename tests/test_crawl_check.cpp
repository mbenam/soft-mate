// ===========================================================================
// test_crawl_check.cpp — the field-map gate, run offline.
//
// m8_crawl walks a screen's cursor chain on real hardware and records every
// stop it actually has. This checks a recorded crawl against the field maps,
// with no device attached, so the comparison is exercised in CI rather than
// only on the day someone runs the crawler.
//
// The gate itself has to be tested for the same reason the #34 guard did: it
// passed on its first run, which proved the plumbing and said nothing about
// the decision. Both failure directions are pinned below, plus the real
// MIXER artifact, which is currently RED on purpose.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <string>

#include "m8/CrawlCheck.h"

using namespace m8::dev;

namespace {

// Read the grid coordinates out of a crawl artifact. Positional parsing on a
// file this project wrote itself, rather than pulling in a JSON library.
std::set<CrawlStop> loadStops(const std::string& path, bool& ok) {
    std::set<CrawlStop> stops;
    std::ifstream f(path);
    ok = static_cast<bool>(f);
    if (!ok) return stops;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t q = all.find("\"stops\"");
    while (q != std::string::npos) {
        q = all.find("\"grid_row\":", q);
        if (q == std::string::npos) break;
        CrawlStop s;
        s.gridRow = std::atoi(all.c_str() + q + 11);
        size_t c = all.find("\"grid_col\":", q);
        if (c == std::string::npos) break;
        s.gridCol = std::atoi(all.c_str() + c + 11);
        stops.insert(s);
        q = all.find('}', c);
    }
    return stops;
}

} // namespace

TEST_CASE("the gate flags a mapped field that is not a real stop",
          "[crawl][hwdecode]") {
    // A PHANTOM: the driver aims at a cell the cursor never visits. This is
    // M8_DRIVER_BUGS.md #20 exactly -- MST_CHO/DEL/REV pointed at the "MX DE
    // RE" column header instead of the send-return values a row above it -- and
    // #17, a field that does not exist on this firmware at all.
    //
    // MIXER's OUT_VOL is at row 2 col 0. Offer a stop set that has everything
    // except that row, and the gate must name it.
    std::set<CrawlStop> stops = { {9, 1}, {9, 4}, {14, 1} };
    const auto r = checkCrawl(Screen::MIXER, stops);

    REQUIRE(r.checkable);
    bool namedOutVol = false;
    for (const auto& n : r.phantomFields) if (n == "OUT_VOL") namedOutVol = true;
    CHECK(namedOutVol);
}

TEST_CASE("the gate flags a real stop no field claims", "[crawl][hwdecode]") {
    // An UNCLAIMED stop is the more dangerous half. identifyCursorField hands
    // it to whatever mapped field sits nearest to its left and reports that
    // with confidence -- #31, and #21. A partial map is worse than an empty
    // one, which is why this direction is checked at all.
    //
    // Row 9 is MIXER's track-volume row and kMixerFields has no entry for it,
    // so every stop on it must come back unclaimed.
    std::set<CrawlStop> stops = { {9, 1}, {9, 4}, {9, 7} };
    const auto r = checkCrawl(Screen::MIXER, stops);

    REQUIRE(r.checkable);
    CHECK(r.unclaimedStops.size() == 3);
}

TEST_CASE("the gate does not cry wolf on a stop the label covers",
          "[crawl][hwdecode]") {
    // A field's coordinates are its LABEL and the cursor sits on the VALUE, to
    // the right on the same row. If that span were not allowed the gate would
    // report every screen as broken, which is the failure mode that gets a gate
    // switched off.
    //
    // MIXER OUT_VOL is row 2 col 0; its value sits a few columns right.
    std::set<CrawlStop> stops = { {2, 1} };
    const auto r = checkCrawl(Screen::MIXER, stops);

    REQUIRE(r.checkable);
    CHECK(r.unclaimedStops.empty());
}

TEST_CASE("grid screens are reported as not checkable, not as clean",
          "[crawl][hwdecode]") {
    // SONG/CHAIN/PHRASE have no field map by design -- they are addressed by
    // (step, col). Returning "ok" for them would be a gate that passes by
    // knowing nothing, so checkable is false and ok() is false with it.
    std::set<CrawlStop> stops = { {5, 5} };
    const auto r = checkCrawl(Screen::SONG, stops);
    CHECK_FALSE(r.checkable);
    CHECK_FALSE(r.ok());
}

TEST_CASE("the recorded MIXER crawl still disagrees with kMixerFields",
          "[crawl][hwdecode]") {
    // The real artifact, crawled off a device on fw 6.5.2 (22 stops, 82 edges).
    //
    // This is RED ON PURPOSE and asserts the disagreement rather than its
    // absence, because the map has not been corrected yet -- see #20. Two
    // phantoms (MST_REV, DJF_TYP) and fifteen unclaimed stops, including all
    // eight track volumes on row 9, which kMixerFields does not model at all.
    //
    // When the map is fixed, this test fails, and that is the intended signal:
    // flip it to `CHECK(r.ok())` then. Asserting "still broken" keeps the gate
    // exercised in the meantime instead of leaving it dormant until someone
    // remembers to run the crawler.
    bool ok = false;
    const auto stops = loadStops("tests/fixtures/crawl/mixer_fw652.json", ok);
    if (!ok || stops.empty()) {
        WARN("mixer_fw652.json not found from this working directory; skipped");
        return;
    }

    const auto r = checkCrawl(Screen::MIXER, stops);
    REQUIRE(r.checkable);
    INFO("phantoms=" << r.phantomFields.size()
         << " unclaimed=" << r.unclaimedStops.size());
    CHECK(stops.size() == 22);
    CHECK_FALSE(r.phantomFields.empty());
    CHECK_FALSE(r.unclaimedStops.empty());
}
