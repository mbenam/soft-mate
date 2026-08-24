#pragma once

// ===========================================================================
// NavPaths.h — replay a measured route instead of guessing one.
//
// Why this exists
// ---------------
// `moveCursorTo` navigates by heuristic: press toward the target row, then
// toward the target column, with an axis fallback when neither moves. Every
// part of that has its own bug number -- #3, #7, #10, #12, #21, #31 -- because
// the M8's cursor chain is not a grid. It is a chain, and on some screens the
// route to a cell goes LEFT before it goes DOWN. No axis-at-a-time walker finds
// that, and after #20's map was corrected three MIXER fields were still
// unreachable for exactly this reason: correct coordinates, no route.
//
// `m8_crawl` already records the route. It walks the chain until it closes and
// writes, for every stop, the shortest key sequence from home. This reads those
// artifacts back so the driver can replay a route that is known to work rather
// than derive one that might.
//
// Deliberately a FALLBACK, not a replacement. The walker reaches most fields
// fine and is fast; homing first costs a `panicHome` every call. So the walker
// runs first and this catches what it drops. That also means turning this on
// cannot regress anything that already worked -- the change is invisible except
// where the old path failed.
//
// Artifacts live at the repo root under `hw_crawl/<SCREEN>.json`, the same
// bare-relative-path convention `hw_buttons.json` and `hw_theme.json` already
// use. Missing file = no paths = the old behaviour exactly.
// ===========================================================================

#include "ScreenModel.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m8 {
namespace dev {

// One recorded route: key masks to press, in order, starting from a homed
// cursor on the screen the crawl was taken on.
using NavRoute = std::vector<uint8_t>;

class NavPaths {
public:
    // Load (or reload) the crawl for one screen. Returns false if the file is
    // absent or has no stops -- both of which are fine, and mean "no routes
    // known for this screen".
    bool loadScreen(Screen s, const std::string& path);

    // Convenience: hw_crawl/<SCREEN>.json, tried once per screen per process.
    // A screen with no artifact is remembered as such so a missing file is not
    // re-opened on every call.
    bool ensureLoaded(Screen s);

    // The route to a stop, in text-grid coordinates. Empty = not known.
    const NavRoute* route(Screen s, int gridRow, int gridCol) const;

    size_t routeCount(Screen s) const;

private:
    struct Key { int screen; int row; int col;
        bool operator<(const Key& o) const {
            if (screen != o.screen) return screen < o.screen;
            if (row != o.row) return row < o.row;
            return col < o.col;
        } };
    std::map<Key, NavRoute> m_routes;
    std::map<int, bool> m_tried;      // screen -> already attempted a load
};

NavPaths& getNavPaths();

// "UP"/"DOWN"/"LEFT"/"RIGHT" -> key mask. 0 for anything else, so an unknown
// token in an artifact drops that route rather than pressing something random.
uint8_t navKeyMask(const std::string& name);

// Default artifact path for a screen: hw_crawl/<SCREEN>.json
std::string navPathFileFor(Screen s);

} // namespace dev
} // namespace m8
