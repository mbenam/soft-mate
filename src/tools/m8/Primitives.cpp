// ===========================================================================
// Primitives.cpp — verified generic primitives implementation.
// ===========================================================================

#include "Primitives.h"
#include "Gestures.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace m8 {
namespace dev {

// ---- Helpers --------------------------------------------------------------

static std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::string alnumUpper(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)))
            o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

static bool isModal(const ScreenGrid& g) {
    std::string h = toUpper(g.topHeader());
    // topHeader() strips space characters, so check both with-space and
    // without-space variants. The real M8 header is "LOSE CHANGES TO CURRENT
    // SONG?" but topHeader() returns "LOSECHANGESTOCURRENTSONG?".
    return h.find("LOSECHANGES") != std::string::npos
        || h.find("LOSE CHANGES") != std::string::npos
        || h.find("SONG?") != std::string::npos
        || h.find("OVERWRITE") != std::string::npos;
}

// LIVE mode is a performance overlay (header: "LIVE (SEL+LEFT TO EXIT)").
// "SEL" = SHIFT on M8 hardware. SHIFT+LEFT exits it.
static bool isLiveMode(const ScreenGrid& g) {
    std::string h = toUpper(g.topHeader());
    return h.find("LIVE") != std::string::npos
        && h.find("EXIT") != std::string::npos;
}

// Helper to identify the cursor field based on screen coordinates (x, y) in pixels.
static std::optional<std::string> identifyCursorField(M8Device& dev, Screen cur) {
    auto cf = dev.cursorField();
    if (!cf) return std::nullopt;

    int gridCol = cf->col / 8;
    int gridRow = (cf->row / 10) - 3;

    std::string instType;
    if (cur == Screen::INSTRUMENT) {
        instType = readInstrumentType(dev.grid());
    }

    auto map = instType.empty() ? getFieldMap(cur) : getFieldMap(cur, instType);
    if (map.isGrid || !map.fields) return std::nullopt;

    const FieldInfo* best = nullptr;
    for (size_t i = 0; i < map.count; ++i) {
        if (map.fields[i].row == gridRow && map.fields[i].col <= gridCol) {
            if (!best || map.fields[i].col > best->col) {
                best = &map.fields[i];
            }
        }
    }

    if (best) return best->name;
    return std::nullopt;
}

// ---- confirmRead -----------------------------------------------------------

bool confirmRead(M8Device& dev, Screen expectedScreen,
                 const std::string& expectedCursorField,
                 int extraSettleMs, int maxAttempts) {
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        // First read.
        dev.readScreen();
        Screen s1 = identifyScreen(dev.grid());

        // Wait for the display to settle further.
        std::this_thread::sleep_for(std::chrono::milliseconds(extraSettleMs));

        // Second read.
        dev.readScreen();
        Screen s2 = identifyScreen(dev.grid());

        // Check screen identity agreement.
        bool screenOk = (expectedScreen == Screen::UNKNOWN) ? (s2 != Screen::UNKNOWN) : (s1 == s2 && s2 == expectedScreen);
        bool screenAgree = (s1 == s2);

        // Check cursor field agreement (if requested).
        bool fieldOk = expectedCursorField.empty();
        bool fieldAgree = true;
        if (!expectedCursorField.empty()) {
            auto f1 = identifyCursorField(dev, s1);
            auto f2 = identifyCursorField(dev, s2);
            fieldAgree = (f1 == f2);
            fieldOk = fieldAgree && f2.has_value() && (f2.value() == expectedCursorField);
        }

        if (screenAgree && fieldAgree) {
            // Both reads agree. Check if the agreed state matches expectations.
            if (screenOk && fieldOk) {
                return true;
            }
            // Reads agree but don't match expectations — we're stably on the wrong
            // screen/field. No point retrying; the caller should handle it.
            return false;
        }

        // Reads disagree — transitional frame caught. Retry with a longer wait.
        extraSettleMs *= 2;
    }
    return false;  // never settled
}

// ---- dismissModal ----------------------------------------------------------

JsonResult dismissModal(M8Device& dev, bool confirm, int holdMs, int maxRetries) {
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        // Check if we're still in a modal.
        dev.readScreen();
        if (!isModal(dev.grid())) return JsonResult::success();

        // Press the dismiss key.
        dev.press(confirm ? Key::EDIT : Key::OPT, holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Confirm the modal is gone.
        if (!confirmRead(dev)) dev.readScreen();
        if (!isModal(dev.grid())) return JsonResult::success();
    }
    return JsonResult::fail("dismissModal: modal still present after "
                            + std::to_string(maxRetries) + " attempts",
                            dev.grid());
}

// ---- gotoScreen -----------------------------------------------------------

