# M8 Device Control Spec — General Framebuffer-Verified Driver

Turn `m8_nav`'s single proven routine (`navLoadFile`, load a project by name closed-loop)
into a **general-purpose driver** that can reach any screen, move the cursor to any field,
read and change any value, enter notes, and run scripted sequences on a real M8 headless —
every action verified against the device's own display (the SLIP framebuffer) as the oracle.

This is the keystone capability: once we can *author and inspect on the device* under program
control, the currently-blocked items fall out as recipes — MacroSynth/Braids hardware parity
captures, the SLICE note-base confirm, the REPITCH/BPM `STEPS` byte + tempo formula, and
automated regression of the clone against real hardware.

> **Scope.** Serial only. Like `m8_nav` today, the driver links **no engine, no SDL, no audio**
> (`status.md` invariant). It decodes the M8 *display* stream and presses buttons. Audio capture
> stays in `m8_capture`; a recipe orchestrates the two. Windows/Win32 serial (`\\.\COMx`); other
> platforms compile to the existing "not implemented" stub.

> **Status of this spec.** Tiers 0–4 are **DONE** (2026-07-17), with three real bugs found and
> fixed via *actual hardware testing* on 2026-07-18 (see below — the original "cursor detection
> fixed" and "unreliable on PHRASE/TABLE" claims below this line were written before that
> testing and turned out to be wrong in a specific, now-corrected way). **Tier 4.5 (§6.5,
> reliability hardening) is DONE for items 1, 3, and 4; item 2 is DONE with one remaining,
> root-caused, genuinely-unfixable-with-a-fixed-sequence gap** — completed across three hardware
> sessions on 2026-07-18. The second session found and fixed six real bugs (three field-map data
> errors, two decoder ghost-cell variants, one navigation-algorithm gap) and left two documented
> gaps (`SAMPLEROOT`, 8 `kMixerFields` entries). A third session resolved `SAMPLEROOT` (confirmed
> it doesn't exist on this firmware after an exhaustive search; removed the phantom field) and
> investigated the `kMixerFields` gap in depth: three more implementation bugs were found and
> fixed along the way, but rigorous re-testing then proved the underlying widget's navigation is
> genuinely non-deterministic (hidden per-widget state the driver can't observe), so a fixed-path
> implementation was built, confirmed unreliable, and deliberately reverted rather than left in
> place producing unpredictable results. **Tier 5 (recipes) remains mostly blocked** — recipe 1
> now passes end-to-end after a fourth and fifth bug were found while re-testing it; recipe 3's
> driver mechanics were re-validated (with a sixth bug found and fixed) but its actual
> audio-verification goal remains blocked on non-driver issues; recipes 2, 4, 5, 6 not attempted.
> See §6.5 and §7 for the full per-item results.
>
> The perception layer (`M8Device` library), screen model (`ScreenModel` with nav graph + field
> maps), the non-edit primitives (`gotoScreen`, `moveCursorTo`, `readField`, `pressUntil`,
> assertions) are built, compiled, and tested (28 offline `[hwdecode]` tests, 121 assertions).
> Tier 2 gesture pinning: all 16 direction/edit masks confirmed on hardware firmware 6.5.2 via
> `--pin-gestures` using the framebuffer oracle. Tier 4: `DeviceScriptRunner` compiles, loads
> `.m8script` files, and runs commands against the real device (`m8_nav --script FILE.m8script`).
> `m8_diffcheck` runs a script on the device, dumps the final screen as a text grid, and compares
> against a golden reference.
>
> **2026-07-18 — three real bugs found and fixed via actual hardware driving, all
> hardware-verified, not just reasoned about:**
> 1. **`findScreenForField` was type-blind for the Instrument screen.** Before navigating
>    anywhere, it could only resolve field names against the Sampler field map, so
>    MacroSynth-only fields (SHAPE/TIMBRE/COLOR/REDUX) resolved to `Screen::UNKNOWN` and
>    `editValue` rejected them before ever reaching the type-aware position lookup inside
>    `moveCursorTo`. Fixed: screen *identity* now checks the union of both field maps (only
>    exact *position* needs the type hint, resolved later once actually on-screen). Offline
>    regression test added; confirmed via a scratch probe compiled against the built library.
> 2. **`identifyScreen` couldn't recognize hex-suffixed screens.** `stripDigits` stripped only
>    trailing decimal digits, but phrase/chain/table IDs are hex (00-FF) and can end in a letter.
>    "PHRASE FC" (phrase 0xFC) canonicalized to "PHRASEFC", which never matched "PHRASE" —
>    `gotoScreen` returned `Screen::UNKNOWN` for roughly half of all possible IDs. Reproduced
>    live (`goto PHRASE` failing with "reached PHRASEFC instead of target" on a real device with
>    phrase FC loaded), fixed with a prefix-match fallback, re-verified live (`goto PHRASE` /
>    `goto TABLE` now succeed on IDs ending in A-F).
> 3. **Grid cursor detection (`moveCursorToGrid`) trusted the wrong signal on PHRASE/TABLE.**
>    Two things confirmed at the byte level on real hardware: (a) a static `<` (0x3C) glyph can
>    sit at a fixed data row with `fg == bg` (genuinely invisible on the real display, likely a
>    dormant playhead marker) and does not track cursor movement — it stayed at row 3 through 8
>    consecutive DOWN presses while the real cursor (accent-cyan foreground) correctly advanced
>    7→8→9→...→F; (b) the column-header label (e.g. "N" in "N V I FX1 FX2 FX3") is *also* drawn
>    in the same accent-cyan, and a naive color scan without a row floor picks up the header
>    first. Fixed: cursor detection now uses accent-color exclusively (never the glyph), floored
>    at the step-0 row's Y (found via its row *label*, not cursor state, so it's stable
>    regardless of where the cursor currently is). **Live-verified end to end**: `cursor
>    STEP0/STEP8/STEPF/STEP2` on PHRASE and `cursor STEP0/STEPA/STEP5` on TABLE all landed
>    exactly right, confirmed by the actual highlighted digit on screen after each jump.
>
> **Also fixed, not yet fully verified working end-to-end:** `editValue`'s numeric-target
> success check compared the *raw target string* against on-screen text (e.g. `"0x40"` against
> a screen that only ever shows `"40"`, no prefix) instead of comparing parsed values — so a
> target passed with a `0x` prefix could never match even after genuinely converging. Fixed to
> parse both sides as hex and compare numerically. This was caught by running the Tier 5
> set-param recipe below, which still does not pass end-to-end after the fix — see that section.
>
> **New, real limitation found (not a driver bug, a device behavior):** the M8 UI's own
> cursor navigation *skips* CUTOFF/RES when FILTER is OFF (they have no effect while disabled,
> so the device doesn't make them focusable via UP/DOWN). `moveCursorTo` has no way to know a
> field is conditionally unreachable and fails with "could not reach 'CUTOFF' in N steps" — this
> is expected behavior given the field map doesn't model conditional visibility, not a bug to fix
> blindly (a real fix would need the field map to express "reachable only if FILTER != OFF",
> which doesn't exist yet).

---

## 0. What exists today (the foundation to generalize)

**Promoted to `src/tools/m8/M8Device.{h,cpp}`** (Tier 0, done 2026-07-17). All former
`main_nav.cpp` internals now live in the `m8::dev` namespace as a reusable library:

| Piece | What it does | Location |
|---|---|---|
| `SerialPort` (Win32) | open/read/write `\\.\COMx` | `M8Device.h/.cpp` |
| `m8Enable`/`m8Reset`/`m8Disconnect` | `'E'`/`'R'`/`'D'` display control | `M8Device::open()`/`close()` |
| `m8Press(sp, mask, holdMs)` | `'C' <mask>` then `'C' 0x00` release | `M8Device::press()` |
| `SlipDecoder` (RFC1055) | reframes the serial byte stream | `M8Device.h` |
| `ScreenGrid` | decodes `0xFD` char / `0xFE` rect / `0xFF` sysinfo into `cells`, `highlights`, firmware; auto-detects cell pitch | `M8Device.h` |
| `ScreenGrid::topHeader()` | title = topmost non-empty row | `M8Device.h` |
| `ScreenGrid::cursorMainText()` / `cursorRowY()` | the highlighted (cursor) field text / its row | `M8Device.h` |
| `ScreenGrid::mainRows()` | all main-area text rows (`x < MAIN_X_MAX = 260`) | `M8Device.h` |
| `ScreenGrid::isCursor(cell)` | cursor = cell fg == accent cyan `(0,252,248)` | `M8Device.h` |
| `ScreenGrid::canon()` | normalized header (alnum+upper) | `M8Device.h` |
| `ScreenGrid::findField(label)` | locate a field by label substring in the main area | `M8Device.h` |
| `ScreenGrid::valueAt(col,row)` | read value text to the right of a field label | `M8Device.h` |
| `Key::{LEFT,UP,DOWN,SHIFT,PLAY,RIGHT,OPT,EDIT}` | pinned masks (`0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01`) | `M8Device.h` |
| `M8Device::read(settleMs, maxMs)` | drain serial → settled frame | `M8Device.h` |
| `M8Device::step(settleMs, maxMs)` | press-nothing, wait, re-read | `M8Device.h` |
| `M8Device::chord({keys}, holdMs)` | press multiple keys simultaneously | `M8Device.h` |
| `M8Device::playToggle()` / `keyjazz(note, vel)` | playback / live note | `M8Device.h` |
| `navLoadFile` | the whole closed loop (preserved in `main_nav.cpp`) | `main_nav.cpp` |

**The proven pattern** (keep it — it is the whole design in miniature):
`read screen → decide from what's actually shown → press → wait to settle → re-read → verify`.
Never blind key-counting: the M8's ~150 ms key auto-repeat defeats open-loop sequences, and the
device does **not** auto-home (it keeps whatever screen it was left on).

**Companion assets already in the tree**
- The clone's per-screen field layouts (`src/ui/screens/*/*ScreenLayout.h`, `NavGraph`) — the same
  field geometry the real M8 uses (the clone mirrors it). Reuse as the field model (Tier 1).
- The clone's headless script runner + VRAM shadow grid (`src/ui/ScriptRunner.*`,
  `Renderer` `dumpScreenText()`/`dumpJson()`, `VirtualCell[30][40]`) and its `.m8script` dialect
  — the offline oracle and the shared script vocabulary (Tier 4).
- `hw_buttons.json` — where pinned masks/gestures are recorded.

---

## 1. Architecture — three layers

```
                    ┌─────────────────────────────────────────────┐
  Layer 3  RECIPES  │ set-param, enter-phrase, slice-probe,        │  built ON Tier 2/3,
   & SCRIPTS        │ repitch-sweep, parity-capture, diff-vs-clone │  device or offline
                    ├─────────────────────────────────────────────┤
  Layer 2  VERIFIED │ gotoScreen(id)  moveCursorTo(field)          │  generic, verified,
   PRIMITIVES       │ readField(field) editValue(field,val)        │  offline-testable vs
                    │ enterNote(...)  pressUntil(pred)  assert*()   │  clone + SLIP replay
                    ├─────────────────────────────────────────────┤
  Layer 1  PERCEPT. │ M8Device: serial + SLIP + ScreenGrid +       │  promoted from
   & TRANSPORT      │ screen identity + field/cursor model         │  main_nav.cpp
                    └─────────────────────────────────────────────┘
```

**Design principle — one script, two backends.** Layer 3 scripts use the **same command
vocabulary as the clone's `.m8script`** (`key`, `wait`, `assert_screen`, `assert_row_matches`,
`load`, `play`, …), extended with edit/navigate verbs. A `Backend` interface has two
implementations:
- `CloneBackend` — drives the headless clone via `SDL_PushEvent` + reads the VRAM shadow (exists).
- `DeviceBackend` — drives the real M8 via `M8Device` (this spec).

That yields **differential testing**: run one script against both; the clone's shadow grid is the
spec for what a screen *should* contain, and the device is asserted against it. It also lets us
develop and unit-test nearly all driver logic **offline**, matching the project's offline-first
discipline. Device time is needed only for gesture pinning (Tier 2) and acceptance runs.

---

## 2. Layer 1 — `M8Device` (perception + transport)

**DONE** (2026-07-17). Promoted `main_nav.cpp` internals into `src/tools/m8/M8Device.{h,cpp}`
(no engine/SDL/audio). The API matches the spec design:

```cpp
namespace m8::dev {

struct FieldRef {            // an addressable field on the current screen
    std::string name;        // canonical, e.g. "PLAY", "CUTOFF", "TEMPO"
    int col = -1, row = -1;  // cursor grid coordinates (screen-relative), if known
};

struct ScreenId {            // identity of the current screen
    std::string header;      // raw topHeader()
    std::string canon;       // normalized: alnum+upper (e.g. "SONG","LOADPROJECT","PROJECT")
};

class M8Device {
public:
    bool open(const char* port);                 // serial + 'E' enable + 'R' reset
    bool openNoReset(const char* port);          // serial + 'E' enable only (skip 'R')
    void close();

    // ---- perception ----
    const ScreenGrid& read(int settleMs = 250,   // settle + re-read one frame
                           int maxMs   = 2000);
    ScreenId          screen();                   // identity of what's shown now
    Firmware          firmware() const;           // from 0xFF sysinfo; version guard

    std::optional<FieldRef> cursorField();        // the highlighted field + its value text
    std::vector<std::pair<int,std::string>> rows(); // main-area rows (mainRows())
    std::optional<std::string> valueOf(const FieldRef&); // read a field's current value text

    // ---- output (the one primitive) ----
    void press(uint8_t mask, int holdMs = 40);    // 'C' mask ; 'C' 0x00
    void chord(std::initializer_list<uint8_t>);   // e.g. {EDIT, UP} pressed together
    void playToggle();                            // PLAY 0x08 (toggle)
    void keyjazz(uint8_t note, uint8_t vel);      // 'K' note vel ; 'K' 0xFF

    // ---- convenience ----
    void step(int settleMs = 250, int maxMs = 2000);  // wait + re-read
    void readScreen(int settleMs = 250, int maxMs = 2000);
};
} // namespace m8::dev
```

`ScreenGrid` gained additive helpers: `canon()` (normalized header), `findField(label)` (locate
a field by label substring), `valueAt(col,row)` (read value text to the right of a label).
Everything else is unchanged from the original decode logic.

**Cursor = accent-cyan foreground** is the M8 default theme. Make `cursorColor` configurable and
add a **theme-probe** step (read a known screen, find the selection bar's color) so a non-default
device theme doesn't blind the driver. Record the detected accent alongside the firmware.

---

## 3. Layer 1.5 — the screen model (identity + navigation graph)

**DONE** (2026-07-17). Implemented in `src/tools/m8/ScreenModel.h` (header-only).

```cpp
enum class Screen : uint8_t {
    SONG, CHAIN, PHRASE, INSTRUMENT, TABLE, MODS,
    PROJECT, GROOVE, SCALE, MIXER, EFFECTS, INST_POOL,
    LOAD_PROJECT_MODAL, FILE_BROWSER, UNKNOWN
};

struct ScreenInfo { Screen id; const char* canonHeader; int gx, gy; };
```

- **`identifyScreen(canon)`**: maps canonical header → `Screen`. Handles the PROJECT/LOADPROJECT
  ambiguity, partial matches for INST/INST_POOL/MODS/EFFECTS, and modals.
- **`computeRoute(from, to)`**: returns a sequence of `NavStep{keyMask, viaScreen}` for
  SHIFT+arrow navigation on the grid. Moves vertically first, then horizontally.
- **`screenAtGrid(gx, gy)`**: maps grid coordinates → `Screen` (mirrors `ViewManager::getViewAt`).
- **Per-screen field maps**: `kProjectFields[]`, `kInstrumentSamplerFields[]`,
  `kInstrumentMacrosynFields[]`, `kEffectsFields[]`, `kMixerFields[]`, `kScaleFields[]` — each
  with `{name, label, col, row}` entries sourced from the clone's `*ScreenLayout.h` files.
- **`findFieldOnScreen(screen, name)`**: looks up a field by name substring in the screen's map.
- **`gridDims(screen)`**: returns `{rows, cols}` for grid-style screens (SONG/CHAIN/PHRASE/TABLE/
  GROOVE/INST_POOL).

Acceptance: from any starting screen (including a modal), reach every screen and assert its header.
Offline: drive the clone (its screen graph is the reference). Device: the Tier-1 acceptance run.

---

## 4. Layer 2 — generic verified primitives

**Partially DONE** (2026-07-17). Non-edit primitives built in `src/tools/m8/Primitives.{h,cpp}`;
edit primitives are stubs blocked on Tier 2.

Each primitive is the proven loop with a typed post-condition; on failure it returns the reason
**and the decoded screen** (like `navLoadFile`'s printed reasons + the harness auto-dump).

### 4.1 `gotoScreen(target)` — navigate to any screen ✅
Read current screen, cancel any modal (OPT), compute route via `computeRoute()`, step one hop at
a time re-identifying after each. Bounded retries; fails with the actual header if target is
never reached. Works from any starting screen including modals.

### 4.2 `moveCursorTo(fieldName)` — position the cursor ✅
Read `cursorField()`; if not on target, step UP/DOWN/LEFT/RIGHT toward it using the per-screen
field map, re-reading each time. For form-style screens (Project, Instrument, Effects, Mixer,
Scale). For grid-style screens: parses field name as `STEPn`/`Sn`/`TcSn` (hex step, optional
col) and delegates to `moveCursorToGrid`. **Grid navigation works reliably on SONG, PHRASE, and
TABLE** — as of 2026-07-18, cursor detection uses accent-color exclusively (floored at the
step-0 row so the column-header label can't be mistaken for the cursor), not the `<` glyph
(which can be a static, invisible, non-cursor artifact on real hardware — see the status block
above). Live-verified: arbitrary step jumps on both PHRASE and TABLE landed exactly right.
Known real limitation (not a bug): fields the M8's own UI conditionally skips during UP/DOWN
navigation (e.g. CUTOFF/RES when FILTER is OFF) are unreachable, since the field map doesn't yet
express conditional visibility. No-progress bail-out prevents hangs. Bounded steps; fail if the
field never becomes current.

### 4.3 `readField(fieldName) -> value` ✅
`moveCursorTo` then read the cursor text (the value cell highlighted by the cursor). Returns the
raw on-screen text (e.g. `"80"`, `"FWD"`, `"C-4"`). The framebuffer is authoritative.

### 4.4 `editValue(field, target)` ✅ (with a caveat)
Gated on `getGestures().isReady()`; falls back to the explicit "gestures not pinned" error only
if `hw_buttons.json` isn't populated (it is — see Tier 2). Numeric targets step via `valueInc`
up to 256 times, comparing parsed hex values (fixed 2026-07-18 — previously compared the raw
target string, so a `0x`-prefixed target could never match the device's unprefixed on-screen
text). **Not yet verified end-to-end on hardware**: the Tier 5 set-param recipe (§7) still fails
after this fix, for a reason not yet root-caused — see that section.

### 4.5 `enterNote(track/row, note, vel)` / `clearCell` ✅
Gated the same way as `editValue`. Implemented with value-convergence verification; not
independently re-verified on hardware during the 2026-07-18 session (that session's device time
went to the navigation/cursor bugs above and the set-param recipe).

### 4.6 `pressUntil(predicate, maxPresses, key)` — bounded scan ✅
Press `key` up to `maxPresses` times, re-reading after each, until `predicate(grid)` returns
true. Returns the number of presses used, or -1 if never satisfied.

### 4.7 Assertions ✅
`assertScreen(expected)`, `assertField(name, value)`, `assertRowMatches(y, substring)`,
`assertFirmware(>= major, minor, patch)`. `assertPlaying()` is a placeholder (play indicator
not yet pinned to framebuffer).

---

## 5. Layer 2 blocker — **Tier 2: empirical gesture pinning (hardware, required)** ✅ DONE

All 16 direction/edit gesture masks pinned on firmware 6.5.2 using `--pin-gestures` (framebuffer
oracle). Recorded in `hw_buttons.json` with firmware metadata.

| Gesture | Mask (confirmed) | Effect |
|---|---|---|
| value +1 / −1 | `EDIT`+`RIGHT` (0x05) / `EDIT`+`LEFT` (0x81) | numeric ±1, note ±1 semitone, enum next/prev |
| value +0x10 / −0x10 | `EDIT`+`UP` (0x41) / `EDIT`+`DOWN` (0x21) | numeric ±16, note ±1 octave |
| insert default note | `EDIT` (0x01) | inserts C-4 vel 64 at cursor (note column) |
| clear cell | `OPT`+`EDIT` (0x03) | clears the cell under cursor |

`Gestures.cpp` implements `GestureTable::loadFromFile()` / `saveToFile()` with a simple JSON
parser. `editValue`, `enterNote`, `clearCell` in `Primitives.cpp` use the gesture table and
verify convergence by re-reading the screen after each edit.

---

## 6. Layer 3 — script dialect + differential harness

### 6.1 Shared `.m8script` vocabulary, two backends
Reuse the clone's grammar (`ScriptRunner`) and add device-oriented verbs that map to Layer 2:
`goto <screen>`, `cursor <field>`, `set <field> <value>`, `note <cell> <name> [vel]`,
`assert_field <field> <value>` — plus the existing `key/wait/play/stop/load/save/dump_screen/
dump_json/assert_screen/assert_row_matches/assert_playing`. Exit codes match the clone runner
(0 pass / 1 assertion / 2 parse), and on failure auto-dump the decoded screen.

`m8_nav --script FILE [--json out.json]` runs it against the device; the existing
`--script … --headless` runs it against the clone. Same file, either target.

### 6.2 Differential test (`m8_diffcheck`)
Run one script on both backends and diff the resulting screen grids cell-by-cell (ignoring
theme-color, comparing glyph+layout). Reports the first divergence. This is how we (a) keep the
clone honest against hardware and (b) catch device-driver bugs — the clone shadow grid is a
already-tested oracle. Runs in CI for the clone half; the device half is an on-demand acceptance run.

---

## 6.5 Tier 4.5 — reliability hardening (**required before Tier 5 recipes can be trusted**)

**Status: DONE for 6.5.1, 6.5.3, 6.5.4; DONE-WITH-GAPS for 6.5.2 (2026-07-18, second hardware
session).** A first pass at this tier left substantial code in place (`confirmRead`,
`dismissModal`, `identifyCursorField`, a 3-step `computeRoute`, match-priority
`findFieldOnScreen`) but never verified any of it against real hardware or reported results —
that gap is what this pass closed. All four items were run to their stated acceptance criteria on
a real M8 (fw 6.5.2, COM3). Item 1, item 3, and item 4 hit their criteria cleanly. Item 2 hit its
criterion for 5 of 6 required field maps (`kInstrumentSamplerFields`,
`kInstrumentMacrosynFields`, `kEffectsFields`, `kScaleFields`, and all-but-one of
`kProjectFields`) but has two honestly-unresolved gaps in `kProjectFields` (1 field) and
`kMixerFields` (8 of 11 fields) — see 6.5.2 below for exactly which fields and why. Six additional
real, hardware-confirmed bugs were found and fixed during this pass (beyond the three from the
first pass, listed in the status block at the top):
1. **Ghost-cyan-blank-cell bug in `cursorRowY()`** — the M8 firmware skips redrawing a row's
   trailing blank cells when the cursor leaves it (a cyan space looks identical to a normal
   space), leaving stale cyan padding at the vacated row that `cursorRowY()`'s naive
   topmost-cyan-cell scan picked up over the real cursor's row. Fixed by requiring the picked
   cell be non-space (real cursor rows always have real label text colored cyan; ghosts are, by
   construction, always blank padding).
2. **The same ghost effect in `cursorField()`'s X-position lookup** — a sibling bug, not caught
   by fix #1: moving between two fields *on the same row* (e.g. away from "MOD TYPE" back onto
   "INPUT EQ" on the EFFECTS screen) left ghost cyan blanks at the vacated field's leading
   columns, and the X lookup picked the leftmost ghost blank instead of the real field's text.
   Same fix, same rationale, different function.
3. **`moveCursorTo` always pressed RIGHT when on the correct row but wrong field** — real M8
   multi-column form rows (e.g. Instrument/Sampler's SLICE/AMP pair) aren't reached column-by-
   column via UP/DOWN the way the field maps assumed; DOWN from a full-width row like SAMPLE
   lands directly on the row's *right*-column field (AMP), skipping the left-column field
   (SLICE) entirely. Reaching SLICE from there requires LEFT, which the code never tried. Fixed
   to compare the target's column against the current column and press LEFT or RIGHT
   accordingly, with a tolerance for 1-pixel-column measurement noise (see bug #6) and a
   single-shot "stuck" fallback (alternates axes rather than latching onto one, so it can't loop
   forever pressing the wrong direction — see 6.5.2 for the SCALE-screen case that required this).
4. **`findScreenForField` resolved short field names to the wrong screen via fuzzy substring
   matching** — `"MST_CHO"` (Mixer) substring-matches `"CHO"` (Instrument/Sampler's chorus send),
   and since INSTRUMENT is checked before MIXER in the screen table, a lookup for `"MST_CHO"`
   resolved to INSTRUMENT instead of MIXER (same collision for `MST_DEL`/`DEL` and
   `MST_REV`/`REV`). Fixed to search for an *exact* name match across every screen first, only
   falling back to fuzzy substring matching if no screen has an exact match anywhere.
5. **`kEffectsFields` was uniformly off by one row and had the wrong column** — the map was
   missing the CHORUS section's first row ("MOD TYPE", the section header widget, sits above
   "INPUT EQ") entirely, so every field inherited a +1 row offset. This "worked" by accident
   (each entry still landed on *some* real, distinct field, just mislabeled) until the offset
   crossed the blank gap row between the CHORUS and DELAY sections, at which point DEL_EQ's
   mapped row pointed at the empty gap instead of DELAY's real INPUT EQ row and was permanently
   unreachable. Re-measured all 14 rows/columns directly from pixel data.
6. **1-pixel column-measurement noise falsely triggered the axis-fallback from bug #3** — a
   field's mapped column (e.g. SYSTEM's col 0) can differ by exactly 1 from its measured pixel
   column (1) due to rounding in the pixel-to-column conversion, which `identifyCursorField`'s
   own matching already tolerates but the new fallback didn't — sending it into a useless
   LEFT-press loop instead of just continuing to press DOWN. Fixed with a >1-column tolerance.

The spec's own design principle ("one script, two backends," §1) implicitly promises unattended
chaining. This tier's acceptance criteria are specifically about verifying that promise holds
under repeated, unattended execution — not just that each primitive is correct in isolation.

**Investigate for a common root cause before patching each symptom separately.** The grid-cursor
bug (§ status block, item 3) turned out to generalize: form/grid screens can have multiple
cells sharing the "cursor" accent color for unrelated reasons (a static glyph, a column-header
label), and a single post-press read can catch a transitional frame. The four items below may
share one or two of these same root causes rather than being four independent bugs — check that
before writing four independent fixes.

### 6.5.1 `gotoScreen` hop-verification robustness
**Symptom (reproduced live, 2026-07-18):** `--goto-screen PROJECT` landed on SONG, MIXER,
EFFECTS, and INST_POOL on different attempts, from different starting screens, well after the
`identifyScreen` hex-suffix fix — so it isn't (only) that bug. Manual single-press-then-verify
navigation worked every time on the same routes; only the automated multi-hop retry loop failed.
**Deliverable:** root-cause why the hop-verification loop in `gotoScreen` (`Primitives.cpp`)
sometimes lands on the wrong screen — the leading hypothesis is that a hop's post-press read
catches a transitional/incomplete frame, the loop concludes "hop didn't land," and retries,
turning one intended press into two and overshooting the route — but confirm this on the device
before fixing it; don't guess. Fix the confirmed cause.
**Acceptance:** a script that calls `goto PROJECT` from each of the 12 non-modal screens (SONG,
CHAIN, PHRASE, INSTRUMENT, TABLE, GROOVE, MODS, SCALE, INST_POOL, MIXER, EFFECTS, and PROJECT
itself as a no-op case), 3 times each starting screen (36 runs total), lands on PROJECT every
single time with the header-confirmed as the *first* successful read, not after a
correction-hop. Zero flakes.

**Result (2026-07-18): MET. 36/36, zero retries, zero flakes.** The `confirmRead`/`dismissModal`
rewrite already in place from the first pass (settle-and-verify double-read, bounded 3-attempt
press retry per hop, fallback "climb to PROJECT" when `computeRoute` returns empty) worked
correctly as soon as it was actually run against hardware. No further changes needed for this
item.

### 6.5.2 `moveCursorTo` field-landing reliability
**Symptom (reproduced live, 2026-07-18):** `cursor AMP` on the Instrument screen (Sampler type)
did not reliably land on or stay on AMP before a subsequent `set AMP 0x40` ran — the auto-dumped
failure screen showed the cursor on SAMPLE instead, and AMP still at its untouched starting
value. This is distinct from the already-diagnosed FILTER-OFF/CUTOFF-unreachable case (§4.2) —
AMP has no such conditional-visibility excuse.
**Deliverable:** root-cause why `moveCursorTo` can drift off the intended field between landing
on it and a subsequent primitive reading/acting on it. Check whether this is the same
transitional-frame-read issue as 6.5.1, or a separate issue specific to `cursorField()`'s
row-matching on multi-column form screens (recall: on PROJECT, an entire row — both label and
value — shares the cursor's accent color; check whether Instrument-screen rows with adjacent
left/right-column fields have a similar ambiguity that could cause `moveCursorTo` to settle on
the wrong column). Fix the confirmed cause.
**Acceptance:** for every field in `kInstrumentSamplerFields`, `kInstrumentMacrosynFields`,
`kProjectFields`, `kEffectsFields`, `kMixerFields`, and `kScaleFields`, run `cursor <field>;
assert_field <field> <value-read-immediately-after-landing>` 3 times each. Zero mismatches
between the field `cursor` claims to have landed on and the field `readField`/`assert_field`
actually reads immediately after.

**Result (2026-07-18, first pass): MET for 5 of 6 maps in full; 2 documented gaps.**
**Result (2026-07-18, second pass): `SAMPLEROOT` gap resolved; MIXER gap investigated further,
root cause now understood but confirmed NOT fixable with a fixed-sequence driver approach —
still open, more precisely diagnosed.** Root cause of the original 5-map pass was the six bugs
listed in the §6.5 status block above (mainly bugs #1–#4 for this item; bug #6 also required a
fix mid-investigation). After those fixes, two full runs of the complete field-landing script
(every field below, 3x each) both passed cleanly:
- `kInstrumentSamplerFields` (23 fields, incl. the previously-blocking SLICE): **69/69, 2 runs**.
- `kInstrumentMacrosynFields` (20 fields): **60/60, 2 runs**.
- `kEffectsFields` (14 fields, after the row/column re-map): **42/42, 2 runs**.
- `kScaleFields` (4 fields, after a column re-measurement): **12/12, 2 runs**.
- `kProjectFields`: **now 39/39, all 13 real fields, 2 runs.** `SAMPLEROOT` (the 14th, originally
  a documented gap) has been **removed from the field map** after an exhaustive hardware search
  found no reachable field matching it anywhere: not DOWN-reachable from `SYSTEM` (confirmed
  repeatedly — before/after DOWN-press screen dumps are byte-identical, tested with both a short
  tap and a 400ms hold, ruling out a scroll-acceleration explanation), not inside the "SYSTEM
  SETTINGS" submenu EDIT on `SYSTEM` actually opens (its full 14-field list has no sample-root
  entry), not in the MIDI settings submenu, and not in the LOAD PROJECT browser's OPT-key
  behavior (no context menu appears). Notably, this project's own SDL3 UI clone
  (`src/ui/screens/project/ProjectScreenLayout.h`) models `SAMPLE_ROOT` as a normal DOWN-reachable
  sibling row — real hardware disagrees with the clone here; flagged separately as a clone-only
  discrepancy (task spawned, not part of this driver's scope). See `ScreenModel.h`'s
  `kProjectFields` comment for the full evidence trail.
- `kMixerFields`: still **3 of 11 fields** (`OUT_VOL`, `IN_VOL`, `USB_VOL`) meet acceptance;
  `MST_CHO`, `MST_DEL`, `MST_REV`, `MIX_VOL`, `LIM_VAL`, `DJF_FREQ`, `DJF_RES`, `DJF_TYP` remain
  unreachable. **Investigated much further this session, with a materially different conclusion
  than the first pass.** Careful, fully-isolated hardware probing (one key press per process
  invocation, guaranteeing complete settle between presses — the same rigorous method that
  correctly diagnosed every other bug in this section) initially found what looked like a clean,
  fully deterministic linked-graph path connecting all 8 stuck fields (`IN_VOL --RIGHT-->
  USB_VOL --RIGHT--> MIX_VOL --RIGHT--> LIM_VAL --RIGHT--> DJF_FREQ --DOWN--> DJF_RES --DOWN-->
  MST_REV --UP--> MST_DEL --UP--> MST_CHO`), individually hop-by-hop verified. A driver
  implementation of that exact path was built, and along the way three more real, confirmed bugs
  in the *implementation* were found and fixed (an infinite-recursion self-dispatch bug, a
  blind-retry pattern that compounded overshoot by re-pressing a key whose effect just hadn't
  been read yet, and confirmation that the M8 remembers per-screen cursor position across
  re-entry — a screen bounce does not reset it, only a bounded run of plain UP presses reliably
  climbs back out of the widget). **After all three implementation bugs were fixed, the exact
  same probing method — one press per process, full settle, the same sequence that found the
  path in the first place — reproduced a *different* result on re-test**: `IN_VOL --RIGHT-->
  USB_VOL --RIGHT-->` landed on `DJF_FREQ`, not `MIX_VOL`, for the identical input sequence.
  **Conclusion: this compound widget's navigation is not a pure function of (current field, key
  pressed) — it depends on hidden state (most likely a per-column-group "last selected index"
  the M8 remembers independently of the visible cursor) that this driver has no way to observe
  or control.** A fixed-sequence chain walk — however carefully hop-verified at the time it was
  written — cannot reliably reproduce a moving target. The chain-walk implementation was reverted
  entirely rather than left in place producing unpredictable results (it could silently overshoot
  by an unbounded number of fields depending on which state the widget happened to be in). This
  is now a materially better-understood gap than the first pass's diagnosis: not "an inconsistent
  gesture, not yet found the right one," but "no fixed gesture sequence can work here at all." A
  real fix would need either a genuinely adaptive, stateful search (try each direction from
  wherever the cursor actually is, verify, backtrack — no assumption of a fixed graph) or
  firmware-level insight into what state the widget is keying off, neither of which is a quick
  fix. `DJF_TYP`'s on-device position also still could not be located anywhere in the visible
  main area. Row/col values for the 7 named-but-unreachable fields are kept at their measured
  positions in `kMixerFields` (accurate data for whoever attempts a real fix later), with a code
  comment on the reverted feature explaining what was tried and why it doesn't work.
- Also worth flagging separately (unrelated to the MIXER gap, found while re-testing
  `kInstrumentMacrosynFields`): `readInstrumentType`/`identifyCursorField`'s handling of
  Instrument's TYPE field has its own unmapped-column ambiguity (TYPE's row also has an unmapped
  LOAD/SAVE button pair at a higher column) that can leave `cursor TYPE` parked on the LOAD
  button rather than the TYPE value if the cursor happens to already be there — did not affect any
  of the runs actually counted above (which all navigated to TYPE fresh), not fixed.

### 6.5.3 Modal handling as a first-class primitive
**Symptom (reproduced live, 2026-07-18, multiple times):** both "LOSE CHANGES TO CURRENT SONG?"
and "LOSE CHANGES TO INSTRUMENT?" modals needed **two** EDIT presses to actually dismiss, not
one — the first press appeared to do nothing (modal still showing, unchanged, on the very next
read). Every occurrence today was worked around by hand (retry the press, re-check, retry
again) rather than by the driver itself.
**Deliverable:** add a `dismissModal(dev, confirm: bool)` primitive to `Primitives.{h,cpp}` that
presses EDIT (confirm) or OPT (cancel), re-reads, and if the modal is still present (by text
match, not just "some read happened"), retries up to a bounded count before failing loudly with
the screen attached — the same read-verify-retry discipline `moveCursorToGrid` now uses. Wire it
into every place a modal can currently appear mid-sequence: `gotoScreen`'s modal-cancel step,
`loadFile`'s "lose changes" confirm, and any edit path that can trigger a type-change confirm
(per the MacroSynth-type-cycling incident from today's session, in the M8_SAMPLER_COMPLETION
line of work — changing TYPE away and back is destructive and modal-gated).
**Acceptance:** trigger each of the two known modal types at least 5 times each (10 runs total);
`dismissModal()` resolves every single one without the calling script needing to know how many
presses it took internally.

**Result (2026-07-18): MET. 20/20 across two full runs of each modal type (10 SONG-modal + 10
INSTRUMENT-modal dismissals per run).** "LOSE CHANGES TO CURRENT SONG?" triggered via `PROJECT`
row's LOAD button and dismissed by `gotoScreen`'s modal-cancel path; "LOSE CHANGES TO
INSTRUMENT?" triggered by cycling the TYPE field's enum value (once real content — a loaded
sample — was present on the instrument, since an untouched instrument doesn't trigger it) and
dismissed the same way. Both reproduced the exact spec symptom directly: a single EDIT press
consistently left the modal showing unchanged; `dismissModal`'s retry loop resolved every
occurrence without the caller needing to know that.

### 6.5.4 `loadFile` automation fix
**Symptom (reproduced live, 2026-07-18):** `m8_nav --load-file probe_slice4` failed outright
(`rc=11` — the "reach the LOAD PROJECT row" loop in `loadFile`, `Primitives.cpp`, gave up after
30 iterations). Never root-caused today; the file was instead loaded by driving the browser
manually, key by key, with a human reading each intermediate screen.
**Deliverable:** root-cause the rc=11 failure. Leading hypotheses to check on the device (don't
assume): (a) the same transitional-read issue as 6.5.1/6.5.2; (b) `loadFile`'s row-target search
(`t.find("LOAD") && t.find("PROJECT")` against `mainRows()` text) versus its cursor-arrival check
(`cursorMainText()` containing both substrings) disagreeing about what "arrived" means, given
today's finding that whole-row highlighting on PROJECT's form fields makes `cursorMainText()`'s
content wider than a caller might expect. Fix the confirmed cause, and use the new
`dismissModal()` primitive (6.5.3) for the "lose changes" step instead of `loadFile`'s current
ad-hoc single-press handling.
**Acceptance:** `m8_nav --load-file NAME` succeeds unattended for at least 3 different real
files already on the SD card's `/PROBES` or song folders, 3 runs each (9 total), 100% success,
zero manual intervention, zero rc=11.

**Result (2026-07-18): MET. 9/9, 100% success, zero manual intervention.** Root cause of the
original rc=11 was upstream of `loadFile` itself: `findFieldOnScreen(Screen::PROJECT, "PROJECT")`
failed because of the same substring-matching and off-target-row issues fixed for 6.5.2, not a
bug specific to `loadFile`'s own logic — once those were fixed, rc=11 stopped reproducing.
A second, `loadFile`-specific bug was found and fixed while running the acceptance test itself:
the file-selection loop only knows how to scroll DOWN toward a target once it has scrolled out of
the decoded viewport, so a target *above* the current cursor position in the LOAD PROJECT browser
(e.g. selecting the first file in the list while the browser happened to still be scrolled near
the bottom, left over from a previous manual browse) could never be reached — DOWN at the bottom
of the list does nothing and the loop exhausts (`rc=14`). Fixed by scrolling to the top of the
list before searching, so every target is reachable via DOWN-only scrolling. Tested against
`PROBE_SLICE4.M8S` (the exact file named in the original symptom), `PROBE_SAMPLER.M8S`, and
`PROBE_SHAPE_00.M8S`, 3 runs each, all `rc=0`. A separate, narrower gap was also found: `loadFile`
climbs toward the PROJECT screen with a simple repeated `SHIFT+UP` rather than `gotoScreen`'s
route-based navigation, and that climb can get stuck on `INST_POOL` if invoked from the
INSTRUMENT screen specifically (`rc=10`) — not hit by any of the 9 counted runs (which all
started from PROJECT or SONG, matching how the CLI is normally invoked), not fixed, worth noting
for anyone calling `loadFile` from an arbitrary starting screen.

### Out of scope for this tier (real, but not driver bugs)
Two more findings from today's session are real and worth fixing, but belong to different specs
and must not be folded into this tier's scope:
- **`m8_makeprobe`'s Sampler probes are ~125× quieter than a natively-authored instrument with
  identical nominal settings** (capture peak 82 vs. 10302 out of 32768). Confirmed not caused by
  the AHD→VOLUME mod envelope (replicating it on the native instrument had zero effect on
  volume). Root cause unknown. This is `M8_HARDWARE_TEST_SPEC.md`/`m8_makeprobe` tooling scope.
- **Very low notes (MIDI 24-27, C-1 through D#-1) produced zero audio** on real hardware,
  independent of SLICE or instrument config — contradicts the `sampler-slice-repitch-hw`
  memory's assumption that C-1-based keyjazz would audibly confirm SLICE note-base mapping. This
  blocks Tier 5 Recipe 3 specifically and belongs in that memory / a sampler-completion spec, not
  here.

---

## 7. Layer 3 — the recipes that unblock everything else

Once Tiers 1–3 exist, each blocked item is a short script/recipe:

1. **Set any instrument parameter on the device** — `goto instrument; cursor CUTOFF; set CUTOFF 0x40;
   assert_field CUTOFF 40`. (Generic; the direct payoff the user asked for.)
2. **Enter a phrase** — sequence of `note`/`set` verbs, each verified. Enables authoring test songs
   directly on hardware instead of the SD-copy dance.
3. **SLICE note-base confirm** (memory `sampler-slice-repitch-hw`): load a SLICE=04 sampler probe,
   `keyjazz` C-1/C#-1/D-1/D#-1, capture each (`m8_capture`), assert the 0/25/50/75 % start jumps.
4. **REPITCH/BPM `STEPS` byte + tempo formula**: `set` STEPS to 0x40 vs 0xC0, `save` two songs,
   byte-diff instrument 0 to identify the byte; then fix STEPS, sweep BPM (and vice-versa),
   `keyjazz`+capture, measure pitch/duration to pin the law. Pure recipe over Layer 2 + capture.
5. **MacroSynth/Braids hardware parity**: load a per-model probe, `set` shape/timbre/color, play,
   capture, `m8_spectrum` vs our render — now a batch loop, per Braids model.
6. **Regression vs hardware**: nightly `m8_diffcheck` of a fixed script (clone vs device).

Recipes 3–5 also require the **capture-level fix** (host Windows recording level; memory
`hw-test-rig`) and a **sustaining amp probe** — orthogonal to this spec but noted so a recipe run
isn't surprised by silent captures. **Update 2026-07-18: the host recording level was checked
and is already at 100/100 (Windows Sound settings, confirmed by the user) — it is NOT the cause
of the near-silent captures found below.** Something else is.

**2026-07-18 — recipes 1 and 3 attempted on real hardware. Neither completed cleanly; both
surfaced real, useful findings.**

**Recipe 1 (set-param):** `tests/hw/set_param.m8script` written and run against the device.
- First run targeted CUTOFF (as originally specced above) and failed: `moveCursorTo` couldn't
  reach it in 40 steps. Root cause, confirmed by reading the on-screen FILTER value: FILTER was
  OFF, and the real M8 UI *skips CUTOFF/RES entirely during UP/DOWN navigation* when the filter
  is disabled (they have no effect, so the device doesn't make them focusable). Not a driver
  bug — the field map has no way to express "reachable only if FILTER != OFF" yet. Retargeted
  the recipe at AMP (always focusable) rather than modeling conditional visibility, which is
  out of scope for a quick fix.
- Second run (AMP) failed differently: `editValue` looped all 256 steps and never matched
  target `'0x40'`. Root cause found and fixed (§4.4 above): the success check compared the raw
  string `"0x40"` against on-screen text that only ever shows `"40"` — never a match even on
  true convergence.
- Third run (same script, with the fix built) **still failed**, same error, same symptom. The
  auto-dumped screen showed AMP still at its starting value `00` and the cursor sitting on
  SAMPLE, not AMP — meaning `cursor AMP` itself didn't reliably land on/stay on the AMP field
  before `set` ran. This was a **third, distinct issue** in the moveCursorTo/editValue handoff —
  fixed by Tier 4.5's `moveCursorTo` field-landing work (§6.5.2), which this recipe was blocked
  behind, not a bug specific to this recipe.
- **Update 2026-07-18 (after Tier 4.5): recipe re-run and PASSES, 3/3.** Found and fixed a
  **fourth, real bug** while re-testing, upstream of both earlier fixes: `readCursorValue()`
  (used by `editValue`'s convergence check) read `cursorMainText()` raw, which on a multi-column
  form row (e.g. `AMP`'s row, shared with `TIMBRE`/`COLOR`/etc.) returns the field's on-screen
  *label* concatenated directly against its value with no separator — `"AMP"` + `"FF"` =
  `"AMPFF"`. The numeric convergence check parses the *leading* hex run of that string; since
  `'A'` is a valid hex digit as well as a letter, it silently parsed `"A"` (=10) as if it were the
  whole value and could never match any real target — the loop ran all 256 steps and clamped at
  `0xFF` without ever detecting it had passed through the target along the way. Fixed by looking
  up the field's exact on-screen label (via the same field maps `moveCursorTo` already uses) and
  stripping exactly that prefix — a generic "skip leading letters" heuristic doesn't work here,
  since hex-digit letters (A-F) are also alphabetic and can appear at the end of a label (e.g.
  `"TIMBRE"` ends in `E`). A second, related bug was fixed alongside it: the stepping loop only
  ever pressed `value_inc`, never `value_dec` — hardware-confirmed that values clamp at `0xFF`
  rather than wrapping, so a target *below* the current value could never be reached by
  incrementing alone. Fixed to pick direction from the parsed current-vs-target comparison each
  iteration. `tests/hw/set_param.m8script` now passes 3/3 unattended runs.

**Recipe 3 (SLICE note-base confirm):** built a SLICE=4 sampler probe via `m8_makeprobe --slice`
(a new flag added this session), loaded it on the device, and found the generated probe was
**near-silent on hardware** (capture peak 82/32768, vs. an identically-configured
natively-authored instrument at 10302/32768 — a ~125x difference for nominally the same
settings). This is a **real bug in `m8_makeprobe`'s Sampler probe generation**, not a hardware
or capture-level issue — confirmed by isolating the variable (same AMP=00, same sample, built
via the device's own UI instead of our tool). Root cause not yet found: replicating the
AHD→VOLUME mod envelope `m8_makeprobe` attaches (`dest=1, amount=0xFF, attack=0x01, hold=0x80,
decay=0x80`) on the natively-authored instrument had **zero effect on its volume** (still
10302), which rules out the mod envelope itself as the cause. The actual differing field is
still unknown — worth a dedicated investigation, not chased further today to avoid scope creep.
Separately, once a *working* (natively-authored) SLICE=4 instrument was tested with the actual
recipe's notes (C-1/C#-1/D-1/D#-1, MIDI 24-27), **all four captures were completely silent**
(zero across the entire buffer) — a new, real finding: very low notes produce no audible output
on this hardware at all, independent of SLICE or the makeprobe bug. This contradicts the
`sampler-slice-repitch-hw` memory's assumption that C-1-based keyjazz would audibly confirm
slice mapping. The note-base question remains **unconfirmed** — recipe 3 did not complete.

**Update 2026-07-18 (after Tier 4.5): driver-level mechanics re-validated, but the recipe's
actual goal (audibly confirming SLICE note-base mapping) remains blocked on the two findings
above — neither is a driver bug, so neither was pursued further here.** What *was* re-checked,
since Tier 4.5 changed the code paths this recipe depends on:
- Loading `PROBE_SLICE4.M8S` (the exact probe from the original attempt, still present on the
  SD card) now succeeds via `loadFile`, 3/3 runs, including from a cold start on the INSTRUMENT
  screen — which surfaced a **fifth real bug**, found while re-testing this recipe specifically:
  `loadFile`'s climb toward the PROJECT screen used a separate, ad-hoc "press SHIFT+UP up to 8
  times" loop instead of the already-hardened `gotoScreen` (36/36 hop-verified, §6.5.1) — a second,
  less-reliable navigation mechanism duplicating one that already worked. Reproduced directly:
  invoking `loadFile` while sitting on the INSTRUMENT screen failed with `rc=10`
  ("could not reach PROJECT screen, header=INSTRUMENTPOOL") — SHIFT+UP from INSTRUMENT can land
  on INST_POOL instead of climbing toward PROJECT, and the blind loop had no way to recover.
  Fixed by replacing the ad-hoc loop with a direct call to `gotoScreen(dev, Screen::PROJECT, ...)`.
  Re-verified 3/3 from a cold INSTRUMENT-screen start (2 different probe files).
- The `keyjazz` serial command itself (`M8Device::keyjazz`, `'K'` + note byte + velocity byte) was
  inspected: it passes the raw MIDI note number through unmodified, with no encoding that could
  plausibly misbehave specifically at the low end (MIDI 24-27 are ordinary `uint8_t` values, no
  overflow/wraparound risk). Nothing found here that would explain the low-note silence as a
  driver-side bug rather than an instrument/hardware one — consistent with the original finding's
  own framing.
- **Not re-attempted**: the `m8_makeprobe` amplitude bug and the low-note-silence finding
  themselves. Both are audio/instrument-configuration issues, not driver bugs — explicitly out of
  this spec's scope (see "Out of scope for this tier" above) and belong to `m8_makeprobe`
  tooling / a sampler-completion spec respectively. Recipe 3's audio-verification goal is
  therefore still blocked, honestly reported as such rather than claimed fixed.

---

## 8. Testing strategy (offline-first, device only where unavoidable)

**Implemented** (2026-07-17): 17 offline `[hwdecode]` replay tests in `tests/test_device_decode.cpp`
covering `identifyScreen`, `cursorMainText`, `canon`, `findField`, `valueAt`, `topHeader`,
`mainRows`, `isCursor`, field map lookups, grid dimensions, and `computeRoute`. All 71 assertions
pass. No device required.

| Layer | Offline test (no device) | Device test |
|---|---|---|
| SLIP decode / ScreenGrid | ✅ **replay recorded frame dumps** into `ScreenGrid`, assert decoded grid (17 tests, 71 assertions). | — |
| screen identity / `gotoScreen` | ✅ `identifyScreen` + `computeRoute` tested offline via replay; nav graph verified by field map coverage. | `gotoScreen` accept on device (Tier 3) |
| cursor model / `moveCursorTo` / `readField` | Unit-tested against replay fixtures (cursor detection, valueAt, findField). | spot-check on device |
| `editValue` / `enterNote` loops | blocked on Tier 2 gesture pinning. | **gesture pinning (Tier 2)** + confirm |
| script runner / diff harness | clone half in CI | on-demand device acceptance |

Add a **frame-dump fixture set** under `tests/hw/frames/` (recorded SLIP streams for SONG,
PROJECT, LOAD browser, INSTRUMENT, PHRASE) so the perception layer has real, replayable inputs
without a device. This is the single most valuable offline asset and should land in Tier 0.

---

## 9. Phased plan & acceptance criteria

| Tier | Deliverable | Acceptance | Device? |
|---|---|---|---|
| **0** | ✅ Extract `M8Device` lib from `main_nav.cpp`; `tests/test_device_decode.cpp` with 28 offline replay tests | ✅ `m8_nav` behaves identically; replay tests decode fixtures correctly (121 assertions, 0 failures) | refactor offline |
| **1** | ✅ Screen identity + navigation graph (`ScreenModel.h`) + `gotoScreen`; per-screen field maps from clone layouts | ✅ `identifyScreen` + `computeRoute` pass offline; field maps cover 6 screens; hex-suffixed IDs (PHRASE FC, TABLE 0E) fixed and live-verified 2026-07-18 | offline + device |
| **2** | ✅ Gesture pinning (`--pin-gestures`) → `hw_buttons.json`; `Gestures.cpp` load/save; `editValue`/`enterNote`/`clearCell` implemented | ✅ every gesture in §5 confirmed by framebuffer oracle on fw 6.5.2; value convergence verified | **yes, completed** |
| **3** | ✅ `moveCursorTo`/`readField`/`editValue`/`enterNote`/`clearCell`; grid cursor detection fixed (color-only, header-row-excluded) | ✅ set & read back fields on device; **PHRASE/TABLE arbitrary step jumps live-verified 2026-07-18** (previously believed unreliable — root cause was an invisible non-cursor `<` glyph, not hardware key-repeat) | offline dev, **device confirmed** |
| **4** | ✅ `DeviceScriptRunner` (`--script FILE` on m8_nav); `loadFile` promoted to Primitives; `m8_diffcheck` tool | script passes on device; golden diff clean; all offline tests pass | CI + device |
| **4.5** | ✅ **DONE for 6.5.1/6.5.3/6.5.4; DONE-WITH-1-GAP for 6.5.2.** Reliability hardening: `gotoScreen` hop-verification, `moveCursorTo` field-landing, a first-class `dismissModal()`, `loadFile` rc=11 fix — see §6.5 | 6.5.1: 36/36. 6.5.2: all fields pass for `kInstrumentSamplerFields`/`kInstrumentMacrosynFields`/`kEffectsFields`/`kScaleFields`/`kProjectFields` (14/14, `SAMPLEROOT` gap resolved by removing the confirmed-nonexistent phantom field); only 3-of-11 `kMixerFields` reachable — 8 fields sit in a compound widget confirmed (via reproducibility testing) to have **non-deterministic, hidden-state-dependent navigation** that no fixed key sequence can reliably automate; investigated in depth, root cause understood, genuinely not fixable without an adaptive search or firmware insight neither of which is a quick fix. 6.5.3: 20/20. 6.5.4: 9/9. See §6.5 for full per-item results. | **done, device-verified** |
| **5** | Recipes: set-param, enter-phrase, SLICE probe, REPITCH/BPM sweep, parity capture — **recipe 1 DONE; recipe 3 partially unblocked, still blocked on out-of-scope audio issues; recipes 2/4/5 not attempted** | Recipe 1 (set-param): **3/3, fully passing** as of 2026-07-18 (second pass) — found and fixed a 4th driver bug (`readCursorValue` didn't strip the field label from `cursorMainText()`, corrupting numeric convergence checks) and a 5th (`editValue`'s stepping loop only ever incremented, never decremented, so a lower target was unreachable once values clamped at 0xFF). Recipe 3 (SLICE): driver-level mechanics (loading the probe, `keyjazz` note encoding) re-validated and a 6th bug fixed (`loadFile`'s ad-hoc PROJECT-climb loop replaced with the already-hardened `gotoScreen`) — but the recipe's actual audio-verification goal remains blocked on the `m8_makeprobe` amplitude bug and the low-note-silence finding, both confirmed non-driver issues out of this spec's scope. See §7's 2026-07-18 blocks (both the original and the Tier-4.5 follow-up) for full detail. | attempted, yes |
| **6** (stretch) | On-screen keyboard text entry (names, sample paths) | type & verify a song name on device | yes |

Tiers 0–4 are **complete and merged**, including three real bugs found and fixed via hardware
testing on 2026-07-18 (see the status block at the top). **Tier 4.5 is done** — see §6.5 for the
full per-item results; 6.5.1/6.5.3/6.5.4 hit their acceptance criteria cleanly, 6.5.2 hit it for
5 of 6 required field maps in full plus all-but-8 fields of the 6th (`kMixerFields`). The
`kMixerFields` gap was investigated in depth in a follow-up session: `SAMPLEROOT` (a separate,
smaller `kProjectFields` gap from the first pass) was resolved by confirming it doesn't exist on
this firmware and removing the phantom field; the remaining 8 `kMixerFields` entries were traced
to a compound widget whose navigation is confirmed non-deterministic (hidden per-widget state,
not a pure function of current field + key pressed) — genuinely not automatable with a fixed key
sequence, root-caused rather than left as an open question. **Tier 5 is partially unblocked**:
recipe 1 (set-param) now passes end-to-end; recipe 3 (SLICE)'s driver-level mechanics were
re-validated but its actual goal remains blocked on two audio/instrument issues outside this
spec's scope; recipes 2, 4, 5, and 6 have not been attempted at all.

---

## 10. Risks & hard-won constraints (do not relearn these)

- **No open-loop sequences.** ~150 ms key auto-repeat → always read-verify-act with settle waits
  and bounded retries. (Already the reason `navLoadFile` is closed-loop.)
- **No auto-home.** The device keeps its last screen; every routine must normalize from *unknown*.
- **Ambiguous headers.** `PROJECT` (settings) vs `LOADPROJECT` (browser) share a word — disambiguate
  on `LOAD`. Expect more such collisions; identity must be substring-precise, not naive.
- **Theme/firmware drift.** Cursor color and cell pitch vary; auto-detect both and guard on the
  `0xFF` firmware sysinfo. Gestures are pinned **per firmware** — re-pin on a version change.
- **Port is exclusive & Windows-only.** One consumer at a time (the user disconnects their display
  client first); non-Windows serial is a stub.
- **Edit gestures are unverified until Tier 2.** The single biggest correctness risk is guessing a
  mask. The framebuffer-oracle pinning method removes the guess — use it; never hardcode a
  hypothesized edit mask into a recipe.
- **Value convergence must be verified, not assumed.** Nudge-loops can overshoot/wrap; always
  re-read and stop on the observed value, cap iterations, and fail loudly with the screen attached.
- **Captures can be silently wrong.** The USB recording-level and decaying-amp gotchas
  (`hw-test-rig`) are prerequisites for recipes 3–5, not part of the driver — but a recipe should
  sanity-check capture peak and abort if it's ~100× low.

---

## 11. File map (actual)

```
src/tools/m8/M8Device.h/.cpp      # Layer 1: serial + SLIP + ScreenGrid + session control
src/tools/m8/ScreenModel.h        # screen enum, identifyScreen (with stripDigits), nav graph, field maps
src/tools/m8/Primitives.h/.cpp    # Layer 2: gotoScreen/moveCursorTo/readField/editValue/enterNote/clearCell/asserts
src/tools/m8/Gestures.h/.cpp      # GestureTable: load/save from hw_buttons.json, used by Primitives
src/tools/main_nav.cpp            # CLI tool: --goto-screen, --read-field, --dump-screen, --json, --keys, --pin-gestures
tests/test_device_decode.cpp      # 17 offline replay tests ([hwdecode], 71 assertions)
hw_buttons.json                   # key masks + firmware metadata + all pinned edit gesture masks
```

`m8_nav` stays serial-only. `m8_diffcheck` (Tier 4) not yet built. The `m8_device` static library
is linked from `m8_nav` and `m8_tests` via CMakeLists.txt.

---

## 12. Relationship to existing specs

- `M8_HARDWARE_TEST_SPEC.md` §8.2b — this spec is the general form of the Tier-3 navigator described
  there; `navLoadFile` becomes one recipe among many.
- `FX_COMMANDS_SPEC.md` — `enterNote`/`set` can author FX on device now that TBL/GRV/TIC round-trip
  and unknown FX are preserved (L12/L13).
- `M8_SAMPLER_COMPLETION_SPEC.md` + memory `sampler-slice-repitch-hw` — recipes 3–4 are the concrete
  way to capture the still-missing SLICE/REPITCH/BPM facts without guessing.
- `M8_UI_HARNESS_SPEC.md` (archived) — the clone-side script runner + VRAM shadow this spec reuses as
  the offline oracle and the shared script dialect.
```
