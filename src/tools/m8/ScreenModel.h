#pragma once

// ===========================================================================
// ScreenModel.h — screen identity, navigation graph, and per-screen field maps.
//
// Tier 1 of M8_DEVICE_CONTROL_SPEC.md. Maps the M8's 2D screen grid into
// data the driver can use to navigate, identify screens, and locate fields.
// Source of truth: the clone's ViewManager + per-screen ScreenLayout.h files.
// ===========================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "M8Device.h"

namespace m8 {
namespace dev {

// ---- Screen identity ------------------------------------------------------

enum class Screen : uint8_t {
    SONG, CHAIN, PHRASE, INSTRUMENT, TABLE,
    PROJECT, GROOVE, SCALE, MODS, INST_POOL,
    MIXER, EFFECTS,
    LOAD_PROJECT_MODAL,    // LOAD PROJECT browser (distinct from PROJECT settings)
    FILE_BROWSER,          // sample / song file browser modal
    UNKNOWN
};

// Canonical header strings for each screen (topHeader(), alnum+upper).
// These are the M8's actual header texts as seen on the display.
struct ScreenInfo {
    Screen id;
    const char* canonHeader;   // unique canonical header (alnum+upper)
    int gx, gy;                // grid position (from ViewManager)
};

// The full screen table. Look up by canon header substring.
inline const ScreenInfo kScreenTable[] = {
    {Screen::SONG,          "SONG",              0, 0},
    {Screen::CHAIN,         "CHAIN",             1, 0},
    {Screen::PHRASE,        "PHRASE",            2, 0},
    {Screen::INSTRUMENT,    "INST.",             3, 0},  // header is "INST." on device
    {Screen::TABLE,         "TABLE",             4, 0},
    {Screen::PROJECT,       "PROJECT",           0, 1},  // settings (no LOAD in header)
    {Screen::GROOVE,        "GROOVE",            2, 1},
    {Screen::MODS,          "MOD",               3, 1},  // header starts with "MOD"
    {Screen::SCALE,         "SCALE",             2, 2},
    {Screen::INST_POOL,     "INST.POOL",         3, 2},
    {Screen::MIXER,         "MIXER",             0, -1},
    {Screen::EFFECTS,       "EFFECTS",           0, -2},
    {Screen::LOAD_PROJECT_MODAL, "LOADPROJECT",  0, 1},  // browser (has LOAD in header)
};

// Strip trailing digits from a canonical header to get the base screen name.
// "PHRASE01" → "PHRASE", "INST.01" → "INST.", "MODS01" → "MODS".
inline std::string stripDigits(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && s[end - 1] >= '0' && s[end - 1] <= '9') --end;
    return s.substr(0, end);
}

// Identify the current screen from the canonical header string.
// Handles the PROJECT/LOADPROJECT ambiguity by checking for "LOAD".
inline Screen identifyScreen(const std::string& canon) {
    // Special case: PROJECT settings vs LOAD PROJECT browser.
    if (canon.find("LOADPROJECT") != std::string::npos)
        return Screen::LOAD_PROJECT_MODAL;
    if (canon.find("LOSECHANGES") != std::string::npos || canon.find("SONG?") != std::string::npos)
        return Screen::LOAD_PROJECT_MODAL;  // confirmation modal

    // Strip trailing digits so "PHRASE01" matches "PHRASE", etc.
    std::string base = stripDigits(canon);

    for (auto& si : kScreenTable) {
        if (si.canonHeader == base) return si.id;
    }
    // Partial matching for screens whose headers may vary.
    if (base.find("POOL") != std::string::npos)
        return Screen::INST_POOL;
    if (base.find("INST") != std::string::npos && base.find("POOL") == std::string::npos
        && base.find("MOD") == std::string::npos)
        return Screen::INSTRUMENT;
    if (base.find("MOD") != std::string::npos && base.find("MODS") != std::string::npos)
        return Screen::MODS;
    if (base.find("EFFECT") != std::string::npos)
        return Screen::EFFECTS;
    if (base.find("GROOV") != std::string::npos)
        return Screen::GROOVE;
    // Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): stripDigits only
    // strips trailing DECIMAL digits, but phrase/chain/table IDs are HEX
    // (00-FF) and can end in a letter -- "PHRASE FC" canons to "PHRASEFC",
    // which stripDigits leaves untouched, so the exact-match loop above
    // never matches "PHRASE". Prefix-match as a fallback, the same way
    // INST/MOD/EFFECTS already do above. Must be startswith, not a bare
    // substring find, or e.g. "CHAINABCDEF" could spuriously match "PHRASE"
    // wherever "PHRASE" happened to appear as a substring (it can't here,
    // but the discipline matters as more screens gain hex-suffixed IDs).
    if (base.rfind("CHAIN", 0) == 0)  return Screen::CHAIN;
    if (base.rfind("PHRASE", 0) == 0) return Screen::PHRASE;
    if (base.rfind("TABLE", 0) == 0)  return Screen::TABLE;
    return Screen::UNKNOWN;
}

inline Screen identifyScreen(const ScreenGrid& grid) {
    std::string canon = grid.canon();
    Screen s = identifyScreen(canon);
    if (s == Screen::LOAD_PROJECT_MODAL) {
        // Disambiguate from PROJECT settings screen with LOAD highlighted.
        bool hasTempo = false;
        for (auto& [y, text] : grid.mainRows()) {
            if (text.find("TEMPO") != std::string::npos) {
                hasTempo = true;
                break;
            }
        }
        if (hasTempo) return Screen::PROJECT;
    }
    return s;
}

// ---- Navigation graph -----------------------------------------------------
//
// The M8 UI is a 2D grid. SHIFT+arrows move between screens; arrows move
// the cursor within one screen. This mirrors ViewManager::getViewAt().
// Positions: x = 0..4 (Song/Chain/Phrase/Inst/Table), y = -2..2.

struct NavEdge {
    Screen target;
    int dx, dy;   // signed grid delta to reach target from current position
};

// Get the grid position of a screen.
inline bool gridPos(Screen s, int& gx, int& gy) {
    for (auto& si : kScreenTable) {
        if (si.id == s) { gx = si.gx; gy = si.gy; return true; }
    }
    return false;
}

// Map a grid position to a screen (some positions are shared).
inline Screen screenAtGrid(int gx, int gy) {
    if (gx < 0 || gx > 4) return Screen::UNKNOWN;
    if (gy == -1) return Screen::MIXER;
    if (gy == -2) return Screen::EFFECTS;
    if (gy < -2) return Screen::UNKNOWN;
    if (gy == 0) {
        switch (gx) {
            case 0: return Screen::SONG;
            case 1: return Screen::CHAIN;
            case 2: return Screen::PHRASE;
            case 3: return Screen::INSTRUMENT;
            case 4: return Screen::TABLE;
        }
    }
    if (gy == 1) {
        if (gx == 0 || gx == 1) return Screen::PROJECT;
        if (gx == 2) return Screen::GROOVE;
        if (gx == 3 || gx == 4) return Screen::MODS;
    }
    if (gy == 2) {
        if (gx == 2) return Screen::SCALE;
        if (gx == 3) return Screen::INST_POOL;
    }
    return Screen::UNKNOWN;
}

// Compute the route from `from` to `to` as a sequence of grid moves.
// Returns the sequence of (shift+arrow) key masks to press.
// Each move is one SHIFT+direction press. The caller should re-identify
// after each hop (never assume the hop landed).
struct NavStep {
    uint8_t keyMask;   // SHIFT ORed with the direction
    Screen viaScreen;  // expected screen after this step (for verification)
};

inline std::vector<NavStep> computeRoute(Screen from, Screen to) {
    std::vector<NavStep> steps;
    if (from == to) return steps;

    int fromX, fromY, toX, toY;
    if (!gridPos(from, fromX, fromY) || !gridPos(to, toX, toY))
        return steps;  // can't compute route for unknown screens

    int curX = fromX, curY = fromY;

    // 1. If we are not at y = 0, and we need to change X, we must first drop to y = 0.
    if (curY != 0 && curX != toX) {
        while (curY != 0) {
            uint8_t dir = (0 > curY) ? Key::UP : Key::DOWN;
            curY += (0 > curY) ? 1 : -1;
            Screen via = screenAtGrid(curX, curY);
            steps.push_back({static_cast<uint8_t>(Key::SHIFT | dir), via});
        }
    }

    // 2. Move horizontally at y = 0.
    while (curX != toX) {
        uint8_t dir = (toX > curX) ? Key::RIGHT : Key::LEFT;
        curX += (toX > curX) ? 1 : -1;
        Screen via = screenAtGrid(curX, curY);
        steps.push_back({static_cast<uint8_t>(Key::SHIFT | dir), via});
    }

    // 3. Move vertically to target Y.
    while (curY != toY) {
        uint8_t dir = (toY > curY) ? Key::UP : Key::DOWN;
        curY += (toY > curY) ? 1 : -1;
        Screen via = screenAtGrid(curX, curY);
        steps.push_back({static_cast<uint8_t>(Key::SHIFT | dir), via});
    }

    return steps;
}

// ---- Per-screen field maps ------------------------------------------------
//
// Each screen has a set of addressable fields. For form-style screens (those
// with CursorId + NavNode), the fields have fixed names and positions. For
// grid-style screens, the fields are dynamic (track columns, phrase rows).
//
// FieldRef names are UPPER_CASE canonical names that can be matched against
// the on-screen label text.

struct FieldInfo {
    const char* name;     // canonical name, e.g. "TEMPO", "CUTOFF"
    const char* label;    // on-screen label text to match
    int col;              // text grid column of the label
    int row;              // text grid row
};

// Project screen fields (from ProjectScreenLayout.h).
inline const FieldInfo kProjectFields[] = {
    {"TEMPO",     "TEMPO",       0, 2},
    {"TRANSPOSE", "TRANSPOSE",   0, 3},
    {"GROOVE",    "GROOVE",      0, 4},
    {"SCALE",     "SCALE",       0, 5},
    {"QUANTIZE",  "LIVE QUANTIZE", 0, 6},
    {"MIDI",      "MIDI",        0, 8},
    {"NAME",      "NAME",        0, 10},
    {"PROJECT",   "PROJECT",     0, 11},  // LOAD/SAVE/NEW row
    {"EXPORT",    "EXPORT/SHARE", 0, 12},
    {"CLEAR",     "CLEAR UNUSED", 0, 13},
    {"INST.POOL", "INST. POOL",  0, 14},
    {"TIME",      "TIME STATS",  0, 16},
    {"SYSTEM",    "SYSTEM",      0, 17},
};

// SAMPLEROOT removed (2026-07-18, real M8 fw 6.5.2) after an exhaustive,
// hardware-verified search found no reachable field matching it. This
// project's own UI clone (src/ui/screens/project/ProjectScreenLayout.h)
// models "SAMPLE ROOT" as a normal sibling row, DOWN-reachable from
// SYSTEM_SETTINGS on the PROJECT screen itself -- which is where this field
// map entry's row=18 originally came from. Real hardware disagrees:
//   - SYSTEM (row 17) is the LAST row PROJECT's own DOWN navigation reaches.
//     Confirmed directly and repeatedly: full-screen dumps before/after a
//     DOWN press from SYSTEM are byte-identical (TEMPO still visible at the
//     top, cursor still on SYSTEM) -- no cursor movement AND no viewport
//     scroll, tested with both a short tap and a 400ms hold (ruling out a
//     scroll-acceleration explanation).
//   - Pressing EDIT on SYSTEM opens a genuinely different screen (header
//     "SYSTEM SETTINGS": BACKLIGHT, FONT OPTIONS, THEME, NOTE PREVIEW,
//     REC.METRONOME, METRONOME VOL, SONG TEMPLATE, USB AUDIO MODE, USB MAIN
//     OUT, LINE-IN GATE, SPLASH SCREEN, HP PROTECTION, KEY DELAY:REP,
//     BATT.STATUS) -- not a same-screen row reveal, and this submenu has no
//     sample-root field either.
//   - Also checked the MIDI submenu (SYNC IN/OUT, CTRL SURFACE, track MIDI
//     input mapping, DEFAULTS) and the LOAD PROJECT browser's OPT-key
//     behavior (just cancels the browser, no context menu appears) -- no
//     sample-root field in either.
// Net finding: the clone's own SAMPLE_ROOT nav-graph entry does not match
// this device's real behavior on fw 6.5.2 -- whether that's a clone bug, a
// firmware version difference, or something else is a question for the
// clone's own UI code, not this driver. A phantom field map entry that can
// never be reached serves no purpose here; if a real "Sample Root" setting
// turns up later, re-add it with its actual verified position. See
// M8_DEVICE_CONTROL_SPEC.md §6.5.2.

// Instrument screen fields — Sampler layout (from InstrumentSamplerLayout.h).
// Two-column form: left column (type/sample/params) and right column (amp/pan/sends).
inline const FieldInfo kInstrumentSamplerFields[] = {
    {"TYPE",      "TYPE",       0, 2},
    {"NAME",      "NAME",       0, 3},
    {"TRANSP",    "TRANSP.",    0, 4},
    {"TBL_TIC",   "TBL.TIC",   13, 4},
    {"EQ",        "EQ",         26, 4},
    {"SAMPLE",    "SAMPLE",     0, 6},
    {"SLICE",     "SLICE",      0, 8},
    {"AMP",       "AMP",        17, 8},
    {"PLAY",      "PLAY",       0, 9},
    {"LIM",       "LIM",        17, 9},
    {"START",     "START",      0, 10},
    {"PAN",       "PAN",        17, 10},
    {"LOOP_ST",   "LOOP ST",    0, 11},
    {"DRY",       "DRY",        17, 11},
    {"LENGTH",    "LENGTH",     0, 12},
    {"CHO",       "MFX",        17, 12},
    {"DETUNE",    "DETUNE",     0, 13},
    {"DEL",       "DEL",        17, 13},
    {"DEGRADE",   "DEGRADE",    0, 14},
    {"REV",       "REV",        17, 14},
    {"FILTER",    "FILTER",     0, 15},
    {"CUTOFF",    "CUTOFF",     0, 16},
    {"RES",       "RES",        0, 17},
};

// Instrument screen fields — Macrosyn layout (from InstrumentMacrosynLayout.h).
inline const FieldInfo kInstrumentMacrosynFields[] = {
    {"TYPE",      "TYPE",       0, 2},
    {"NAME",      "NAME",       0, 3},
    {"TRANSP",    "TRANSP.",    0, 4},
    {"TBL_TIC",   "TBL.TIC",   13, 4},
    {"EQ",        "EQ",         26, 4},
    {"SHAPE",     "SHAPE",      0, 6},
    {"TIMBRE",    "TIMBRE",     0, 8},
    {"COLOR",     "COLOR",      0, 9},
    {"DEGRADE",   "DEGRADE",    0, 10},
    {"REDUX",     "REDUX",      0, 11},
    {"FILTER",    "FILTER",     0, 12},
    {"CUTOFF",    "CUTOFF",     0, 13},
    {"RES",       "RES",        0, 14},
    {"AMP",       "AMP",        17, 8},
    {"LIM",       "LIM",        17, 9},
    {"PAN",       "PAN",        17, 10},
    {"DRY",       "DRY",        17, 11},
    {"CHO",       "CHO",        17, 12},
    {"DEL",       "DEL",        17, 13},
    {"REV",       "REV",        17, 14},
};

// Effects screen fields (from EffectsScreenLayout.h).
// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): every row below was off
// by exactly one from the real on-device layout, and every column was 0
// instead of 8. Root cause: the map was missing the CHORUS section's "MOD
// TYPE" row (row 2, the very first editable field, above INPUT EQ) entirely,
// so CHO_EQ ended up bound to MOD TYPE's row instead of INPUT EQ's, and
// every later field inherited the same +1 offset -- which happened to still
// "work" (each entry landed on SOME real, distinct field, just mislabeled)
// until the offset crossed the blank gap row separating the CHORUS and DELAY
// sections, at which point DEL_EQ's row pointed at the empty gap instead of
// DELAY's real INPUT EQ row and moveCursorTo could never reach it. Rows/cols
// below are re-measured directly from pixel data.
inline const FieldInfo kEffectsFields[] = {
    {"CHO_EQ",       "INPUT EQ",    8, 3},
    {"CHO_MOD_DEP",  "MOD DEPTH",   8, 4},
    {"CHO_WID",      "STEREO WIDTH", 8, 5},
    {"CHO_REV",      "REVERB SEND", 8, 6},
    {"DEL_EQ",       "INPUT EQ",    8, 8},
    {"DEL_TIME_L",   "TIME L",      8, 9},
    {"DEL_FBK",      "FEEDBACK",    8, 10},
    {"DEL_WID",      "STEREO WIDTH", 8, 11},
    {"DEL_REV",      "REVERB SEND", 8, 12},
    {"REV_EQ",       "INPUT EQ",    8, 14},
    {"REV_SIZE",     "ROOM SIZE",   8, 15},
    {"REV_DEC",      "DECAY",       8, 16},
    {"REV_MOD_DEP",  "MOD DEPTH",   8, 17},
    {"REV_WID",      "STEREO WIDTH", 8, 18},
};

// Mixer screen fields (from MixerScreenLayout.h).
//
// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): the row/col values below
// were re-measured directly from pixel data after the original coordinates
// (row 8-12 for everything past OUT_VOL) turned out not to match this
// firmware's actual layout at all -- the on-device labels are "MX"/"DE"/"RE"
// (a vertical column at col 10, rows 16-18), not "CH"/"DE"/"RE" at row 8, and
// "INPUT"/"USB" live at row 15 (col 13/19), not row 12. DJF_RES's on-device
// label reads "OTT", not "RES" -- the field name is kept as-is (it's a
// stable identifier, not required to match the on-screen text) but the
// mismatch is worth knowing if a future firmware relabels it back.
// DJF_TYP's on-device position could NOT be located within the visible main
// area (x < MAIN_X_MAX) in this investigation -- its coordinates are left
// unchanged from the original (unverified) guess. See
// M8_DEVICE_CONTROL_SPEC.md §6.5 for the full writeup.
inline const FieldInfo kMixerFields[] = {
    {"OUT_VOL",  "OUTPUT VOL", 1, 2},
    {"MST_CHO",  "MX",         10, 16},
    {"MST_DEL",  "DE",         10, 17},
    {"MST_REV",  "RE",         10, 18},
    {"IN_VOL",   "INPUT",      13, 15},
    {"USB_VOL",  "USB",        19, 15},
    {"MIX_VOL",  "MIX",        24, 14},
    {"LIM_VAL",  "LIM",        24, 15},
    {"DJF_FREQ", "DJF",        24, 16},
    {"DJF_RES",  "OTT",        24, 17},
    {"DJF_TYP",  "TYP",        34, 12},  // UNVERIFIED — not found on-device, see comment above
};

// Scale screen fields (from ScaleScreenLayout.h).
// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): LOAD/SAVE's columns were
// re-measured from pixel data -- LOAD's real label starts at col 7, not 0;
// SAVE's at col 12, not 5.
inline const FieldInfo kScaleFields[] = {
    {"TUNE",     "TUNE",   1, 16},
    {"NAME",     "NAME",   1, 17},
    {"LOAD",     "LOAD",   7, 18},
    {"SAVE",     "SAVE",   12, 18},
};

// Groove, Table, Song, Chain, Phrase screens are grid-style (raw cursor_x/cursor_y).
// No static field maps — use the grid model instead.

// ---- Instrument type extraction from framebuffer ---------------------------
//
// The TYPE field on the Instrument screen shows "SAMPLER" or "MACROSYN" as
// on-screen text. These helpers extract it from the grid without adding any
// device-I/O dependency to ScreenModel.h.

inline std::string readInstrumentType(const ScreenGrid& grid) {
    // Strategy 1: If the cursor is on a row, check that row for the TYPE value.
    for (auto& [pos, c] : grid.cells) {
        if (c.ch == 0x3C) {  // '<' cursor marker
            std::map<int, char> rowChars;
            for (auto& [p, cell] : grid.cells) {
                if (p.first == pos.first && p.second < ScreenGrid::MAIN_X_MAX) {
                    char g = (cell.ch >= 32 && cell.ch < 127)
                             ? static_cast<char>(cell.ch) : ' ';
                    rowChars[p.second] = g;
                }
            }
            std::string rowText;
            for (auto& [x, ch] : rowChars) rowText += ch;
            std::string upper = rowText;
            for (auto& ch : upper)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (upper.find("SAMPLE") != std::string::npos) return "SAMPLER";
            if (upper.find("MACROSYN") != std::string::npos) return "MACROSYN";
            if (upper.find("FM") != std::string::npos) return "FM";
            if (upper.find("WAV") != std::string::npos) return "WAV";
            if (upper.find("HYPER") != std::string::npos) return "HYPER";
            break;
        }
    }
    // Strategy 2: Scan all rows for "TYPE" label; the instrument type is typically
    // on the same row (value to the right) or the row immediately below.
    std::map<int, std::map<int, char>> rowChars;
    for (auto& [pos, c] : grid.cells) {
        if (pos.second < ScreenGrid::MAIN_X_MAX) {
            char g = (c.ch >= 32 && c.ch < 127) ? static_cast<char>(c.ch) : ' ';
            rowChars[pos.first][pos.second] = g;
        }
    }
    for (auto& [y, cols] : rowChars) {
        std::string rowText;
        for (auto& [x, g] : cols) rowText += g;
        std::string upper = rowText;
        for (auto& ch : upper)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (upper.find("TYPE") != std::string::npos) {
            if (upper.find("SAMPLE") != std::string::npos) return "SAMPLER";
            if (upper.find("MACROSYN") != std::string::npos) return "MACROSYN";
            if (upper.find("FM") != std::string::npos) return "FM";
            if (upper.find("WAV") != std::string::npos) return "WAV";
            if (upper.find("HYPER") != std::string::npos) return "HYPER";
        }
    }
    return "SAMPLER";  // default assumption if TYPE is unreadable
}

// ---- Field map lookup by screen -------------------------------------------

struct ScreenFieldMap {
    const FieldInfo* fields;
    size_t count;
    bool isGrid;   // true = grid-style screen, fields are dynamic
};

inline ScreenFieldMap getFieldMap(Screen s) {
    switch (s) {
        case Screen::PROJECT:    return {kProjectFields, std::size(kProjectFields), false};
        case Screen::INSTRUMENT: return {kInstrumentSamplerFields, std::size(kInstrumentSamplerFields), false};
        case Screen::EFFECTS:    return {kEffectsFields, std::size(kEffectsFields), false};
        case Screen::MIXER:      return {kMixerFields, std::size(kMixerFields), false};
        case Screen::SCALE:      return {kScaleFields, std::size(kScaleFields), false};
        // Grid-style screens:
        case Screen::SONG:       return {nullptr, 0, true};
        case Screen::CHAIN:      return {nullptr, 0, true};
        case Screen::PHRASE:     return {nullptr, 0, true};
        case Screen::TABLE:      return {nullptr, 0, true};
        case Screen::GROOVE:     return {nullptr, 0, true};
        case Screen::INST_POOL:  return {nullptr, 0, true};
        case Screen::MODS:       return {nullptr, 0, false};  // dynamic NavNode, treat as form
        default:                 return {nullptr, 0, false};
    }
}

// Type-aware overload for INSTRUMENT: uses typeHint to select Sampler vs
// MacroSynth field map. Falls back to Sampler for unknown types.
inline ScreenFieldMap getFieldMap(Screen s, const std::string& typeHint) {
    if (s == Screen::INSTRUMENT) {
        std::string upper = typeHint;
        for (auto& c : upper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper.find("MACROSYN") != std::string::npos)
            return {kInstrumentMacrosynFields, std::size(kInstrumentMacrosynFields), false};
        return {kInstrumentSamplerFields, std::size(kInstrumentSamplerFields), false};
    }
    return getFieldMap(s);
}

// Look up a field by name in a screen's field map.
// Matching priority: exact > prefix (field starts with name) > suffix (field
// ends with name) > substring. This prevents short names like "AMP" from
// falsely matching longer names like "SAMPLE" (which contains "AMP" as a
// substring). The first match at the highest priority level is returned.
inline std::optional<FieldRef> findFieldOnScreen(Screen s, const std::string& name) {
    auto map = getFieldMap(s);
    if (map.isGrid || !map.fields) return std::nullopt;
    std::string upper = name;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::optional<FieldRef> prefixMatch, suffixMatch, substringMatch;
    for (size_t i = 0; i < map.count; ++i) {
        std::string fn = map.fields[i].name;
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fn == upper)
            return FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!prefixMatch && fn.find(upper) == 0)
            prefixMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!suffixMatch && upper.size() >= 3 && fn.size() >= 3
            && fn.size() > upper.size()
            && fn.substr(fn.size() - upper.size()) == upper)
            suffixMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!substringMatch
            && (fn.find(upper) != std::string::npos || upper.find(fn) != std::string::npos))
            substringMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
    }
    if (prefixMatch) return prefixMatch;
    if (suffixMatch) return suffixMatch;
    return substringMatch;
}