JsonResult gotoScreen(M8Device& dev, Screen target, int holdMs, int maxHops) {
    // Read current screen with confirm-read to avoid transitional frames.
    confirmRead(dev);
    Screen cur = identifyScreen(dev.grid());

    if (cur == target) return JsonResult::success();

    // If in LIVE mode, exit with SHIFT+LEFT (SEL+LEFT per on-screen hint).
    if (isLiveMode(dev.grid())) {
        dev.press(Key::SHIFT | Key::LEFT, holdMs);
        if (!confirmRead(dev)) {
            // Extra settle if confirmRead didn't fully agree.
            dev.readScreen();
        }
        cur = identifyScreen(dev.grid());
        if (cur == target) return JsonResult::success();
    }

    // If in a modal, dismiss with the read-verify-retry primitive.
    if (isModal(dev.grid())) {
        auto mr = dismissModal(dev, false, holdMs);
        if (!mr.ok) return mr;
        cur = identifyScreen(dev.grid());
        if (cur == target) return JsonResult::success();
    }

    // Closed-loop route execution, one hop at a time.
    for (int hop = 0; hop < maxHops; ++hop) {
        if (cur == target) return JsonResult::success();

        auto steps = computeRoute(cur, target);
        if (steps.empty()) {
            // Direct route failed — try climbing to PROJECT as a neutral starting point.
            // This handles cases where the current screen's grid position is unknown.
            Screen project = Screen::PROJECT;
            auto climbSteps = computeRoute(cur, project);
            if (climbSteps.empty()) {
                return JsonResult::fail("gotoScreen: no route to target and cannot climb", dev.grid());
            }
            auto& cs = climbSteps[0];
            dev.press(cs.keyMask, holdMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            confirmRead(dev);
            if (isModal(dev.grid())) {
                dismissModal(dev, false, holdMs);
            }
            cur = identifyScreen(dev.grid());
            continue;
        }

        // Take the first step of the route
        auto& step = steps[0];
        bool stepOk = false;
        std::printf("gotoScreen: hop from %s (cur=%d) to %s (keyMask=0x%02X)\n",
                    dev.grid().canon().c_str(), static_cast<int>(cur),
                    step.viaScreen == target ? "target" : "via",
                    step.keyMask);
        for (int pressAttempt = 0; pressAttempt < 3; ++pressAttempt) {
            dev.press(step.keyMask, holdMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            confirmRead(dev);

            if (isModal(dev.grid())) {
                dismissModal(dev, false, holdMs);
            }

            Screen arrived = identifyScreen(dev.grid());
            std::printf("  attempt %d: arrived at %s (canon=%s)\n",
                        pressAttempt,
                        arrived == Screen::UNKNOWN ? "UNKNOWN" : dev.grid().canon().c_str(),
                        dev.grid().canon().c_str());
            if (arrived == step.viaScreen || arrived == target) {
                cur = arrived;
                stepOk = true;
                break;
            }
            // If we moved to a different known screen, update and re-route
            if (arrived != cur && arrived != Screen::UNKNOWN) {
                cur = arrived;
                stepOk = true;
                break;
            }
            // Otherwise arrived == cur (key missed), so we loop and retry press.
        }

        if (!stepOk) {
            return JsonResult::fail("gotoScreen: screen stuck at " + dev.grid().canon(), dev.grid());
        }
    }

    if (cur != target) {
        return JsonResult::fail(
            "gotoScreen: reached " + dev.grid().canon() + " instead of target",
            dev.grid());
    }
    return JsonResult::success();
}

// ---- moveCursorTo ---------------------------------------------------------

// Parse a grid-screen field name into (step, col) coordinates.
// Supported formats:
//   "STEPn"  → step n, col 0  (any grid screen)
//   "Sn"     → step n, col 0  (SONG/CHAIN/PHRASE/TABLE/GROOVE/INST_POOL)
//   "TcSn"   → track c, step n (SONG only, c = hex track 0-7)
//
// n is a hex digit 0-F (case-insensitive). c is a hex digit 0-7.
static bool parseGridField(const std::string& name, Screen screen, int& step, int& col) {
    std::string u = name;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    col = 0;
    step = -1;

    // "STEPn" — universal grid format.
    if (u.size() >= 4 && u.substr(0, 4) == "STEP") {
        char ch = u[4];
        if (ch >= '0' && ch <= '9') step = ch - '0';
        else if (ch >= 'A' && ch <= 'F') step = 10 + (ch - 'A');
        if (u.size() >= 6 && u[5] == 'C') {
            // "STEP5C3" → step 5, col 3
            char cc = u[6];
            if (cc >= '0' && cc <= '9') col = cc - '0';
            else if (cc >= 'A' && cc <= 'F') col = 10 + (cc - 'A');
        }
        return step >= 0;
    }

    // "TcSn" — SONG-specific: track c, step n.
    if (screen == Screen::SONG && u.size() >= 4 && u[0] == 'T' && u[2] == 'S') {
        char tc = u[1], sc = u[3];
        int track = -1;
        if (tc >= '0' && tc <= '7') track = tc - '0';
        if (sc >= '0' && sc <= '9') step = sc - '0';
        else if (sc >= 'A' && sc <= 'F') step = 10 + (sc - 'A');
        if (track >= 0 && step >= 0) { col = track; return true; }
    }

    // "Sn" — short step format (any grid screen).
    if (u.size() == 2 && u[0] == 'S') {
        char sc = u[1];
        if (sc >= '0' && sc <= '9') step = sc - '0';
        else if (sc >= 'A' && sc <= 'F') step = 10 + (sc - 'A');
        return step >= 0;
    }

    // Plain hex number: "5" or "A" → step, col 0.
    if (u.size() == 1) {
        char sc = u[0];
        if (sc >= '0' && sc <= '9') { step = sc - '0'; return true; }
        if (sc >= 'A' && sc <= 'F') { step = 10 + (sc - 'A'); return true; }
    }

    return false;
}

JsonResult moveCursorTo(M8Device& dev, const std::string& fieldName,
                        int holdMs, int maxSteps) {
    dev.readScreen();
    Screen cur = identifyScreen(dev.grid());

    // Auto-navigate to the correct screen if the field is not on the current screen.
    Screen targetScreen = findScreenForField(fieldName);
    if (targetScreen != Screen::UNKNOWN && targetScreen != cur) {
        auto navRes = gotoScreen(dev, targetScreen, holdMs);
        if (!navRes.ok) return navRes;
        cur = targetScreen; // update current screen after navigation
    }

    // Extract instrument type from the framebuffer when on INSTRUMENT screen.
    std::string instType;
    if (cur == Screen::INSTRUMENT) {
        instType = readInstrumentType(dev.grid());
    }

    auto fmap = instType.empty() ? getFieldMap(cur) : getFieldMap(cur, instType);

    if (fmap.isGrid) {
        int step = -1, col = 0;
        if (!parseGridField(fieldName, cur, step, col)) {
            return JsonResult::fail(
                "moveCursorTo: cannot parse grid field '" + fieldName + "' on screen "
                + dev.grid().canon() + ". Use STEPn (e.g. STEP5, STEP5C3, S5, T0S5, or just 5).",
                dev.grid());
        }
        return moveCursorToGrid(dev, step, col, holdMs, maxSteps);
    }

    // Find the target field in the type-aware field map.
    auto target = instType.empty()
        ? findFieldOnScreen(cur, fieldName)
        : findFieldOnScreen(cur, fieldName, instType);
    if (!target) {
        return JsonResult::fail("moveCursorTo: field '" + fieldName + "' not found on screen",
                                dev.grid());
    }

    // Check if already on target by coordinate matching.
    auto curFieldName = identifyCursorField(dev, cur);
    if (curFieldName && curFieldName.value() == target->name) {
        return JsonResult::success();  // already on target
    }

    // Navigate: use the target field's Y display row directly from the field map coordinates.
    int targetDisplayRow = target->row;

    // Tracks the previous iteration's row, to detect when vertical movement
    // has stopped making progress (see the stuck-on-row fallback below).
    int prevY = -999;

    // Navigate UP/DOWN toward the target display row.
    for (int i = 0; i < maxSteps; ++i) {
        auto cfName = identifyCursorField(dev, cur);
        if (cfName && cfName.value() == target->name) {
            // Candidate landing — confirm the cursor field is stable.
            if (confirmRead(dev, cur, target->name)) {
                return JsonResult::success();
            }
            // Confirm failed — cursor field was a transitional read.
            // Re-read and continue navigating.
            dev.readScreen();
            continue;
        }

        auto cf = dev.cursorField();
        int curY = cf ? ((cf->row / 10) - 3) : -1;
        int curCol = cf ? (cf->col / 8) : -1;

        // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, MIXER screen): some
        // compound widgets are only enterable via LEFT/RIGHT -- plain DOWN from
        // IN_VOL (row 15, col 13) does not descend into the MX/DE/RE column
        // (col 10, rows 16-18) at all; it lands on row 15 and DOWN then has no
        // further effect. Only after LEFT moves into that column (landing on
        // whichever row it last had focus on -- sticky memory) does UP/DOWN
        // start working normally again within it. Detect this by noticing that
        // a vertical press didn't change the row even though we're not yet on
        // the target row, and fall back to the horizontal axis for one step.
        //
        // The fallback is deliberately single-shot: after firing it, prevY is
        // reset so the NEXT iteration re-attempts the vertical direction
        // rather than staying latched onto the horizontal axis. Hardware-
        // confirmed (2026-07-18, real M8 fw 6.5.2, SCALE screen): a latched
        // fallback that keeps pressing LEFT/RIGHT forever once triggered can
        // walk off the edge of a row's own sub-fields (e.g. OFFSET -> EN) and
        // then loop forever once THAT axis also stops making progress,
        // without ever giving the vertical direction another chance --
        // exactly the SCALE per-note EN/OFFSET rows (not present in
        // kScaleFields at all) were doing to the TUNE search. Alternating
        // (vertical, horizontal, vertical, horizontal, ...) escapes that trap.
        // A 1-column difference is measurement noise, not a real mismatch --
        // e.g. SYSTEM's label measures at pixel col 1 even though its field
        // map entry says col 0 (rounding in the pixel-to-column conversion).
        // identifyCursorField()'s own matching already tolerates this via its
        // "col <= gridCol, greatest wins" rule; this fallback needs the same
        // tolerance or it misfires into a useless LEFT/RIGHT loop whenever a
        // vertical press is merely slow to register (as happened searching
        // for SAMPLEROOT from SYSTEM, a plain single-column list with no
        // column-crossing involved at all).
        bool colMismatch = curCol >= 0 && std::abs(curCol - target->col) > 1;
        bool rowStuck = (curY >= 0 && curY == prevY && curY != targetDisplayRow);
        prevY = curY;

        if (curY < 0) {
            dev.press(Key::DOWN, holdMs);
        } else if (rowStuck && colMismatch) {
            uint8_t dir = (curCol > target->col) ? Key::LEFT : Key::RIGHT;
            dev.press(dir, holdMs);
            prevY = -999;  // give the vertical direction another chance next iteration
        } else if (curY < targetDisplayRow) {
            dev.press(Key::DOWN, holdMs);
        } else if (curY > targetDisplayRow) {
            dev.press(Key::UP, holdMs);
        } else if (curCol >= 0 && curCol > target->col) {
            // Same row, target field is to the left (e.g. landed on a right-column
            // field like AMP via DOWN, but the target is the left-column field on
            // that same row, e.g. SLICE). Hardware-confirmed (2026-07-18, real M8
            // fw 6.5.2, Instrument/Sampler screen): a plain DOWN from SAMPLE (a
            // full-width row) lands directly on AMP (col 17), never on SLICE
            // (col 0) even though they share a row -- the device does not visit
            // every left-column field via straight DOWN/UP. LEFT/RIGHT is required
            // to switch columns within a row.
            dev.press(Key::LEFT, holdMs);
        } else {
            // Same row but not the target field — try RIGHT for multi-column forms.
            dev.press(Key::RIGHT, holdMs);
        }
        // Post-press settle: wait for the CURSOR FIELD reading itself to
        // stabilize, not just the screen identity. Hardware-confirmed
        // (2026-07-18, real M8 fw 6.5.2, MIXER screen): confirmRead(dev, cur)
        // with no expected field only compares screen identity between two
        // reads, which is trivially true for any in-screen cursor move (the
        // screen never changes) -- so it always "succeeds" on the very first
        // attempt regardless of whether the cursor has actually finished
        // moving. On the MIXER screen's more complex compound-widget redraws
        // this let transitional frames through, causing loop iterations to
        // read a stale cursor field and mis-decide the next key to press
        // (observed as double LEFT presses firing before the first had
        // visually landed). Poll identifyCursorField() until two consecutive
        // reads agree instead.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        dev.readScreen();
        auto settleField = identifyCursorField(dev, cur);
        for (int s = 0; s < 4; ++s) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            dev.readScreen();
            auto settleField2 = identifyCursorField(dev, cur);
            if (settleField2 == settleField) break;
            settleField = settleField2;
        }
    }

    return JsonResult::fail("moveCursorTo: could not reach '" + fieldName + "' in "
                            + std::to_string(maxSteps) + " steps", dev.grid());
}

// ---- moveCursorToGrid ------------------------------------------------------

// Detect the pixel pitch (gap between data rows) from the grid cells.
static int detectCellPitch(const ScreenGrid& grid) {
    std::set<int> ys;
    for (auto& [pos, c] : grid.cells) {
        if (pos.second < ScreenGrid::MAIN_X_MAX && c.ch != ' ')
            ys.insert(pos.first);
    }
    if (ys.size() < 3) return 10;  // default M8 pitch
    int minGap = 999, prev = *ys.begin();
    for (auto it = std::next(ys.begin()); it != ys.end(); ++it) {
        int gap = *it - prev;
        if (gap > 1 && gap < minGap) minGap = gap;
        prev = *it;
    }
    return (minGap < 999) ? minGap : 10;
}

// Detect the pixel pitch (gap between data columns) from the grid cells.
static int detectColPitch(const ScreenGrid& grid) {
    std::set<int> xs;
    for (auto& [pos, c] : grid.cells) {
        if (pos.first >= 0 && c.ch != ' ')
            xs.insert(pos.second);
    }
    if (xs.size() < 3) return 8;  // default M8 pitch
    int minGap = 999, prev = *xs.begin();
    for (auto it = std::next(xs.begin()); it != xs.end(); ++it) {
        int gap = *it - prev;
        if (gap > 1 && gap < minGap) minGap = gap;
        prev = *it;
    }
    return (minGap < 999) ? minGap : 8;
}

// Find the row/col label cell reading exactly `text` in the leftmost label
// column (x < 16). Layout-based, not cursor-state-based: on a real device
// the step-0 row label ("0") sits at a fixed pixel position regardless of
// where the cursor currently is, so this gives a stable calibration point.
static bool findLabelCell(const ScreenGrid& grid, const std::string& text, int& y) {
    for (auto& [pos, c] : grid.cells) {
        if (pos.second < 16 && c.ch == static_cast<uint8_t>(text[0])) {
            y = pos.first;
            return true;
        }
    }
    return false;
}

// Find the accent-cyan (cursor) cell in the *data* area (at or below
// `minRowY`), outside any highlight rect. This is the ONLY reliable cursor
// signal on grid screens.
//
// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, PHRASE screen): two
// unrelated things are drawn in accent-cyan simultaneously -- the column
// header label (e.g. "N" in the "N V I FX1 FX2 FX3" header row, showing
// which COLUMN is selected) and the actual step-row cursor. Scanning cells
// in plain row-major order without a floor picks up the header label first
// (it's above the data rows, i.e. a smaller Y), never the real cursor.
// `minRowY` -- callers pass firstStepY, the step-0 row's Y -- excludes the
// header area entirely.
//
// Separately (also hardware-confirmed): a static '<' (0x3C) glyph can
// appear at a fixed data row with fg == bg (i.e. genuinely invisible on the
// actual display, likely a dormant playhead marker) and does NOT track
// cursor movement at all -- it sat at row 3 through 8 consecutive DOWN
// presses while the real, accent-colored cursor correctly advanced
// 7->8->9->A->B->C->D->E->F. Trusting the glyph over color is exactly the
// "unreliable on PHRASE/TABLE" bug M8_DEVICE_CONTROL_SPEC.md documents --
// so this scan uses color only, never the glyph.
static bool findCursorCell(const ScreenGrid& grid, int minRowY, int& y, int& x) {
    for (auto& [pos, c] : grid.cells) {
        if (pos.first >= minRowY && grid.isCursor(c) && pos.second < ScreenGrid::MAIN_X_MAX
            && !grid.isInHighlight(pos.second, pos.first)) {
            y = pos.first; x = pos.second;
            return true;
        }
    }
    return false;
}

JsonResult moveCursorToGrid(M8Device& dev, int targetStep, int targetCol,
                            int holdMs, int maxSteps) {
    // Grid screens need longer hold for key processing.
    int effectiveHold = std::max(holdMs, 40);
    dev.readScreen();
    Screen cur = identifyScreen(dev.grid());
    auto fmap = getFieldMap(cur);
    if (!fmap.isGrid) {
        return JsonResult::fail("moveCursorToGrid: not a grid-style screen", dev.grid());
    }

    int rowPitch = detectCellPitch(dev.grid());
    int colPitch = detectColPitch(dev.grid());

    // Calibrate step 0's Y from its row label ("0"), not from cursor state --
    // this is fixed screen layout, unaffected by where the cursor is now.
    int firstStepY = -1;
    if (!findLabelCell(dev.grid(), "0", firstStepY)) {
        return JsonResult::fail("moveCursorToGrid: cannot find step-0 row label", dev.grid());
    }

    // Column 0's X: from the current cursor cell if visible now (floored to
    // the data area so the header's cyan column label can't be mistaken for
    // it), else from the step-0 label's own column (both share the left edge).
    int firstColX = -1;
    int cy = -1, cx = -1;
    if (findCursorCell(dev.grid(), firstStepY, cy, cx)) {
        firstColX = cx;
    } else {
        for (auto& [pos, c] : dev.grid().cells) {
            if (pos.first == firstStepY && pos.second < 16) { firstColX = pos.second; break; }
        }
    }
    if (firstColX < 0) {
        return JsonResult::fail("moveCursorToGrid: cannot detect column position", dev.grid());
    }

    // Calculate current position from the accent-colored cursor cell only.
    int curStepY = cy, curColX = cx;
    if (curStepY < 0 && !findCursorCell(dev.grid(), firstStepY, curStepY, curColX)) {
        return JsonResult::fail("moveCursorToGrid: cannot detect cursor position", dev.grid());
    }
    int curStep = (curStepY - firstStepY) / rowPitch;
    int curCol = (curColX - firstColX) / colPitch;

    // Navigate to target.
    int lastStep = curStep, lastCol = curCol;
    int noProgressCount = 0;
    for (int i = 0; i < maxSteps; ++i) {
        // Re-read screen and detect actual position.
        dev.readScreen();
        int actualStepY = -1, actualColX = -1;
        if (findCursorCell(dev.grid(), firstStepY, actualStepY, actualColX)) {
            curStep = (actualStepY - firstStepY) / rowPitch;
            curCol = (actualColX - firstColX) / colPitch;
        }

        if (curStep == targetStep && curCol == targetCol) {
            return JsonResult::success();
        }

        // Bail out if position hasn't changed (hardware won't move cursor).
        if (curStep == lastStep && curCol == lastCol) {
            noProgressCount++;
            if (noProgressCount >= 5) break;
        } else {
            noProgressCount = 0;
            lastStep = curStep;
            lastCol = curCol;
        }

        // Move vertically first.
        if (curStep < targetStep) {
            dev.press(Key::DOWN, effectiveHold);
        } else if (curStep > targetStep) {
            dev.press(Key::UP, effectiveHold);
        } else if (curCol < targetCol) {
            dev.press(Key::RIGHT, effectiveHold);
        } else if (curCol > targetCol) {
            dev.press(Key::LEFT, effectiveHold);
        }
        dev.step();
    }

    return JsonResult::fail("moveCursorToGrid: could not reach step " + std::to_string(targetStep)
                            + " col " + std::to_string(targetCol), dev.grid());
}

// ---- readField ------------------------------------------------------------

std::optional<std::string> readField(M8Device& dev, const std::string& fieldName,
                                     int holdMs) {
    auto result = moveCursorTo(dev, fieldName, holdMs);
    if (!result.ok) return std::nullopt;

    // Final confirm-read: verify the cursor is stable on the target field
    // before reading its value. This catches the case where moveCursorTo
    // returned success on a transitional read but the cursor drifted.
    if (!confirmRead(dev, Screen::UNKNOWN, fieldName)) {
        // One more try — re-read and check.
        dev.readScreen();
    }

    auto cf = dev.cursorField();
    if (!cf) return std::nullopt;

    // Read the full row text (all colors). On form screens the cursor
    // accent cells are only the field labels; the values are in normal
    // text. We return the entire row so assertField's substring matching
    // can locate the expected value within it.
    // Read all text on the cursor row. mainRows() uses pixel-y keys.
    typedef std::vector<std::pair<int, std::string> > RowVec;
    RowVec rows = dev.grid().mainRows();
    for (RowVec::size_type ri = 0; ri < rows.size(); ++ri) {
        if (rows[ri].first == cf->row) {
            std::string s = rows[ri].second;
            size_t b = s.find_first_not_of(' ');
            if (b != std::string::npos) s = s.substr(b);
            size_t e = s.find_last_not_of(' ');
            if (e != std::string::npos) s = s.substr(0, e + 1);
            return s.empty() ? std::nullopt : std::optional<std::string>(s);
        }
    }
    return std::nullopt;
}

// ---- pressUntil -----------------------------------------------------------

int pressUntil(M8Device& dev, std::function<bool(const ScreenGrid&)> predicate,
               uint8_t key, int maxPresses, int holdMs) {
    dev.readScreen();
    if (predicate(dev.grid())) return 0;

    for (int i = 0; i < maxPresses; ++i) {
        dev.press(key, holdMs);
        dev.step();
        if (predicate(dev.grid())) return i + 1;
    }
    return -1;
}

// ---- Assertions -----------------------------------------------------------

JsonResult assertScreen(M8Device& dev, Screen expected) {
    dev.readScreen();
    Screen cur = identifyScreen(dev.grid());
    if (cur == expected) return JsonResult::success();
    return JsonResult::fail(
        "assertScreen: expected screen " + std::to_string(static_cast<int>(expected))
        + " but got '" + dev.grid().canon() + "'", dev.grid());
}

JsonResult assertField(M8Device& dev, const std::string& fieldName,
                       const std::string& expectedValue, int holdMs) {
    auto val = readField(dev, fieldName, holdMs);
    if (!val) {
        return JsonResult::fail("assertField: could not read field '" + fieldName + "'",
                                dev.grid());
    }
    // Compare case-insensitively for enum labels.
    std::string got = toUpper(*val);
    std::string want = toUpper(expectedValue);
    // Trim both.
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(' '));
        s.erase(s.find_last_not_of(' ') + 1);
    };
    trim(got);
    trim(want);
    if (got == want || got.find(want) != std::string::npos || want.find(got) != std::string::npos)
        return JsonResult::success();
    return JsonResult::fail(
        "assertField: '" + fieldName + "' = '" + *val + "', expected '" + expectedValue + "'",
        dev.grid());
}

