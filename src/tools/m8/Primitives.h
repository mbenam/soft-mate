#pragma once

// ===========================================================================
// Primitives.h — verified generic primitives for M8 device control.
//
// Tier 3 of M8_DEVICE_CONTROL_SPEC.md. Each primitive is the proven
// read-verify-act loop with a typed post-condition. On failure it returns
// the reason and the decoded screen.
//
// editValue and enterNote are BLOCKED on Tier 2 gesture pinning — they
// assert the gesture table is populated and return an explicit error.
// ===========================================================================

#include "M8Device.h"
#include "ScreenModel.h"
#include "Result.h"
#include <functional>
#include <string>
#include <optional>

namespace m8 {
namespace dev {

// ---- Result type ----------------------------------------------------------

struct JsonResult {
    bool ok = true;
    std::string error;
    ScreenGrid screenSnapshot;   // the decoded screen at the point of failure

    static JsonResult success() { return {true, "", {}}; }
    static JsonResult fail(const std::string& msg, const ScreenGrid& snap) {
        return {false, msg, snap};
    }
};

// ---- Settle-confirm --------------------------------------------------------

// Confirm the screen has settled after a press. Reads the screen, waits an
// additional interval, reads again. Returns true if both reads agree on the
// screen identity (and optionally the cursor field text). Used after any press
// to avoid acting on a transitional/incomplete frame.
//
// expectedScreen: the screen we expect (or Screen::UNKNOWN to skip screen check)
// expectedCursorField: if non-empty, also verify cursorMainText() contains this
// extraSettleMs: additional wait between first and second read
// maxAttempts: total confirm cycles before giving up (each cycle = read + wait + read)
bool confirmRead(M8Device& dev, Screen expectedScreen = Screen::UNKNOWN,
                 const std::string& expectedCursorField = "",
                 int extraSettleMs = 150, int maxAttempts = 3);

// ---- Navigation primitives ------------------------------------------------

// Navigate from the current screen to `target`. Reads the screen after each
// hop to verify the move landed. Bounded retries; fails with the actual
// header if the target is never reached.
//
// Works from any starting screen, including modals (cancels with OPT first).
JsonResult gotoScreen(M8Device& dev, Screen target,
                      int holdMs = 15, int maxHops = 12);

// ---- Cursor movement ------------------------------------------------------

// Move the cursor to a named field on the current screen.
// For form-style screens: uses the field map to find the target, then
// navigates UP/DOWN/LEFT/RIGHT until cursorField() matches.
// For grid-style screens: not yet supported (returns error).
//
// Returns JsonResult with the final screen snapshot.
JsonResult moveCursorTo(M8Device& dev, const std::string& fieldName,
                        int holdMs = 15, int maxSteps = 40);

// Move the cursor to a grid position (step, col) on a grid-style screen.
// step = 0..15 (row in the grid), col = 0..N (column in the grid).
// Uses arrow keys to navigate. Bounded retries.
JsonResult moveCursorToGrid(M8Device& dev, int step, int col,
                            int holdMs = 15, int maxSteps = 100);

// ---- Field reading --------------------------------------------------------

// Read the current value of a named field.
// moveCursorTo the field, then read the value cell(s) to its right.
std::optional<std::string> readField(M8Device& dev, const std::string& fieldName,
                                     int holdMs = 15);

// ---- Bounded scan ---------------------------------------------------------

// Press `key` up to `maxPresses` times, re-reading after each press,
// until `predicate` returns true on the screen grid. Returns the number
// of presses used, or -1 if the predicate was never satisfied.
int pressUntil(M8Device& dev, std::function<bool(const ScreenGrid&)> predicate,
               uint8_t key, int maxPresses = 30, int holdMs = 15);

// ---- List enumeration ------------------------------------------------------

// Enumerate every row of the current browser list, scrolling as needed.
// Scrolls to the top first, then walks DOWN collecting rows until the
// selection stops changing (bottom) or maxRows is hit.
std::vector<ListRow> enumerateList(M8Device& dev, int holdMs = 15,
                                   int maxRows = 256);

// Move the cursor to the named directory row and descend. Verifies the list
// contents changed after EDIT; fails if not.
JsonResult enterDir(M8Device& dev, const std::string& dirName, int holdMs = 15);

// Ascend one level (select "/.." or ".." and press EDIT). Verifies the list changed.
JsonResult upDir(M8Device& dev, int holdMs = 15);

struct FileMatch {
    std::string path;   // "/"-separated, relative to the browser root
    std::string name;   // the row text that matched
};

struct SearchResult {
    std::vector<FileMatch> matches;
    bool truncated = false;
    int  dirsVisited = 0;
    std::string error;    // non-empty if navigation broke mid-search
};

// Depth-first search for rows whose name contains `needle` (case-insensitive,
// alnum-normalized — the same comparison loadFile uses).
SearchResult searchTree(M8Device& dev, const std::string& needle,
                        int maxDepth = 4, int maxVisits = 64, int holdMs = 15);

// Opens the LOAD PROJECT file browser modal.
JsonResult openLoadModal(M8Device& dev, int holdMs = 15);

// Closed-loop directory tree search and load. Navigates to LOAD PROJECT browser,
// searches tree via searchTree, lands on matching file, and opens it.
int searchAndLoad(M8Device& dev, const std::string& needle, int holdMs = 15);

// ---- Assertions -----------------------------------------------------------

// Assert the current screen matches the expected screen.
JsonResult assertScreen(M8Device& dev, Screen expected);

// Assert a named field has the expected value text.
JsonResult assertField(M8Device& dev, const std::string& fieldName,
                       const std::string& expectedValue, int holdMs = 15);

// Assert a text row (by y coordinate) matches a substring.
JsonResult assertRowMatches(M8Device& dev, int y, const std::string& substring);

// Assert playback is active (needs a play indicator readable from framebuffer).
JsonResult assertPlaying(M8Device& dev);

// Assert firmware version is >= major.minor.patch.
JsonResult assertFirmware(M8Device& dev, int major, int minor, int patch);

// ---- Modal handling --------------------------------------------------------

// Dismiss a modal dialog (e.g. "LOSE CHANGES TO CURRENT SONG?").
// Presses EDIT (confirm=true) or OPT (confirm=false), re-reads to verify
// the modal text is gone (via isModal()), retries up to maxRetries times.
// Returns JsonResult::success() when the modal is resolved, or
// JsonResult::fail() with the screen snapshot if it persists.
JsonResult dismissModal(M8Device& dev, bool confirm,
                        int holdMs = 15, int maxRetries = 5);

// ---- Grid geometry ---------------------------------------------------------

// Left-edge X of every column on a grid screen, read off its column-header row.
//
// Each run of consecutive non-space header cells is one column -- single digits
// "1".."8" on SONG, multi-character labels like "FX1" on PHRASE -- so a gap
// wider than one glyph (8px) starts a new column. Exposed for testing because
// this arithmetic is the part of moveCursorToGrid most prone to off-by-one, and
// the rest of that function needs a live device.
//
// Returns an empty vector if the header row has no readable cells.
std::vector<int> gridColumnEdges(const ScreenGrid& grid, int headerY);

// ---- Recovery --------------------------------------------------------------

// Escape an unknown or stuck UI state without human help, and report where we
// landed. Intended as the watchdog action for an unattended driver: call it
// when any other primitive times out or returns an unexpected screen.
//
// Order matters:
//   1. Clear a modal first -- while one is up nothing else responds.
//   2. Then a bounded run of PLAIN UP presses. Per M8_DRIVER_BUGS.md #20 that
//      is the only escape from the MIXER compound widget that hardware testing
//      found reliable; bouncing to another screen and back does not work,
//      because the M8 remembers its per-screen cursor position.
//   3. Re-check for a modal, since an UP press can open one.
//
// `confirmModals` is false by default and should stay false for automated
// recovery: cancelling (OPT) can never commit an edit, whereas confirming
// (EDIT) can. If the modal will not cancel, this FAILS rather than pressing
// EDIT blind -- the caller gets the modal still on screen in the snapshot and
// decides for itself. Pass true only when the caller knows the pending action
// is safe to accept.
JsonResult panicHome(M8Device& dev, int holdMs = 15, int maxUp = 12,
                     bool confirmModals = false);

// ---- File loading ----------------------------------------------------------

// Load a project file on the device. Navigates to PROJECT screen, opens the
// LOAD PROJECT browser, scrolls to the target file, selects it, and confirms
// any modals. Returns 0 on success, nonzero on failure.
int loadFile(M8Device& dev, const std::string& target, int holdMs = 15);

// ---- Edit primitives (BLOCKED on Tier 2) ----------------------------------

// Edit a field to a target value. Requires pinned gestures in hw_buttons.json.
// Returns an explicit "gestures not pinned" error until Tier 2 is completed.
JsonResult editValue(M8Device& dev, const std::string& fieldName,
                     const std::string& targetValue, int holdMs = 15);

// Enter a note at the current cursor position. Requires pinned gestures.
// Returns an explicit "gestures not pinned" error until Tier 2 is completed.
JsonResult enterNote(M8Device& dev, const std::string& noteName,
                     uint8_t velocity = 0xFF, int holdMs = 15);

// Clear the cell at the current cursor position. Requires pinned gestures.
JsonResult clearCell(M8Device& dev, int holdMs = 15);

} // namespace dev
} // namespace m8
