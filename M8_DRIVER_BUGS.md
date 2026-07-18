# M8 Driver — Global Bug List

A single index of every real, hardware-confirmed bug found while building and hardening the
serial driver (`src/tools/m8/`), across all sessions. This is an index, not a duplicate — each
entry links to the fuller writeup in `M8_DEVICE_CONTROL_SPEC.md` where the hardware evidence,
symptom, and exact fix live. Status values: **FIXED** (in the codebase now), **REVERTED** (a fix
was built, hardware-confirmed unreliable, and deliberately removed rather than shipped),
**DATA** (not a logic bug — a field map had wrong/nonexistent coordinates, corrected), **OPEN**
(found, not fixed).

Numbering is chronological across sessions, not by severity.

---

## Driver bugs (in scope)

| # | Component | Status | Summary |
|---|---|---|---|
| 1 | `ScreenModel.h` — `findScreenForField` | FIXED | Type-blind for the Instrument screen: could only resolve field names against the Sampler map, so MacroSynth-only fields (SHAPE/TIMBRE/COLOR/REDUX) resolved to `Screen::UNKNOWN` before `moveCursorTo` ever got a chance to use the type-aware lookup. |
| 2 | `ScreenModel.h` — `identifyScreen` | FIXED | `stripDigits` only stripped trailing *decimal* digits, but phrase/chain/table IDs are hex (00-FF) and can end in a letter — `"PHRASE FC"` never matched `"PHRASE"`, so `gotoScreen` returned `UNKNOWN` for roughly half of all possible IDs. |
| 3 | `Primitives.cpp` — `moveCursorToGrid` | FIXED | Grid-screen cursor detection trusted a static, non-cursor `<` glyph over the real accent-cyan cursor color on PHRASE/TABLE, and a naive color scan (no row floor) could pick up the column-header label instead of the real cursor. |
| 4 | `Primitives.cpp` — `editValue` (numeric branch) | FIXED | Compared the raw target string (`"0x40"`) against on-screen text that only ever shows `"40"` — never matched even on genuine convergence. Fixed to parse both sides as hex and compare numerically. |
| 5 | `M8Device.cpp` — `ScreenGrid::cursorRowY()` | FIXED | The M8 skips redrawing a row's trailing blank cells when the cursor leaves it (a cyan space looks identical to a normal space), leaving stale cyan padding at the *vacated* row. The naive topmost-cyan-cell scan picked the ghost row over the real cursor's row. Fixed by requiring the picked cell be non-space. |
| 6 | `M8Device.cpp` — `M8Device::cursorField()` X-lookup | FIXED | Sibling bug to #5, not caught by the same fix: moving between two fields *on the same row* (e.g. EFFECTS' "MOD TYPE" → "INPUT EQ") left ghost cyan blanks at the vacated field's leading columns, and the X-position lookup picked the leftmost ghost instead of the real field. |
| 7 | `Primitives.cpp` — `moveCursorTo` (row-match branch) | FIXED | Always pressed RIGHT when on the correct row but wrong field. Real M8 multi-column rows (e.g. Sampler's SLICE/AMP pair) aren't reached column-by-column via UP/DOWN — DOWN from a full-width row (SAMPLE) lands directly on the row's *right*-column field (AMP), skipping the left-column field (SLICE) entirely. Fixed to compare target column vs. current column and press LEFT or RIGHT accordingly. |
| 8 | `ScreenModel.h` — `findScreenForField` | FIXED | Resolved short field names to the wrong screen via fuzzy substring matching — `"MST_CHO"` (Mixer) substring-matches `"CHO"` (Instrument/Sampler's chorus send), and since INSTRUMENT is checked before MIXER in the screen table, `"MST_CHO"` resolved to INSTRUMENT. Fixed to require an exact match across every screen first, falling back to fuzzy matching only if none exists. |
| 9 | `ScreenModel.h` — `kEffectsFields` | DATA | Every row was off by exactly one, with the wrong column (0 instead of 8). Root cause: the map was missing the CHORUS section's first row ("MOD TYPE"), so every field inherited a +1 offset — which "worked" by accident until the offset crossed the blank gap between the CHORUS and DELAY sections, at which point `DEL_EQ` became permanently unreachable. Re-measured all 14 rows/columns from pixel data. |
| 10 | `Primitives.cpp` — `moveCursorTo` (stuck-axis fallback) | FIXED | The row/column axis-fallback (added alongside #7) misfired on 1-pixel column measurement noise (e.g. `SYSTEM`'s label measures at col 1 even though its map entry says col 0), sending it into a useless LEFT/RIGHT loop instead of just continuing to press DOWN. Fixed with a >1-column tolerance. |
| 11 | `ScreenModel.h` — `kScaleFields` | DATA | `LOAD`/`SAVE` columns were wrong (0 and 5 instead of the real 7 and 12). |
| 12 | `Primitives.cpp` — `moveCursorTo` (stuck-axis fallback, latching) | FIXED | The axis-fallback from #10 was latched — once triggered it kept pressing LEFT/RIGHT forever, which could walk off the edge of a row's own sub-fields (SCALE's per-note EN/OFFSET columns) and then loop forever once that axis also stopped making progress, without ever giving the vertical direction another chance. Fixed to alternate (vertical, horizontal, vertical, ...) rather than latch. |
| 13 | `Primitives.cpp` — `readCursorValue` | FIXED | Never actually stripped the field's label from `cursorMainText()` despite its own doc comment claiming it did. On multi-column rows (e.g. `"AMP" + "FF"` → `"AMPFF"`), `editValue`'s numeric convergence check parsed the *leading* hex run — since `'A'` is a valid hex digit as well as a letter, it silently read "10" instead of "255" and could never match any real target, running all 256 steps and clamping at `0xFF`. Fixed by looking up the field's exact label from the field maps and stripping precisely that (a generic "skip leading letters" heuristic fails too — hex letters A-F are also alphabetic, e.g. `"TIMBRE"` ends in `E`). |
| 14 | `Primitives.cpp` — `editValue` (numeric stepping direction) | FIXED | The stepping loop only ever pressed `value_inc`, never `value_dec`. Values clamp at `0xFF` rather than wrapping, so a target below the current value was structurally unreachable. Fixed to pick direction from the parsed current-vs-target comparison each iteration. |
| 15 | `Primitives.cpp` — `loadFile` (climb to PROJECT) | FIXED | Used a separate, ad-hoc "press SHIFT+UP up to 8 times" loop instead of the already-hardened `gotoScreen`, duplicating a working mechanism with a less reliable one. SHIFT+UP from INSTRUMENT could land on INST_POOL instead of climbing toward PROJECT, with no way to recover (`rc=10`). Fixed by calling `gotoScreen(dev, Screen::PROJECT, ...)` directly. |
| 16 | `Primitives.cpp` — `loadFile` (file-selection scroll direction) | FIXED | The file-selection loop only knew how to scroll DOWN toward a target once it had scrolled out of the decoded viewport — a target *above* the current cursor position in the LOAD PROJECT browser (e.g. the first file in the list, browser left scrolled near the bottom from a prior manual browse) could never be reached (`rc=14`). Fixed by scrolling to the top of the list before searching. |
| 17 | `ScreenModel.h` — `kProjectFields` (`SAMPLEROOT`) | DATA | Field never existed on this firmware. Exhaustive search (SYSTEM SETTINGS submenu, MIDI settings submenu, LOAD PROJECT browser's OPT-key behavior) found no reachable field matching it anywhere; DOWN from `SYSTEM` (the real last row) never advances or scrolls, tested with both a tap and a 400ms hold. Removed the phantom field entry. Notably, this project's own SDL3 UI clone models `SAMPLE_ROOT` as a normal reachable field — a clone-vs-hardware discrepancy, flagged separately, out of driver scope. |
| 18 | `Primitives.cpp` — MIXER chain-walk (`isMixerChainField`) | FIXED, then REVERTED with the feature | Included `"IN_VOL"` in the recognized set, so the chain walker's own call to reach its entry point (`moveCursorTo(dev, "IN_VOL", ...)`) redirected straight back into the chain walker — infinite recursion. A single `cursor MIX_VOL` call hung 30+ seconds before being killed. |
| 19 | `Primitives.cpp` — MIXER chain-walk (hop retry) | FIXED, then REVERTED with the feature | On a hop mismatch, retried by pressing the *same key again* rather than just waiting longer. If the first press had actually succeeded but the read caught a stale frame, the second press advanced a second logical step — directly reproduced landing 2 fields past the intended target after 3 "failed" attempts that had each actually moved the cursor. |
| 20 | MIXER compound widget navigation (`MST_CHO`/`MST_DEL`/`MST_REV`/`MIX_VOL`/`LIM_VAL`/`DJF_FREQ`/`DJF_RES`) | **OPEN** | Root-caused, not fixable with a fixed key sequence: this widget's navigation is not a pure function of (current field, key pressed) — it depends on hidden state (most likely a per-column-group "last selected index" the M8 remembers independently of the visible cursor). Proven directly: a fully-isolated, hop-by-hop-verified path (`IN_VOL --RIGHT--> USB_VOL --RIGHT--> MIX_VOL --RIGHT-->...`) reproduced a *different* result on identical re-test after bugs #18/#19 were fixed. A fixed-sequence implementation was built, then deliberately reverted rather than shipped unpredictable. Also confirmed: the M8 remembers per-screen cursor position across screen re-entry, so a "bounce out and back" reset does not help escape this widget — only a bounded run of plain UP presses reliably does. `DJF_TYP`'s on-device position could not be located at all. |
| 21 | Instrument screen TYPE field — unmapped LOAD/SAVE column | **OPEN** | TYPE's row also has an unmapped LOAD/SAVE button pair at a higher column, with no field-map entry to disambiguate. If the cursor happens to already be parked on the LOAD button when `cursor TYPE` is called, `identifyCursorField`'s "col ≤ gridCol, greatest wins" rule can report "already on target" without verifying the actual column, leaving `editValue`'s value-cycling presses operating on the wrong widget. Found while setting up a MacroSynth-type test; did not affect any counted acceptance runs (which all navigated to TYPE fresh). Not fixed. |

---

## Related findings (confirmed real, explicitly out of driver scope)

These affect the broader M8 toolchain but are not bugs in the serial driver itself — listed here
only so they aren't lost, not tracked further in this document.

| Component | Summary |
|---|---|
| `m8_makeprobe` (Sampler probe generation) | Generated Sampler probes are ~125× quieter than an identically-configured, natively-authored instrument (capture peak 82 vs. 10302 / 32768). Root cause unknown — ruled out the AHD→VOLUME mod envelope specifically. Belongs to `M8_HARDWARE_TEST_SPEC.md` / `m8_makeprobe` tooling. |
| Real M8 hardware (low notes) | MIDI notes 24-27 (C-1 through D#-1) produce zero audio on real hardware, independent of SLICE or instrument config. Contradicts the `sampler-slice-repitch-hw` memory's assumption that C-1-based keyjazz would confirm SLICE note-base mapping. Belongs to a sampler-completion spec. |
| SDL3 UI clone (`src/ui/screens/project/ProjectScreenLayout.h`) | Models `SAMPLE_ROOT` as a normal PROJECT-screen field, DOWN-reachable from `SYSTEM_SETTINGS` — doesn't match real hardware (see bug #17 above). Task spawned separately to reconcile. |

---

*Cross-reference: `M8_DEVICE_CONTROL_SPEC.md` §6.5 (Tier 4.5 reliability hardening) and §7 (Tier 5
recipe attempts) carry the full hardware evidence, exact symptoms, and acceptance-test tallies
for every entry above.*