JsonResult assertRowMatches(M8Device& dev, int y, const std::string& substring) {
    dev.readScreen();
    auto rows = dev.rows();
    for (auto& [rowY, text] : rows) {
        if (rowY == y) {
            if (text.find(substring) != std::string::npos)
                return JsonResult::success();
            return JsonResult::fail(
                "assertRowMatches: row " + std::to_string(y) + " = '" + text
                + "', expected substring '" + substring + "'", dev.grid());
        }
    }
    return JsonResult::fail(
        "assertRowMatches: row " + std::to_string(y) + " not found", dev.grid());
}

JsonResult assertPlaying(M8Device& dev) {
    // The M8's play indicator is not reliably readable from the framebuffer
    // in all themes. This is a placeholder that checks for a play-mode
    // indicator if visible.
    // TODO: pin which cells show the play indicator on the real device.
    return JsonResult::fail("assertPlaying: play indicator not yet pinned to framebuffer",
                            dev.grid());
}

JsonResult assertFirmware(M8Device& dev, int major, int minor, int patch) {
    dev.readScreen();
    Firmware fw = dev.firmware();
    Firmware want{-1, major, minor, patch, -1};
    if (fw >= want) return JsonResult::success();
    return JsonResult::fail(
        "assertFirmware: device fw " + std::to_string(fw.major) + "."
        + std::to_string(fw.minor) + "." + std::to_string(fw.patch)
        + " < required " + std::to_string(major) + "."
        + std::to_string(minor) + "." + std::to_string(patch),
        dev.grid());
}

