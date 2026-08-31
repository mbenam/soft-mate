# capture_target

**Source:** [`tools/capture_target.py`](../../tools/capture_target.py) (Python 3, stdlib + [`m8drv`](m8drv.md)).
**Build target:** none. It coordinates [`m8drv`](m8drv.md) and [`m8_capture`](m8_capture.md), both of which must already work.
**Category:** deterministic navigation & capture tool.

## What it does

Navigates the M8 tracker hierarchy closed-loop (Song $\to$ Chain $\to$ Phrase or Chain $\to$ Phrase), validates each screen transition and cell coordinate, verifies the active transport state, and triggers an audio capture via `m8_capture.exe`.

## Addressing Modes

### 1. Song-Relative
Given a song row and track, reads the chain hex value from that cell, dives `SHIFT+RIGHT`, locates the requested phrase ID within that chain, and dives into the phrase screen:
```powershell
python tools/capture_target.py --song-row 0 --track 2 --phrase 12 --scope phrase --out cap_p12.wav
```

### 2. Chain-Direct
Navigates directly to a target chain ID, scans chain steps `00`–`0F` for the requested phrase ID, and dives into the phrase screen:
```powershell
python tools/capture_target.py --chain 0E --phrase 12 --scope phrase --out cap_c0e_p12.wav
```

## Playback Scopes

| Scope (`--scope`) | Description |
|---|---|
| `phrase` (default) | Dives into the target phrase screen and plays only that isolated 16-step phrase. |
| `chain` | Parks on the chain screen at the step containing the target phrase and plays the chain. |
| `song` | Parks on the song screen at the specified row and plays the song. |

## Closed-Loop Verification

1. **Header Inspection:** Verifies top header string on every transition (e.g. `SONG`, `CHAIN 0E`, `PHRASE 12`).
2. **Grid Coordinates:** Verifies active cursor step and column on grid screens.
3. **Empty Cell Protection:** Rejects empty cells (`--`) on song or chain screens with a clear error before attempting to dive.
4. **Transport State Verification:** Detects if transport is playing on entry and halts playback on `SONG` to prevent toggle inversion.
5. **Serial Exclusivity:** Scopes `M8Driver` (`m8_nav --serve`) session to navigate and verify, then releases `COM3` before invoking `m8_capture.exe`.

## Options

| Option | Meaning | Default |
|---|---|---|
| `--song-row <HEX/INT>` | Song grid row (`00`–`FF` or `0`–`255`). | None |
| `--track <0-7>` | Song track column index (`0`–`7`). | None |
| `--chain <HEX/INT>` | Target chain ID (`00`–`FF`). | None |
| `--phrase <HEX/INT>` | Target phrase ID (`00`–`FF`). | None |
| `--scope <phrase\|chain\|song>` | Playback scope. | `phrase` |
| `--out <PATH.wav>` | Destination WAV output file. | None |
| `--port <PORT>` | Serial port. | `COM3` |
| `--audio <FILTER>` | Audio device substring filter for capture. | `M8` |
| `--seconds <N>` | Recording duration in seconds. | `3.0` |
| `--dry-run` | Navigate, verify coordinates/headers, but skip audio capture. | `false` |
| `--verbose`, `-v` | Enable detailed diagnostic logging. | `false` |
| `--self-test` | Run built-in offline test suite with mock driver. | `false` |

## Offline Testing

The tool includes an offline mock driver (`MockM8Driver`) that simulates the tracker screen hierarchy, grid cells, and navigation state machine without requiring hardware:
```powershell
# Run built-in self-tests
python tools/capture_target.py --self-test

# Run unit test suite
python -m unittest tests/test_capture_target.py
```
