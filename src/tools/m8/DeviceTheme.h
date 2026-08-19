#pragma once

// ===========================================================================
// DeviceTheme.h — the pinned theme accent used to locate the cursor.
//
// Same shape, and same reasoning, as Gestures.h: something the device decides
// and we cannot derive, pinned once against real hardware into a JSON file and
// loaded at open. Gestures pin *what a key does*; this pins *what the cursor
// looks like*.
//
// Why it needs pinning at all. The M8's display protocol says nothing about
// which cells are the cursor -- the cursor is drawn in the theme's accent
// colour and is otherwise an ordinary cell. So every cursor query in the driver
// bottoms out in ScreenGrid::isCursor(), which is a colour comparison. Until
// 2026-08-18 that comparison was exact equality against one hardcoded RGB, and
// the value was wrong for firmware 6.5.2's own stock theme (hw_findings UI-14).
// The consequence was not a visibly broken cursor: isCursor() returned false
// for every cell on every screen, so cursorRowY/cursorField/moveCursorToGrid
// all returned -1, and the driver reported "the press is not landing" while the
// presses were landing perfectly. A wrong colour presented as a deaf device.
//
// A user-selected theme moves the accent anywhere it likes, so any scheme that
// *assumes* the colour has the same failure waiting in it. Hence pinning by
// observation: press a direction key, see which colour moves. That answer is
// correct for any theme, because it does not ask what the cursor is coloured --
// it asks what follows the cursor keys.
// ===========================================================================

#include <cstdint>
#include <string>

namespace m8 {
namespace dev {

// Where the current accent value came from. Reported by `doctor` and carried in
// error messages, because "which colour are we looking for, and who chose it"
// is the first question worth asking when cursor reads come back empty.
enum class AccentSource {
    BUILTIN_DEFAULT,   // the compiled-in stock value -- a guess until confirmed
    FILE,              // loaded from hw_theme.json
    FLAG,              // --cursor-color on the command line
    CALIBRATED,        // derived on this connection by watching a key press
};

const char* accentSourceName(AccentSource s);

struct ThemeTable {
    bool     pinned = false;          // true once confirmed against hardware
    uint8_t  accent[3] = {0, 240, 248};
    int      tolerance = 16;
    AccentSource source = AccentSource::BUILTIN_DEFAULT;

    // What the device called its theme when this was pinned. The capture
    // reports it (e.g. "m8-default-6.5.2"); a mismatch against the live device
    // means the pin is stale and should be redone rather than trusted.
    std::string themeId;

    int pinnedFwMajor = 0;
    int pinnedFwMinor = 0;
    int pinnedFwPatch = 0;

    // How the accent was determined, in words, for the file and for reports.
    std::string method;

    bool isPinned() const { return pinned; }

    // True if `rgb` is within `tolerance` of the accent on every channel.
    //
    // The tolerance is not slack for a *different* theme -- it is for the same
    // theme read through a different path. It is deliberately far below the
    // spacing between the stock palette's eight entries, so it cannot latch
    // onto a neighbouring colour; widening it is the wrong fix for a theme that
    // does not match, and pinning is the right one.
    bool matches(const uint8_t rgb[3]) const;

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
};

// Global theme table instance, mirroring getGestures().
ThemeTable& getTheme();

} // namespace dev
} // namespace m8