// ---- Edit primitives -------------------------------------------------------

// Navigate to a field by name, trying moveCursorTo first, then pressUntil fallback.
static bool navigateToField(M8Device& dev, Screen targetScreen,
                            const std::string& fieldName, int holdMs) {
    gotoScreen(dev, targetScreen, holdMs);
    auto mc = moveCursorTo(dev, fieldName, holdMs);
    if (mc.ok) return true;

    // Fallback: SHIFT+DOWN to cycle through fields until we see the target.
    std::string targetUpper = fieldName;
    for (auto& c : targetUpper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int presses = pressUntil(dev, [&](const ScreenGrid& g) -> bool {
        auto rows = g.mainRows();
        for (auto& [y, text] : rows) {
            std::string t = text;
            for (auto& c : t)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (t.find(targetUpper) != std::string::npos) return true;
        }
        return false;
    }, Key::SHIFT | Key::DOWN, 30, holdMs);

    return presses >= 0;
}

// Look up a field's on-screen label text (distinct from its canonical name --
// e.g. QUANTIZE's label is "LIVE QUANTIZE"), so readCursorValue can strip
// exactly that prefix rather than guessing by character class.
static std::string findFieldLabel(Screen s, const std::string& name, const std::string& typeHint) {
    auto map = typeHint.empty() ? getFieldMap(s) : getFieldMap(s, typeHint);
    if (map.isGrid || !map.fields) return "";
    std::string upper = name;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (size_t i = 0; i < map.count; ++i) {
        std::string fn = map.fields[i].name;
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fn == upper) return map.fields[i].label;
    }
    return "";
}

