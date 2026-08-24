#pragma once

// ===========================================================================
// FieldGuard — "has this row changed since the capture started?"
//
// Why this exists as its own header
// ---------------------------------
// It is the whole point of m8_watchcapture, and while it lived inside that
// tool's main() nothing could test it. A guard that has never been observed to
// fire is indistinguishable from one that cannot; this one has four failure
// modes that all look like success from the outside:
//
//   - too strict  -> rejects captures because cell accenting varied between
//                    two reads of a screen that never moved
//   - too strict  -> rejects captures because the playhead marker moved, which
//                    is the device working correctly
//   - too strict  -> rejects captures on a row caught mid-repaint
//   - too loose   -> misses the drift it exists to catch (#34)
//
// tests/test_field_guard.cpp pins all four.
// ===========================================================================

#include "M8Device.h"
#include "ScreenModel.h"

#include <cctype>
#include <string>

namespace m8 {
namespace dev {

// Alphanumerics only, upper-cased.
//
// Whitespace-insensitive because label spacing is not stable between reads:
// the same MIXER field has arrived as " OUTPUT VOL  F0" and as "OUTPUTVOLF0",
// since which cells carry the theme accent varies frame to frame. A comparison
// that respects spacing reports drift on a motionless screen.
//
// Punctuation-insensitive as a consequence, and that is load-bearing rather
// than incidental: it drops the playhead marker '>' from the row-label gutter,
// so a guarded row does not fire drift merely because the transport ran
// through it. Values are hex and note names -- alnum -- so nothing a
// measurement depends on is lost.
inline std::string canonRow(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (std::isalnum(c)) o += static_cast<char>(std::toupper(c));
    return o;
}

// The whole row a label sits on, not the parsed value.
//
// Deliberate. Conflating "the value" with "the row" was already a bug once
// (m8drv's `read` vs `read --row`), value extraction has to guess where a
// label ends, and the guard wants the stricter test regardless: #34 moved a
// *neighbouring* field, so a check scoped to the field being set would have
// missed it entirely.
inline std::string rowTextFor(const ScreenGrid& grid, const std::string& label,
                              Screen screen = Screen::UNKNOWN,
                              const std::string& typeHint = "") {
    // Prefer the FIELD MAP over a text search when the screen is known.
    //
    // ScreenGrid::findField matches a canonicalised SUBSTRING, and short labels
    // collide badly: watching "AMP" on the instrument screen matched the TYPE
    // row, because "TYPE SAMPLER LOAD SAVE" canonicalises to a string
    // containing "AMP" (inside SAMPLER). The baseline was then the wrong row
    // entirely. Same family as "BPM" matching "REP.BPM" on the PLAY row, which
    // cost a reset play mode during the sampler verification.
    //
    // The map gives the row outright, and after the crawl work those
    // coordinates are measured. Text search stays as the fallback for callers
    // that do not know the screen.
    if (screen != Screen::UNKNOWN) {
        if (const FieldInfo* info = findFieldInfo(screen, label, typeHint)) {
            const int wantY = fieldStopRow(*info);
            for (const auto& entry : grid.mainRows())
                if (entry.first / 10 - 3 == wantY) return entry.second;
        }
    }
    auto f = grid.findField(label);
    if (!f) return std::string();
    for (const auto& entry : grid.mainRows())
        if (entry.first == f->row) return entry.second;
    return std::string();
}

struct FieldWatch {
    std::string label;
    std::string baseline;     // canonRow() of the row when the capture started
    std::string sawInstead;   // canonRow() of the row when it first disagreed
    bool drifted = false;
};

// True if `now` is a real disagreement with the baseline.
//
// An empty reading is NOT drift. The live grid is sampled without waiting for
// quiet, so a row can be caught between the erase and the redraw; treating
// that as a changed field would abort captures at random. A row that has
// genuinely gone away still reads empty, so this trades a miss for the false
// positives -- the right way round, because the transport guard and the
// screen-identity check already cover "the device left the screen".
inline bool rowDrifted(const std::string& baseline, const std::string& now) {
    return !now.empty() && now != baseline;
}

// Sample one watch against a live grid. Returns true if this call detected
// drift. Sticky: once drifted, the first disagreement is what is reported.
inline bool sampleWatch(FieldWatch& w, const ScreenGrid& grid) {
    const std::string now = canonRow(rowTextFor(grid, w.label));
    if (!rowDrifted(w.baseline, now)) return false;
    if (!w.drifted) { w.drifted = true; w.sawInstead = now; }
    return true;
}

} // namespace dev
} // namespace m8
