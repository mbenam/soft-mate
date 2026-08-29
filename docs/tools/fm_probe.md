# fm_probe — driving the FMSYNTH screen

**Source:** [`tools/fm_probe_map.py`](../../tools/fm_probe_map.py),
[`tools/fm_patch.py`](../../tools/fm_patch.py),
[`tools/fm_run.py`](../../tools/fm_run.py),
[`tools/fm_pitch.py`](../../tools/fm_pitch.py) (Python 3; `fm_pitch` needs numpy).
**Build target:** none. They drive [`m8drv`](m8drv.md) and [`m8_capture`](m8_capture.md).
**Findings:** [`hw_findings.md`](hw_findings.md) §UI-30.

`ScreenModel.h` has no FM field map, so `cursor <FIELD>` cannot reach `ALGO`, the
per-operator `RATIO`/`LEV`/`FB`, the MOD routing or the `MOD1`..`MOD4` amounts.
[`m8_makeprobe`](m8_makeprobe.md)'s `--fm-*` flags bake the patch into a file
instead, but **loading it needs the SD card and there is no file transfer over
serial** (§UI-7). These four scripts build the patch on the device.

## `fm_probe_map.py` — make each cell name itself

The FM screen reports the same cursor position at every stop along a row:
walking right along `LEV/FB` gives `(120, 8)` eight times. Position reads cannot
map it.

So don't read the position — **press a value key and see which number on the
screen moved.** This increments by +16, dumps, decrements by −16, and dumps
again to prove it went back.

```powershell
python tools/fm_probe_map.py --down 6 --left 4 --right 0 1 2 3 4 5 6 7
python tools/fm_probe_map.py --down 7 --left 5 --right 0 1 2 3 --key 0x05 --undo 0x81
```

`--key`/`--undo` swap the value gestures for enum ones on enum cells.

The map it produced, from a cursor homed on `TYPE`:

| Path | Cell |
|---|---|
| `DOWN*3 LEFT*2 RIGHT*0` | `ALGO` |
| `DOWN*6 LEFT*4 RIGHT*n` | n=0 LEV A, 1 FB A, 2 LEV B, 3 FB B, 4 LEV C, 5 FB C, 6 LEV D, 7 FB D |
| `DOWN*7 LEFT*5 RIGHT*n` | MOD slot 1 of operator A/B/C/D |
| `DOWN*9 LEFT*2 RIGHT*0` | `MOD1` amount (`RIGHT*1` is `AMP`) |

## `fm_patch.py` — build and verify the patch

```powershell
python tools/fm_patch.py --setup            # ALGO 0B, operator A alone, MOD1 -> PIT
python tools/fm_patch.py --mod1 40          # then sweep just the amount
python tools/fm_patch.py --mod-a1 -----     # the control: routing off
```

Every step re-reads the row it just changed; nothing is assumed to have landed.
It prints the whole patch plus the send routing at the end.

## `fm_run.py` — one guarded capture

Reuses `rt60_run.py`'s snapshot and diff, pointed at INSTRUMENT and MIXER. Sets
`MOD1`, asserts the screens, captures, asserts again, and renames the WAV
`.DRIFTED` if anything moved.

```powershell
python tools/fm_run.py --out hwtest_out/fm/pit40.wav --mod1 40 `
    --expect "INSTRUMENT:MOD     1>PIT ----- ----- -----"
```

## `fm_pitch.py` — the fundamental, to a fraction of a cent

Three estimators, printed side by side: interpolated autocorrelation,
zero-crossing rate, and an interpolated FFT peak with the fraction of energy
sitting under it.

Three, because two of them are wrong in ways that look right:
`m8_analyze --pitch` returned `0.000 Hz` on these captures outright;
`pitch_windows.py` picks an integer lag, a 5-cent grid at middle C, which cannot
answer "did this come back at exactly the played note?"; and plain
autocorrelation locks onto a subharmonic as soon as the tone gets high — at
`MOD1 0x40` it reported 959 Hz for a tone the other two both put at 10548.09 Hz.

```powershell
python tools/fm_pitch.py hwtest_out/fm/pit*.wav --ref 261.63
```

## Gotchas

- **The DOWN chain remembers the horizontal position per row.** A bare `DOWN*6`
  lands wherever that row was last left, not at its first cell. Saturate LEFT
  before counting RIGHT — every path above does.
- **The enum lists do not wrap.** Stepping forward from `1>PIT` runs up to
  `4>FBK` and stops there, so a one-directional walk can never reach `-----`
  again. `fm_patch.py` reverses when the value stops changing.
- **Changing instrument `TYPE` raises `LOSE CHANGES TO INSTRUMENT?`**, which
  needs two `EDIT` presses, the first appearing to do nothing.
- **`MOD1`'s cell is one RIGHT away from `AMP`.** Overshooting there edits the
  instrument's amp drive, which is silent on the screen you are watching.