// Type-aware overload: uses typeHint to select the correct instrument field map.
inline std::optional<FieldRef> findFieldOnScreen(Screen s, const std::string& name,
                                                  const std::string& typeHint) {
    auto map = getFieldMap(s, typeHint);
    if (map.isGrid || !map.fields) return std::nullopt;
    std::string upper = name;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::optional<FieldRef> prefixMatch, suffixMatch, substringMatch;
    for (size_t i = 0; i < map.count; ++i) {
        std::string fn = map.fields[i].name;
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fn == upper)
            return FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!prefixMatch && fn.find(upper) == 0)
            prefixMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!suffixMatch && upper.size() >= 3 && fn.size() >= 3
            && fn.size() > upper.size()
            && fn.substr(fn.size() - upper.size()) == upper)
            suffixMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
        if (!substringMatch
            && (fn.find(upper) != std::string::npos || upper.find(fn) != std::string::npos))
            substringMatch = FieldRef{map.fields[i].name, map.fields[i].col, map.fields[i].row};
    }
    if (prefixMatch) return prefixMatch;
    if (suffixMatch) return suffixMatch;
    return substringMatch;
}

// Find which screen contains a field by name. Searches all form-style field maps.
//
// For the Instrument screen, this must check the UNION of the Sampler and
// MacroSynth field maps: at this point (before navigating anywhere) the
// framebuffer isn't readable yet, so the instrument's actual type is
// unknown. A type-blind check against only the Sampler map would reject
// MacroSynth-only fields (SHAPE/TIMBRE/COLOR/REDUX) as "not found on any
// screen" before ever reaching the Instrument screen — even though they are
// perfectly valid fields there. Screen *identity* doesn't need the type
// hint; only the exact on-screen *position* does, and that's resolved later
// by moveCursorTo's type-aware lookup once the driver has actually arrived
// and can read TYPE from the framebuffer.
// Case-insensitive exact-name lookup within a screen's field map (no
// prefix/suffix/substring fuzziness). Used by findScreenForField's first pass
// so an exact field name on one screen always wins over a fuzzy substring
// match on an earlier-checked screen.
inline bool hasExactField(Screen s, const std::string& upper, const std::string& typeHint = "") {
    auto map = typeHint.empty() ? getFieldMap(s) : getFieldMap(s, typeHint);
    if (map.isGrid || !map.fields) return false;
    for (size_t i = 0; i < map.count; ++i) {
        std::string fn = map.fields[i].name;
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fn == upper) return true;
    }
    return false;
}

