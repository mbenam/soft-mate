# m8_diffcheck

**Source:** [`src/tools/main_diffcheck.cpp`](../../src/tools/main_diffcheck.cpp) (190 lines)
**Build target:** `m8_diffcheck` (CMakeLists.txt, links `m8_device`)
**Category:** device-vs-golden differential testing — runs a `.m8script` against the real
device and checks the resulting screen against a stored text reference. This is the tool that
fulfills the "keep the clone honest against hardware" half of `M8_DEVICE_CONTROL_SPEC.md`'s
original Tier 6 goal.
**Links:** `m8_device` only. No engine, no SDL, no audio. Serial only.

> Built by the same device-control effort as [`m8_nav`](m8_nav.md) — see that doc's note about
> Tier 4.5 (`M8_DEVICE_CONTROL_SPEC.md` §6.5) reliability work in progress; anything driven
> through `.m8script` execution here shares `m8_nav`'s current reliability caveats, since both
> tools run scripts through the same `DeviceScriptRunner`.

## What it does

Runs a `.m8script` file against a real, connected M8 (via `DeviceScriptRunner`, the same script
executor `m8_nav --script` uses), captures the **final** screen after the script completes, and —
if a golden reference is given — diffs it cell-by-cell against that reference, reporting the
first character that differs. This is how you catch either (a) a real device-behavior regression,
or (b) a driver bug that makes the device diverge from what the script's author expected, without
needing to eyeball a screenshot.

**The comparison is glyph-only, not color-aware.** `dumpTextGrid()` strips everything except the
printable character at each cell — no foreground/background color, no cursor-highlight state.
Two screens that look identical in plain text but differ only in color (e.g. a cursor position,
or a theme difference) will **not** be flagged as a diff by this tool.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | yes | Serial port, e.g. `COM3`. |
| `--script <path>` | yes | The `.m8script` to run. Must complete with exit 0 (script pass) before any diff comparison happens. |
| `--golden <path>` | no | A **plain-text** grid file to compare the final screen against (see format below). If omitted, the tool just prints the final screen and exits 0 — no comparison performed. |
| `--save <path>` | no | Also write the final screen as JSON (cells with colors — via `ScreenGrid::printJson`, the *full* representation, unlike the glyph-only comparison). |
| `--hold-ms <n>` | `15` | Button hold duration passed through to the script runner. |

## Golden reference format

**Plain text**, one line per screen row, one character per column — exactly what `dumpTextGrid()`
produces and prints to stdout under `--- device screen ---`. This is **not** the same format
`--save` writes (`--save` is JSON with colors); if you want to create a golden file, capture a
known-good run's stdout output (the lines between `--- device screen ---` and the next blank
line / `MATCH`/`DIFF` marker) and save that as the golden `.txt` file, or adapt from an
equivalent glyph-only dump if you have one from another tool.

## Diff algorithm

`diffGrids()` walks both grids row by row, column by column (padding the shorter one with spaces
where needed), and returns a description of the **first** mismatch found:
`row R col C: expected '<golden char>' (0xHH), got '<device char>' (0xHH)`. If every character
matches but the two grids have a different number of rows, that's reported as a line-count
mismatch instead (checked only after a full character-by-character pass finds no content
difference). Only the *first* divergence is ever reported — this tool does not produce a full
diff, just enough to point at where things went wrong.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success — script passed, and either no `--golden` was given, or the final screen matched it |
| 1 | Unknown CLI arg; script ran but failed (its own exit code is propagated); or `--golden` was given and the final screen didn't match |
| 2 | Missing `--port`/`--script`; serial port open failure; script parse error |

## Examples

```
# Run a script, just show the final screen (no comparison)
m8_diffcheck --port COM3 --script test.m8script

# Run and compare against a golden reference
m8_diffcheck --port COM3 --script test.m8script --golden ref.txt

# Run, compare, and also save the full (colored) JSON representation
m8_diffcheck --port COM3 --script test.m8script --save out.json --golden ref.txt
```

## Gotchas

- **The script must fully pass before any diff happens.** If the script itself fails (an
  `assert_*` verb fails, a field can't be reached, etc.), `m8_diffcheck` propagates that failure
  and never reaches the golden comparison — a `DIFF` result specifically means "the script ran to
  completion but the final screen didn't match," not "something went wrong."
- **Color/cursor state is invisible to this tool.** Don't use it to verify which field the cursor
  landed on, or a color-only rendering change — only glyph/text content is compared.
- **Golden files are plain text, `--save` output is JSON** — they are not interchangeable formats
  despite both representing "a captured screen." Don't point `--golden` at a `--save` JSON file
  expecting it to work.
- Shares `m8_nav`'s `DeviceScriptRunner` execution engine, so any reliability issue affecting
  script execution there (see `m8_nav.md`'s Gotchas and `M8_DEVICE_CONTROL_SPEC.md` §6.5) affects
  this tool identically — a script that's flaky under `m8_nav --script` will be equally flaky here.
