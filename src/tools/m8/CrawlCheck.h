#pragma once

// ===========================================================================
// CrawlCheck.h — diff a recorded cursor crawl against the field maps.
//
// Why this is a header and not just code inside m8_crawl
// ------------------------------------------------------
// The comparison is the gate, and a gate nothing tests is the same trap the
// #34 guard fell into: it passed on its first run, which proved the plumbing
// and nothing about the decision. Here it can be exercised offline against a
// committed crawl artifact, with no device attached.
//
// Two directions, and both matter:
//
//   PHANTOM   a mapped field whose coordinates match no real cursor stop. The
//             driver aims at a cell that does not exist -- M8_DRIVER_BUGS.md
//             #20 (MST_CHO/DEL/REV pointed at a column header), #17 (a field
//             that is not on this firmware at all).
//
//   UNCLAIMED a real cursor stop no mapped field covers. Worse than it sounds:
//             identifyCursorField hands it to whatever mapped field sits
//             nearest to its left and reports that with confidence, which is
//             #31 and #21. A partial map is more dangerous than an empty one.
// ===========================================================================

#include "ScreenModel.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace m8 {
namespace dev {

// A cursor stop in text-grid coordinates.
struct CrawlStop {
    int gridRow = -1;
    int gridCol = -1;
    bool operator<(const CrawlStop& o) const {
        return gridRow != o.gridRow ? gridRow < o.gridRow : gridCol < o.gridCol;
    }
};

struct CrawlCheckResult {
    std::vector<std::string> phantomFields;   // mapped, no stop
    std::vector<CrawlStop>   unclaimedStops;  // stop, no field
    size_t mappedFields = 0;
    bool   checkable = false;                 // false for grid screens / no map
    bool ok() const { return checkable && phantomFields.empty() && unclaimedStops.empty(); }
};

// A field's coordinates are its LABEL. The cursor sits on the VALUE, to the
// right on the same row, so a stop within this many columns of a label counts
// as that field's.
//
// 16 matches identifyCursorField's kMaxFieldSpan on purpose: the gate has to
// judge by the same rule the driver navigates by, or it would pass maps the
// driver still misreads. Widening one without the other is how a gate goes
// quietly useless.
inline constexpr int kCrawlFieldSpan = 16;

inline bool stopBelongsTo(const CrawlStop& s, const FieldInfo& f) {
    // The STOP coordinates, not the label's. A field may declare where the
    // cursor actually lands when that is not "right of the label on the same
    // row" -- see FieldInfo in ScreenModel.h, and #20 for what happens when the
    // two are conflated.
    const int fr = fieldStopRow(f), fc = fieldStopCol(f);
    return s.gridRow == fr && s.gridCol >= fc && s.gridCol - fc <= kCrawlFieldSpan;
}

inline CrawlCheckResult checkCrawl(Screen screen, const std::set<CrawlStop>& stops,
                                   const std::string& instTypeHint = "") {
    CrawlCheckResult r;
    auto map = instTypeHint.empty() ? getFieldMap(screen)
                                    : getFieldMap(screen, instTypeHint);
    if (map.isGrid || !map.fields || map.count == 0) return r;   // checkable = false

    r.checkable = true;
    r.mappedFields = map.count;

    for (size_t i = 0; i < map.count; ++i) {
        bool found = false;
        for (const CrawlStop& s : stops)
            if (stopBelongsTo(s, map.fields[i])) { found = true; break; }
        if (!found) r.phantomFields.push_back(map.fields[i].name);
    }

    for (const CrawlStop& s : stops) {
        bool claimed = false;
        for (size_t i = 0; i < map.count; ++i)
            if (stopBelongsTo(s, map.fields[i])) { claimed = true; break; }
        if (!claimed) r.unclaimedStops.push_back(s);
    }
    return r;
}

} // namespace dev
} // namespace m8