// Hardware-confirmed (2026-07-18, real M8 fw 6.5.2): a naive single-pass
// scan using findFieldOnScreen's fuzzy (substring) matching sends field
// lookups to the wrong screen when a short field name on one screen is a
// substring of a longer, unrelated field name on an EARLIER-checked screen
// -- e.g. "MST_CHO" (Mixer) substring-matches "CHO" (Instrument/Sampler's
// chorus send), and since INSTRUMENT precedes MIXER in kScreenTable, a
// lookup for "MST_CHO" resolved to INSTRUMENT instead of MIXER, causing
// moveCursorTo to navigate to the wrong screen entirely (same collision hits
// MST_DEL/DEL and MST_REV/REV). Fix: always search for an EXACT match across
// every screen first; only fall back to fuzzy substring matching (still
// screen-by-screen, first hit wins) if no screen has an exact match anywhere.
inline Screen findScreenForField(const std::string& name) {
    std::string upper = name;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    for (auto& si : kScreenTable) {
        if (si.id == Screen::LOAD_PROJECT_MODAL || si.id == Screen::FILE_BROWSER) continue;
        if (si.id == Screen::INSTRUMENT) {
            if (hasExactField(si.id, upper, "SAMPLER") || hasExactField(si.id, upper, "MACROSYN"))
                return si.id;
            continue;
        }
        if (hasExactField(si.id, upper)) return si.id;
    }

    for (auto& si : kScreenTable) {
        if (si.id == Screen::LOAD_PROJECT_MODAL || si.id == Screen::FILE_BROWSER) continue;
        if (si.id == Screen::INSTRUMENT) {
            if (findFieldOnScreen(si.id, name, "SAMPLER").has_value() ||
                findFieldOnScreen(si.id, name, "MACROSYN").has_value())
                return si.id;
            continue;
        }
        if (findFieldOnScreen(si.id, name).has_value()) return si.id;
    }
    return Screen::UNKNOWN;
}

// ---- Grid screen helpers --------------------------------------------------

// Grid dimensions for grid-style screens.
struct GridDims {
    int rows, cols;
};

inline GridDims gridDims(Screen s) {
    switch (s) {
        case Screen::SONG:      return {16, 8};
        case Screen::CHAIN:     return {16, 2};
        case Screen::PHRASE:    return {16, 9};
        case Screen::TABLE:     return {16, 5};
        case Screen::GROOVE:    return {16, 1};
        case Screen::INST_POOL: return {16, 6};
        default:                return {0, 0};
    }
}

} // namespace dev
} // namespace m8
