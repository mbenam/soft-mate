# m8_editwatch

**Source:** [`src/tools/main_editwatch.cpp`](../../src/tools/main_editwatch.cpp)
**Build target:** `m8_editwatch` (CMakeLists.txt, links `m8_device`)
**Category:** hardware diagnostic for `M8_DRIVER_BUGS.md` #34 — watches the cursor while an edit
walk runs, to find out whether it moves.
**Links:** `m8_device` only. No engine, no SDL, no audio. Serial only.

## What it does

Replays the gesture `editValue` sends, one press at a time, with a
[`LiveReader`](../../src/tools/m8/LiveReader.h) sampling the cursor row between presses. Then it
walks the value back and verifies.

It does not call `editValue`. That is the point: `editValue` re-reads through the settle-gated
path, which returns only after the repaint finishes — by which time a cursor that slipped has
already slipped and the walk has continued on the new field. All that survives is the endpoint,
which is why #34 could be observed but not caught.

**It mutates the device.** `--allow-mutation` is required. It restores the field afterwards and
reports whether the restore verified.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | no (`COM3`) | Serial port. |
| `--field <LABEL>` | no (`AMP`) | The field to walk. |
| `--neighbour <LABEL>` | no (`LIM`) | The field a slip would land on. Watched for change. |
| `--gesture <name>` | no (`coarse-up`) | `coarse-up`, `coarse-down`, `fine-up`, `fine-down`. |
| `--steps <n>` | no (`16`) | Presses to send. |
| `--hold-ms <n>` | no (`40`) | Key hold duration. |
| `--sample-ms <n>` | no (`5`) | Cursor sampling interval between presses. |
| `--gap-ms <n>` | no (`60`) | Wait between presses. |
| `--allow-mutation` | **yes** | Required; the tool edits a field. |
| `--help` | no | Print usage and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The walk completed with the cursor never leaving the field. |
| 1 | The cursor moved, or the neighbour changed. #34 reproduced — the JSON names the iteration. |
| 2 | Setup failed: port, screen, field, or gestures not pinned. |

## Measured, fw 6.5.2, 2026-08-24 — a negative result

FMSYNTH instrument screen, `AMP` with `LIM` directly below it, which is #34's exact layout. Nine
configurations, `--steps 16`, coarse-up then coarse-down to restore:

| hold | gap | slips | neighbour moved | AMP reached | restored |
|---|---|---|---|---|---|
| 40 ms | 60 / 25 / 12 ms | 0 | no | FF every run | yes |
| 15 ms | 60 / 25 / 12 ms | 0 | no | FF every run | yes |
| 8 ms | 60 / 25 / 12 ms | 0 | no | FF every run | yes |

288 coarse presses across a 5× hold range and a 5× gap range, **zero cursor slips**, and `AMP`
reached `FF` every time rather than stopping short the way #34 reported (`FD`).

**What that means.** #34's suspected mechanism is a dropped EDIT modifier turning a coarse step
into a bare arrow. If that were simply a function of press timing, this sweep should have provoked
it — pressing harder and faster than `editValue` ever does, since `editValue` carries a
`readSettled(120, 200, 1200)` between every press. It did not. So whatever produces #34 is **not**
"a coarse press loses its modifier at speed", and a guard aimed at that mechanism would have been
aimed at the wrong thing — which is precisely what the bug entry warned about.

**What it does not mean.** A negative result over 288 presses does not prove the device never drops
a modifier; #34 was one event in a long session. It narrows the search, it does not close it.

## Gotchas

- **Run it from the repo root.** Gestures load from the bare relative path `hw_buttons.json`.
- **`--field` and `--neighbour` must be adjacent** for the neighbour check to mean anything. On
  FMSYNTH, `AMP` sits on the MOD1 row and `LIM` on MOD2 directly below — the #34 geometry. On
  SAMPLER the same labels sit on different rows.
- **It samples between presses, not just after them.** A slip that is corrected before the next
  press still happened, and one-sample-per-press would miss it.
- **If the restore does not verify**, nothing was written to the card — reloading the project
  restores it losslessly.
- **COM3 is exclusive.** Let any m8drv daemon exit first.

## Example

```powershell
build\Release\m8_editwatch.exe --port COM3 --field AMP --neighbour LIM --gesture coarse-up --steps 16 --allow-mutation
```