// Read the cursor value text, trimming the field label prefix.
//
// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, Instrument/MacroSynth
// screen): this function's own doc comment ("trimming the field label
// prefix") was aspirational, not actual -- it never trimmed anything.
// cursorMainText() on multi-column form rows (e.g. "TIMBRE 80  AMP FF")
// returns the label concatenated directly against the value with no
// separator ("AMPFF"), and editValue's numeric convergence check parses the
// LEADING hex run of that string -- for "AMPFF", 'A' is a valid hex digit
// (it's also a letter), so the parse silently grabbed "A" (=10) as if it
// were the whole value and never matched any real target, running all 256
// steps and clamping at 0xFF. Stripping the field's exact label text (not a
// generic "skip leading letters" heuristic -- that fails too, since hex
// digits A-F are also letters, e.g. "TIMBRE80" would wrongly consume the
// trailing 'E' of TIMBRE as part of the value) fixes this at the source.
static std::string readCursorValue(M8Device& dev, const std::string& label = "") {
    std::string txt = dev.grid().cursorMainText();
    while (!txt.empty() && txt.back() == '\n') txt.pop_back();
    if (!label.empty()) {
        std::string upperTxt = txt, upperLabel = label;
        for (auto& c : upperTxt) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (auto& c : upperLabel) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upperTxt.compare(0, upperLabel.size(), upperLabel) == 0) {
            txt = txt.substr(upperLabel.size());
        }
    }
    size_t start = txt.find_first_not_of(" \t");
    return (start == std::string::npos) ? "" : txt.substr(start);
}

