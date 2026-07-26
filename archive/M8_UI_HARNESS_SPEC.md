> **ARCHIVED — implemented, see status.md and tests/test_ui_scripts.cpp.**

# M8-SDL3 — UI Test Harness Spec

A scriptable test mode for `m8_clone` so the agent can drive the *application* — not just the
engine — enter keystrokes, dump the screen, and assert on what happened. Combined with the
existing audio tools, this closes the last gap: the agent can verify the whole app, correctness
end to end, with no human in the loop.

**Prerequisite:** none.

**Build this before the next feature.** From then on the agent tests its own UI work.

---

## The idea

The app already renders to a character grid and already has an input handler. This spec adds:

1. a `--script FILE` mode that feeds commands through the **real input handler** (not a
   simulation of it), and
2. the app's ability to **dump its own framebuffer as text** and as a PNG, and
3. **assertions** on that text.

Because commands go through the real input path and the real render, a passing script proves the
real app works — not a mock of it.

Deterministic, headless-friendly, no OS-level input injection, no window-focus games.

---

## Task 1 — `--script` mode

```
m8_clone --script tests/ui/load_and_save.m8script
m8_clone --script tests/ui/nav.m8script --headless --out-dir artifacts/
```

- `--headless`: run without showing a window if SDL allows it (offscreen render target), so CI
  can run it. If truly headless is hard on Windows, a hidden/minimized window is fine — the
  point is no human interaction, not literally no window.
- `--out-dir`: where screenshots and text dumps go.
- Exit code 0 if every assertion passed, 1 if any failed, 2 on a script/parse error.
- On any failed assertion, dump the current screen (text + PNG) automatically before exiting, so
  the agent sees the state at failure.

The script drives the app's own main loop step by step: apply the command, advance one (or N)
frames, repeat. **No wall-clock dependence** — `wait` is in frames or in engine-time, not real
milliseconds, so runs are deterministic and fast.

---

## Task 2 — the command grammar

Plain text, one command per line, `#` comments. Keep it small.

```
# input
key <BUTTON>[+<BUTTON>...]      one tap: key SHIFT+RIGHT   key X   key SPACE
hold <BUTTON> <frames>          hold for N frames (auto-repeat, for hex nudging)
type "<text>"                   feed characters to a text-input prompt (filenames)

# time (deterministic — frames or engine ticks, never real time)
wait <frames>                   advance N rendered frames
play                            start transport (= key SPACE, but explicit)
stop                            stop transport
render <seconds> <file.wav>     render the current song offline to a WAV (reuses m8_render path)

# files
load <path.m8s>                 load a song (through the real LOAD path)
save <path.m8s>                 save (through the real SAVE path)
set_sample_root <path>

# capture
screenshot <file.png>           PNG of the current framebuffer
dump_screen <file.txt>          the 40x30 character grid as text
dump_json   <file.json>         full state: chars + colors + cursor + brackets + sliders + overlay

# assertions  (fail -> auto-dump + exit 1)
assert_screen contains "<text>"        the char grid contains this substring
assert_screen row <n> contains "<text>"
assert_screen not_contains "<text>"
assert_cursor row <n> col <n>          cursor (highlighted-bg cell) is here
assert_playing                          transport is running
assert_stopped
assert_song_name "<name>"
assert_no_error                         no error/missing-sample overlay is up
assert_error contains "<text>"          an error overlay is up and says this

# layout / glitch assertions (need the JSON state)
assert_cell_color row <n> col <n> is <RRGGBBAA>   glyph colour at a cell
assert_no_overlap                       no two fields wrote the same cell this frame (Task 6)
assert_row_matches <n> "<regex>"        a row matches a format regex (Task 6)
assert_slider row <n> col <n> fill <0-8>
```

The button names match the app's own mapping: `UP DOWN LEFT RIGHT X Z SHIFT SPACE` (EDIT=X,
OPT=Z, SELECT=SHIFT, START=SPACE) — mirror whatever `m8_clone` already uses so scripts read the
same as the controls.

`dump_screen` is the workhorse: the agent reads the text, so it can assert on anything visible
without pixel analysis. `screenshot` is for the agent to *look* when a text assertion isn't
enough (layout, colour, the playhead highlight).

---

## Task 3 — a shadow grid (VirtualCell), dumped as text and JSON

The renderer already places characters on a grid. Add a **shadow buffer** that every draw call
also writes to, capturing not just the character but its colour and widget state. This is richer
than a bare char grid, and the extra fields are exactly what the glitch checks (Task 6) need.

```cpp
// Renderer.h
struct VirtualCell {
    char     ch       = ' ';
    uint32_t color    = 0x000000FF;   // RGBA of the glyph
    uint32_t bg       = 0x00000000;   // RGBA of the cell background (cursor detection)
    bool     bracket  = false;        // this cell is inside a [ ] bracket
    uint8_t  slider   = 0;            // slider fill 0..8, else 0
};
VirtualCell m_vram[30][40];
```

Wire it into the existing draw path:
- `drawString` / `drawChar` → write `ch` + `color` + `bg` to each cell it covers.
- `drawBracket` → set `bracket = true` on the bracketed cells.
- the slider/`fillRectPixel` path → compute the grid cell (`x/8`, `y/8`) and set `slider` to the
  fill width.

This costs nothing at runtime in normal mode (it's a couple of array writes per glyph) so it can
be always-on; no need to gate it.

**Two serialisations:**

- `dump_screen <file.txt>` — the 40×30 characters as 30 lines of text. Human- and
  agent-readable, the fast path for `assert_screen`.
- `dump_json <file.json>` — the full state, for assertions that need colour/cursor/widget data:

```json
{
  "screen": "PHRASE",
  "bpm": 120,
  "vram": ["PHRASE 00 ...", "... 30 rows ..."],
  "colors":  [[ "00FFFFFF", ... 40 per row ], ... 30 rows ],
  "cursor":  { "row": 3, "col": 2, "width": 3 },
  "brackets":[ {"row":3,"col":2,"width":3} ],
  "sliders": [ {"row":10,"col":22,"fill":6} ],
  "playheads":[ {"track":0,"row":0,"mode":"PHRASE"} ],
  "overlay": null
}
```

The cursor is derived from the cell(s) whose `bg` is the highlight colour — the app already knows
which cell is active, so emit it directly rather than making the reader infer it.

> This is the same virtual-grid idea as the headless `m8_client.py` text reconstruction, but the
> app emits its own state directly — no protocol decode, no pixel analysis. It is written to a
> **file** the agent reads, driven by a **script the app runs itself (Task 1)** — there is no
> Python wrapper, no live stdin/stdout pipe, no subprocess loop to babysit. Batch in, files out.

---

## Task 4 — the test scripts

Write these as `tests/ui/*.m8script`. They are the manual checklist, automated.

**nav.m8script** — reach every screen, assert its header.
```
key SHIFT+RIGHT
dump_screen artifacts/inst.txt
assert_screen contains "INST"
# ... one per screen: SONG CHAIN PHRASE INST TABLE PROJECT GROOVE SCALE MODS MIXER FX
```

**edit.m8script** — enter a note, confirm it lands.
```
# navigate to phrase 00 row 0
key X
# ... nudge to a note value ...
dump_screen artifacts/phrase.txt
assert_screen row 0 contains "C-4"
```

**load_each.m8script** — load all four example files, assert each loads.
```
load third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s
assert_no_error
load third_party/m8-files-cxx/examples/songs/TEST-FILE.m8s
assert_song_name "TEST-FILE"
```

**save_reload.m8script** — the round-trip that matters most.
```
load .../V4EMPTY.m8s
# make an edit
key X
save artifacts/roundtrip.m8s
load artifacts/roundtrip.m8s
assert_screen contains "..."      # the edit survived
```
(Byte-identity is already covered by L4; this proves the *UI* save/load path works.)

**pre40_refuses.m8script** — save must refuse a pre-4.0 song cleanly.
```
load .../DEFAULT.m8s
save artifacts/should_fail.m8s
assert_error contains "4.0"
```

**missing_samples.m8script** — load a song whose samples aren't found.
```
set_sample_root artifacts/empty_dir
load <a song referencing samples>
assert_error contains ".wav"          # the missing-sample overlay lists them
```

**playhead.m8script** — the bug we found by eye once.
```
load .../TEST-FILE.m8s
play
wait 120
dump_screen artifacts/playing.txt
assert_playing
# the highlighted row should have advanced from 0
```

**live_vs_offline.m8script** — see the audio spec companion below.

---

## Task 5 — live-vs-offline audio parity

The one audio path never checked: does the *running app* produce the same samples as
`m8_render`? If the live callback and the offline render diverge, that is a real bug and nothing
currently catches it.

Two ways, pick the cheaper:

**(a) In-app render assertion.** `m8_clone --script` gains `render <sec> <wav>`, which runs the
engine through the *same* `render()` the audio callback uses, offline, from the app process.
Diff that against `m8_render`'s output of the same song. If they match sample-for-sample, the
app and the tool share one truthful path. (They should — both call `Engine::render` — so this
mostly guards against the app doing something extra in its callback.)

**(b) Capture the live output.** Run the app playing a song; capture its audio out with the
miniaudio path from `m8_capture` (loopback or a physical cable); `m8_analyze` the capture. Proves
the real driver path is clean. Heavier, needs an audio device, but it's the only thing that
tests the actual SDL callback under a real driver.

Do (a) as an automated test. Do (b) once, by hand, as the real-load spot-check (see `TESTING.md`).

---

## Task 6 — glitch detection

Because the shadow grid carries characters, colours, brackets and sliders, layout bugs become
deterministic assertions — no OCR, no flaky pixel diffs. Three categories, all cheap:

**Overlap / truncation.** During a frame's draw, track which cells were written and by which
field. If two fields write the same cell, that is a layout collision (the classic case: a
12-char instrument name bleeding into the adjacent `DRY` column). `assert_no_overlap` flags it.
Implement by tagging each `drawString` with a field id in agent/test mode and checking for
double-writes.

**Colour context.** The active cursor cell must actually carry the highlight/label colour. A
field that is logically selected but not visually highlighted is a real bug the char grid alone
won't catch. `assert_cell_color` covers it.

**Format validation.** Run a regex over a row to confirm values didn't corrupt — e.g. the note
column is always `[A-G][#-]?[0-9]` or `---`, a hex field is always two hex digits or `--`.
`assert_row_matches` covers it. A `dump_json` plus a regex sweep catches a whole class of
"the value rendered as garbage" bugs.

These run in the same scripts as everything else and cost nothing on a good build.

---

## Task 7 — the closed loop: generate → render → analyze → fix

The payoff of having UI control *and* the audio tools in one agent-driven flow: the agent can
build a patch through the real UI, render it, detect that it made something glitchy, and go back
into the UI to fix it — no human, no guessing.

```
# agent-authored script fragment
load .../V4EMPTY.m8s
# ... navigate to instrument 00, set MacroSynth, crank RES and AMP ...
save artifacts/patch.m8s
render 10 artifacts/patch.wav          # offline, via the shared render path
```

Then the agent runs, outside the script:

```
m8_analyze artifacts/patch.wav        # exit 1 if it clips / DC / blows up
```

If `m8_analyze` reports `FAIL clipped` or `FAIL |DC|`, the agent knows the patch it built through
the UI is glitchy (e.g. RES + AMP too high causing a blow-up), and drives the UI to back the
values down and re-render. This is the whole toolchain working as one loop: **UI correctness and
audio correctness checked together, by the agent, end to end.**

This is also how the agent can *generate* test songs — build them through the real UI (which
proves the UI behaves while editing), save, render, verify — rather than hand-writing `.m8s`.

---

## Acceptance

- `m8_clone --script tests/ui/nav.m8script --headless` exits 0 and produces a text dump per
  screen.
- Each script above runs in CI-style (no human), exits 0 on a good build.
- Break something deliberately (e.g. mis-route the playhead) → the relevant script goes red and
  auto-dumps the failing screen.

---

## Order

1. Task 3 — the VirtualCell shadow grid + `dump_screen` / `dump_json` (small; the grid exists).
2. Task 1 + 2 — `--script` mode and the grammar.
3. Task 4 — the scripts: `nav`, `load_each`, `save_reload`, `pre40_refuses` first.
4. Task 6 — the glitch assertions (overlap / colour / regex). Cheap once the shadow grid exists.
5. Task 5a — the live-vs-offline render assertion.
6. Task 7 — wire the closed loop into an agent workflow (mostly documentation + one example
   script; the pieces already exist).

CMake: no new target — this is a mode of `m8_clone`. Guard the script reader so it adds no
runtime cost to normal launches.