// Check if a string looks like a number (optional leading sign, digits, optional hex).
static bool isNumeric(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '-' || s[i] == '+') i++;
    if (i >= s.size()) return false;
    bool hasDigit = false;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { hasDigit = true; continue; }
        if (i == 1 && (s[0] == '0') && (c == 'x' || c == 'X')) continue;
        if (c >= 'A' && c <= 'F') { hasDigit = true; continue; }
        if (c >= 'a' && c <= 'f') { hasDigit = true; continue; }
        return false;
    }
    return hasDigit;
}

JsonResult editValue(M8Device& dev, const std::string& fieldName,
                     const std::string& targetValue, int holdMs) {
    auto& g = getGestures();
    if (!g.isReady()) {
        return JsonResult::fail(
            "editValue: gestures not pinned — Tier 2 hardware gesture pinning required. "
            "Run m8_nav --pin-gestures on a real M8 headless to populate hw_buttons.json.",
            dev.grid());
    }

    // Find which screen the field is on.
    Screen targetScreen = findScreenForField(fieldName);
    if (targetScreen == Screen::UNKNOWN) {
        return JsonResult::fail("editValue: field '" + fieldName + "' not found in any screen",
                                dev.grid());
    }

    // Navigate to the field.
    if (!navigateToField(dev, targetScreen, fieldName, holdMs)) {
        return JsonResult::fail("editValue: could not navigate to '" + fieldName + "'",
                                dev.grid());
    }

    // Look up the field's on-screen label so readCursorValue can strip it
    // from cursorMainText()'s "LABELVALUE" concatenation (see readCursorValue's
    // comment). Needs the same instrument-type hint moveCursorTo uses.
    std::string instTypeForLabel;
    if (targetScreen == Screen::INSTRUMENT) instTypeForLabel = readInstrumentType(dev.grid());
    std::string fieldLabel = findFieldLabel(targetScreen, fieldName, instTypeForLabel);

    // Read current value.
    dev.readScreen(200, 300);
    std::string currentRaw = readCursorValue(dev, fieldLabel);

    // If target matches current, nothing to do.
    if (currentRaw.find(targetValue) != std::string::npos) {
        return JsonResult::success();
    }

    // For numeric targets: use step-by-step editing.
    if (isNumeric(targetValue)) {
        int target = static_cast<int>(std::strtol(targetValue.c_str(), nullptr, 0));
        int maxSteps = 256;  // safety limit
        for (int i = 0; i < maxSteps; ++i) {
            dev.readScreen(200, 300);

            // Handle modals if triggered (e.g. destructive edits or confirms)
            if (isModal(dev.grid())) {
                auto mr = dismissModal(dev, true, holdMs);
                if (!mr.ok) return mr;
                dev.readScreen(200, 300);
            }

            std::string val = readCursorValue(dev, fieldLabel);

            // Check if we've reached the target. Compare parsed values, not
            // raw strings: the device always displays hex without a "0x"
            // prefix (e.g. "40"), so a target passed as "0x40" would never
            // string-match the screen text even once convergence succeeds
            // (hardware-confirmed 2026-07-18: editValue looped all 256 steps
            // and failed despite the field genuinely reaching the target).
            // Parse the leading hex run from the screen text the same way
            // `target` itself was parsed, so both forms ("40" and "0x40")
            // compare correctly.
            size_t hexEnd = 0;
            while (hexEnd < val.size() && std::isxdigit(static_cast<unsigned char>(val[hexEnd])))
                hexEnd++;
            if (hexEnd > 0) {
                int screenVal = static_cast<int>(std::strtol(val.substr(0, hexEnd).c_str(), nullptr, 16));
                if (screenVal == target) {
                    return JsonResult::success();
                }
                // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2, Instrument/
                // MacroSynth AMP field): pressing value_inc unconditionally
                // clamps at 0xFF rather than wrapping back to 0x00 -- a target
                // BELOW the current value could never be reached by only ever
                // incrementing. Pick the direction that actually closes the gap.
                dev.press(screenVal < target ? g.valueInc : g.valueDec, holdMs);
            } else {
                // Couldn't parse a value at all this read (transitional frame,
                // or a screen we don't fully understand yet) -- default to
                // increment as a best-effort nudge rather than getting stuck.
                dev.press(g.valueInc, holdMs);
            }
            dev.step();
        }
        dev.readScreen(200, 300);
        return JsonResult::fail("editValue: could not reach target '" + targetValue
                                + "' after " + std::to_string(maxSteps) + " steps",
                                dev.grid());
    }

    // For enum targets: step through until the cursor text contains the target.
    std::string targetUpper = targetValue;
    for (auto& c : targetUpper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int maxSteps = 30;  // enums have at most ~15 values
    for (int i = 0; i < maxSteps; ++i) {
        dev.readScreen(200, 300);

        // Handle modals if triggered (e.g. destructive edits or confirms)
        if (isModal(dev.grid())) {
            auto mr = dismissModal(dev, true, holdMs);
            if (!mr.ok) return mr;
            dev.readScreen(200, 300);
        }

        std::string val = readCursorValue(dev, fieldLabel);
        std::string valUpper = val;
        for (auto& c : valUpper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (valUpper.find(targetUpper) != std::string::npos) {
            return JsonResult::success();
        }

        // Step to next enum value.
        dev.press(g.enumNext, holdMs);
        dev.step();
    }

    dev.readScreen(200, 300);
    return JsonResult::fail("editValue: could not find enum '" + targetValue + "'",
                            dev.grid());
}

JsonResult enterNote(M8Device& dev, const std::string& noteName,
                     uint8_t velocity, int holdMs) {
    auto& g = getGestures();
    if (!g.isReady() || g.noteEnter == 0) {
        return JsonResult::fail(
            "enterNote: note entry gesture not pinned — Tier 2 hardware pinning required.",
            dev.grid());
    }

    // Clear cell if target is empty.
    if (noteName == "---" || noteName.empty()) {
        dev.press(g.clearCell, holdMs);
        dev.step();
        return JsonResult::success();
    }

    // Parse the target note: "C-4" → octave=4, semitone=0; "F#5" → octave=5, semitone=6.
    static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int targetOctave = -1, targetSemitone = -1;
    for (int i = 0; i < 12; ++i) {
        std::string nn = kNoteNames[i];
        if (noteName.size() >= 2 && noteName.substr(0, nn.size()) == nn) {
            targetSemitone = i;
            // Parse octave: the character after the note name.
            size_t oPos = nn.size();
            if (oPos < noteName.size() && noteName[oPos] == '-') oPos++;
            if (oPos < noteName.size()) {
                targetOctave = noteName[oPos] - '0';
            }
            break;
        }
    }
    if (targetOctave < 0 || targetSemitone < 0) {
        return JsonResult::fail("enterNote: cannot parse note '" + noteName + "'",
                                dev.grid());
    }

    // Insert default note (C-4) first.
    dev.press(g.noteEnter, holdMs);  // EDIT = insert default
    dev.step();

    // Compute delta from C-4 (octave 4, semitone 0) to target.
    int currentTotal = 4 * 12 + 0;  // C-4
    int targetTotal = targetOctave * 12 + targetSemitone;
    int delta = targetTotal - currentTotal;

    if (delta == 0) return JsonResult::success();

    // Apply delta using octave and semitone gestures.
    int octaveSteps = delta / 12;
    int semitoneRemainder = ((delta % 12) + 12) % 12;  // always positive

    // First adjust octave.
    if (octaveSteps > 0) {
        for (int i = 0; i < octaveSteps; ++i) {
            dev.press(g.valueInc16, holdMs);  // EDIT+UP = octave up
            dev.step();
        }
    } else if (octaveSteps < 0) {
        for (int i = 0; i < -octaveSteps; ++i) {
            dev.press(g.valueDec16, holdMs);  // EDIT+DOWN = octave down
            dev.step();
        }
    }

    // Then adjust remaining semitones.
    if (semitoneRemainder > 0) {
        // Check if going up or down is shorter.
        if (semitoneRemainder <= 6) {
            // Go up.
            for (int i = 0; i < semitoneRemainder; ++i) {
                dev.press(g.valueInc, holdMs);  // EDIT+RIGHT = +1 semitone
                dev.step();
            }
        } else {
            // Go down (shorter path).
            for (int i = 0; i < 12 - semitoneRemainder; ++i) {
                dev.press(g.valueDec, holdMs);  // EDIT+LEFT = -1 semitone
                dev.step();
            }
        }
    }

    // Verify the result.
    dev.readScreen(200, 300);
    std::string txt = dev.grid().cursorMainText();
    while (!txt.empty() && txt.back() == '\n') txt.pop_back();
    if (txt.find(noteName) != std::string::npos) {
        return JsonResult::success();
    }

    return JsonResult::fail("enterNote: verification failed — expected '" + noteName
                            + "' but got '" + txt + "'", dev.grid());
}

JsonResult clearCell(M8Device& dev, int holdMs) {
    auto& g = getGestures();
    if (!g.isReady() || g.clearCell == 0) {
        return JsonResult::fail(
            "clearCell: clear gesture not pinned — Tier 2 hardware pinning required.",
            dev.grid());
    }
    dev.press(g.clearCell, holdMs);
    dev.step();
    return JsonResult::success();
}

// ---- loadFile --------------------------------------------------------------

int loadFile(M8Device& dev, const std::string& target, int holdMs) {
    const std::string want = alnumUpper(target);

    auto header = [&]{ return toUpper(dev.grid().topHeader()); };
    auto inBrowser = [&]{ return header().find("LOADPROJECT") != std::string::npos; };
    auto onProjSettings = [&]{
        std::string h = header();
        return h.find("PROJECT") != std::string::npos && h.find("LOAD") == std::string::npos;
    };

    // Helper: find the pixel Y of a row containing both "LOAD" and "PROJECT"
    // in mainRows() (not cursorMainText, which is accent-cells-only and can
    // miss the full row text during transitions).
    auto findLoadProjectRowY = [&]() -> int {
        auto allRows = dev.grid().mainRows();
        for (auto& [y, text] : allRows) {
            std::string t = toUpper(text);
            if (t.find("LOAD") != std::string::npos &&
                t.find("PROJECT") != std::string::npos) {
                return y;
            }
        }
        return -1;
    };

    // 1-3) Reach the LOAD PROJECT browser.
    if (!inBrowser()) {
        // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): this used to climb
        // toward PROJECT with a blind, fixed "press SHIFT+UP up to 8 times"
        // loop -- a second, less-reliable navigation mechanism duplicating what
        // gotoScreen already does correctly via computeRoute's real grid
        // routing. That duplication was a real bug, not just redundancy: SHIFT+UP
        // from INSTRUMENT can land on INST_POOL rather than climbing toward
        // PROJECT, and the loop has no way to recover from an unexpected
        // intermediate screen the way gotoScreen's hop-by-hop route does.
        // Reproduced directly: `loadFile` invoked while sitting on INSTRUMENT
        // failed with "could not reach PROJECT screen (header=INSTRUMENTPOOL)".
        // Reuse gotoScreen (36/36 hop-verified, §6.5.1) instead of a parallel
        // implementation.
        bool onProject = onProjSettings();
        if (!onProject) {
            auto navRes = gotoScreen(dev, Screen::PROJECT, holdMs);
            onProject = navRes.ok;
        }
        if (!onProject) {
            std::printf("loadFile: could not reach PROJECT screen (header=\"%s\")\n",
                        dev.grid().topHeader().c_str());
            return 10;
        }

        auto targetField = findFieldOnScreen(Screen::PROJECT, "PROJECT");
        if (!targetField) return 11;

        bool onLoad = false;
        for (int i = 0; i < 30; ++i) {
            // Check if cursor is already on the LOAD/PROJECT row.
            // Use mainRows() for the target row search (not cursorMainText),
            // and cursorMainText() for the cursor arrival check, but do a
            // confirm-read to avoid transitional frame false positives.
            int targetPixelY = findLoadProjectRowY();
            if (targetPixelY < 0) {
                // Row not visible yet — keep scrolling down.
                dev.press(Key::DOWN, holdMs);
                if (!confirmRead(dev)) dev.readScreen();
                continue;
            }

            // Check cursor arrival by comparing cursor row Y to target row Y.
            int curY = dev.grid().cursorRowY();
            if (curY == targetPixelY) {
                // Cursor is on the right row — confirm it's stable.
                if (confirmRead(dev, Screen::PROJECT)) {
                    // Double-check the row text contains LOAD + PROJECT.
                    int recheckY = findLoadProjectRowY();
                    if (recheckY == targetPixelY) {
                        onLoad = true;
                        break;
                    }
                }
            }

            // Navigate toward the target row.
            if (curY < 0)            dev.press(Key::DOWN, holdMs);
            else if (curY < targetPixelY) dev.press(Key::DOWN, holdMs);
            else if (curY > targetPixelY) dev.press(Key::UP, holdMs);
            else                          dev.press(Key::LEFT, holdMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!confirmRead(dev)) dev.readScreen();
        }
        if (!onLoad) return 11;

        // Press EDIT to open the LOAD PROJECT browser.
        dev.press(Key::EDIT, holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        // Use dismissModal in case a confirmation modal appears (e.g. "LOSE CHANGES?").
        if (!confirmRead(dev)) dev.readScreen();
        if (isModal(dev.grid())) {
            auto mr = dismissModal(dev, true, holdMs);
            if (!mr.ok) {
                // Modal didn't resolve with EDIT — try OPT (cancel) and re-navigate.
                dismissModal(dev, false, holdMs);
                return 11;
            }
        }
    }

    if (!inBrowser()) {
        // One more confirm to handle transitional frames.
        if (!confirmRead(dev)) dev.readScreen();
        if (!inBrowser()) return 12;
    }

    // Scroll to the top of the file list before searching. Hardware-
    // confirmed (2026-07-18, real M8 fw 6.5.2): the search loop below only
    // knows how to scroll DOWN toward a target once that target's row has
    // scrolled OUT of the decoded viewport (targetY < 0 always presses
    // DOWN, regardless of which direction the target actually is) -- so a
    // target ABOVE the current cursor position (e.g. selecting the first
    // file in the list while the browser opened scrolled near the bottom,
    // left over from a previous manual browse) can never be reached; DOWN
    // at the bottom of the list does nothing and the loop exhausts. Starting
    // from a known top-of-list position makes every target reachable via
    // DOWN-only scrolling, which the existing loop already handles.
    for (int i = 0; i < 20; ++i) {
        std::string before = dev.grid().cursorMainText();
        dev.press(Key::UP, holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (!confirmRead(dev)) dev.readScreen();
        if (dev.grid().cursorMainText() == before) break;  // reached the top
    }

    // 4) Select target file.
    for (int i = 0; i < 40; ++i) {
        std::string sel = dev.grid().cursorMainText();
        if (alnumUpper(sel).find(want) != std::string::npos) {
            dev.press(Key::EDIT, holdMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (!confirmRead(dev)) dev.readScreen();
            if (isModal(dev.grid())) {
                dismissModal(dev, true, holdMs);
            }
            auto result = gotoScreen(dev, Screen::SONG, holdMs);
            (void)result;
            return 0;
        }
        auto rows = dev.rows();
        int targetY = -1;
        for (auto& [y, s] : rows)
            if (alnumUpper(s).find(want) != std::string::npos) { targetY = y; break; }
        int curY = dev.grid().cursorRowY();
        if (targetY < 0)         dev.press(Key::DOWN, holdMs);
        else if (curY < 0)       dev.press(Key::DOWN, holdMs);
        else if (targetY > curY) dev.press(Key::DOWN, holdMs);
        else if (targetY < curY) dev.press(Key::UP, holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!confirmRead(dev)) dev.readScreen();
    }
    return 14;
}

} // namespace dev
} // namespace m8
