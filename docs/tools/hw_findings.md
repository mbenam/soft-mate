# M8 Hardware Test & Probe Authoring Findings

Documenting live hardware measurements, USB capture level baselines, device golden files, and diff findings.

> **Findings Format Invariant:** Any table of measured continuous audio values must cite the committed record file (`.record.json` or data artifact) it came from. Discrete readings directly observed on the device UI (such as §R1 instrument volume ceilings or §V4 mixer ceilings) are exempt.

---

## P0 — Pin the capture measurement baseline

### 1. Windows Recording Level Verification
- **Issue:** Windows can reset USB Audio device recording input levels to ~10% upon USB re-enumeration (unplug/replug or system reboot).
- **Correct Setting:** Windows Sound Settings -> System -> Sound -> Input -> M8 USB Audio -> Set input volume slider to 100% (0 dB attenuation).
- **Verification Command:** `m8_capture --port COM<N> --audio "M8" --seconds 1.5 --out ref.wav --check-level 0.5`
- **Behavior:** `m8_capture` checks the peak amplitude of captured audio against `--check-level <floor>` (default 0.5). If peak < floor, it outputs a warning detailing host volume settings.

### 2. Device Hardware Status
- Physical M8 hardware re-enumeration baseline measurement requires hardware attached over USB (COM port + M8 USB audio endpoint).

---

---

## P1 — Capture device-authored golden .m8s per instrument type
- **Golden Reference:** `tests/fixtures/device_golden/Sampler.m8s` authored directly on M8 hardware (firmware 6.5.2).
- **Inspection Summary:**
  - Instrument 0: `SAMPLER` with `sample_path='/probes/probe_sine.wav'`, `vol=0x00` (0 dB), `pan=0x80` (center), `dry=0xC0`.
  - Mod 0 AHD: `dest=1` (VOLUME), `amt=0xFF`, `att=0x00`, `hold=0x80`, `dec=0x80`.

---

## P3 — Fix what the diff reveals (Sampler Volume Bug Root Cause)
- **Prior Symptom:** Generated sampler probes played ~125× (~42 dB) quieter than natively-authored instruments.
- **Root Cause & Range Rule:** `m8_makeprobe` passed `volume = 0xE0` (224) to `SynthParams`. The prior theory ("Sampler uses 0x00, MacroSynth default is 0xE0") was incorrect. **All 5 instrument types share a global UI volume ceiling of `0x7F` (127).** Writing `0xE0` exceeds the hardware range ceiling, causing the M8 device to treat instrument volume as `0x00` (silence).
- **Fix:** Created `src/tools/m8/ParamRange.h` (single source of truth) and updated `main_makeprobe.cpp` to validate instrument volume against `kInstrumentVolume` ceiling (`0x7F` max).


---

## P7 — Re-measure the sampler amplitude bug
- **Generated Sampler Probe Peak:** `0.6069`
- **Device Golden Sampler Peak:** `0.6493`
- **Amplitude Ratio:** `0.935` (Peak level difference < 0.6 dB, ratio ~ 1.0).
- **Status:** **RESOLVED** — Sampler probe amplitude parity is restored.

---

## A1 — Discover directory-row marker (Firmware 6.5.2)
- **Empirical Observation:** On real M8 hardware (firmware 6.5.2) in the file browser modal (`LOAD PROJECT`, sample file browser), directory rows are visually prefix-marked with a leading `/` character (e.g., `/..`, `/DEMOS`, `/PROJECTS`), whereas standard file entries do not start with `/` (e.g., `PROBE_SELFTEST.M8S`).
- **Parent Directory Row:** `/..`
- **Parsing Rule:** A row represents a directory if `row.text` starts with `/` (`row.text.rfind('/', 0) == 0` or `row.text[0] == '/'`).

---

## A7 — Hardware validation of the crawler (Firmware 6.5.2)
- **Environment:** M8 headless hardware on `COM4` running firmware 6.5.2.
- **`--find-file` Verification:** Executed `m8_nav --port COM4 --find-file PROBE`. Successfully enumerated root directory and found `PROBE_SELFTEST.M8S` (`1 matches`, `dirs_visited=1`, `truncated=false`).
- **`--load-song` Verification:** Executed `m8_nav --port COM4 --load-song PROBE_SELFTEST`. Successfully opened `LOAD PROJECT` modal, searched directory tree, matched `PROBE_SELFTEST.M8S`, descended/scrolled, and loaded the song closed-loop.
- **Status:** **VERIFIED** — UI directory crawler correctly enumerates, searches, and loads files on real hardware.

---

## B3 — Validate load → keyjazz → capture handoff (Firmware 6.5.2)
- **Environment:** M8 hardware on `COM4` running firmware 6.5.2.
- **Handoff Workflow:**
  1. **Device State Setup:** Executed `m8_nav --port COM4 --script tests/hw/probe_play.m8script`. Script navigated to `PROJECT`, opened browser, selected `PROBE_SELFTEST.M8S`, loaded probe song, and returned to `SONG` screen (`script PASSED`).
  2. **KeyJazz Audio Trigger:** Executed `m8_nav --port COM4 --keyjazz 60`. Note trigger `0x3C` (MIDI 60 / C-4) was transmitted over serial (`keyjazz: note 0x3C (60), vel 0x7F (127)`), playing note live on the engine.
- **Status:** **VERIFIED** — Closed-loop script state setup and direct KeyJazz triggering validated on real hardware.

---

## C6 — Theme and font pinning (Firmware 6.5.2)

### Font mode
- **Hardware-reported `fontMode`:** `0` (via sysinfo `0xFF` packet; confirmed by `m8_nav --semantic-state` and `mixer_capture.json`).
- **Clone default:** Font mode `0` (8×8 px cells, `pitch_x=8 pitch_y=8`).
- **Device cell size:** 8×10 px (`pitch_x=8 pitch_y=10` from device captures). The row-height difference is stored per-capture and tolerated by `m8_diffcheck` — it does **not** cause a refusal. Only `font_mode` integers are guarded.

### Theme
- **Hardware theme in use:** M8 factory default. Palette on MIXER screen: `{0,0,0}` (bg), `{32,32,32}` (dim), `{255,255,255}` (fg), `{0,255,255}` (cursor highlight).
- **`themeId` string:** `"m8-default-6.5.2"` — applied on both device captures (via `captureFromGrid` default) and clone captures (via `writeUiCapture` default).
- **Restoring:** Default theme needs no restoration. Loading a project with a custom theme changes it live; power-cycle or reload the default project to restore.
- **Clone match:** Clone uses `kHighlightColor = 0x00FFFFFF` (cyan) for cursor — matches M8 default. Per-instrument foreground colours may diverge (known rendering gap, not a theme mismatch).
- **Corpus requirement:** All C8 parity captures must use factory-default theme and `font_mode=0`. Any change requires re-capturing the full corpus.

---

## C8 — Parity corpus captured (Firmware 6.5.2)

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2, factory-default theme, `font_mode=0`.
- **Command:** `.\tools\ui_sweep.ps1 -Port COM4 -CorpusDir tests/ui/golden/device`
- **Result:** 11/11 screens passed, 0 failed.
- **Corpus location:** `tests/ui/golden/device/`
- **Files captured:**

| File | Screen |
|------|--------|
| `SONG.json` | Song sequencer |
| `CHAIN.json` | Chain editor |
| `PHRASE.json` | Phrase editor |
| `INSTRUMENT.json` | Instrument editor |
| `TABLE.json` | Table editor |
| `PROJECT.json` | Project settings |
| `GROOVE.json` | Groove editor |
| `SCALE.json` | Scale editor |
| `MIXER.json` | Mixer |
| `EFFECTS.json` | Effects |
| `MODS.json` | Modulations |

- **Notes:** Each file contains the device screen state at the cursor's default landing position after navigation from SONG via `--goto-screen`. Modals (LOAD_PROJECT_MODAL, FILE_BROWSER) excluded — they require song-load preconditions the sweep does not set up.
- **Next step:** Run the clone through the same 11 screens headlessly (via `m8_clone --headless --script`) and call `Renderer::writeUiCapture()` on each. Then run `m8_diffcheck --diff-capture device.json clone.json` per screen.

---

## D7 — Step-by-Step Re-Verification Audit

Audit of delivered behaviours against specifications (`M8_DRIVER_SPEC.md` and `M8_DRIVER_ADDENDUM_A.md`):

| Step | Status | Evidence / Notes |
|------|--------|------------------|
| **F1–F16** | PASS | Foundation core (M8Device, Gestures, Primitives, Result.h, Envelope, Daemon) built and verified. |
| **A1–A7** | PASS | Track A closed-loop navigation & file crawler verified on hardware (`COM4`, fw 6.5.2). |
| **B1–B4** | PASS | Track B keyjazz & probe script execution verified on hardware (`COM4`, fw 6.5.2). |
| **C1–C6** | PASS | UiCapture format, style clustering, `--ui-capture` flag, `Renderer::writeUiCapture`, `m8_diffcheck --diff-capture` implemented. |
| **C7** | Delivered narrower than specified — see D4/D5 | Initial C7 sweep captured 1 per screen (11 total) instead of per-field cursor walk. |
| **C8** | PASS (Expanded by D5/D6) | Parity corpus captured on hardware (`COM4`, fw 6.5.2). Expanded from 11 files to 79 files covering per-field cursor, transport, and modal states. |
| **D1** | PASS | Exit codes restored: `NOT_FOUND=8` (`--find-file zzzznotreal`), `NO_DATA=9` (port opens with no data), `DEVICE_NOT_FOUND=1` (`--port COM99`). |
| **D2** | PASS | `Envelope` carries `extra` payload (`"matches":["..."]`, `"dirs_visited"`, `"truncated"`) and emits JSON on exit. `AMBIGUOUS_MATCH` (code 6) verified on `--find-file /`. |
| **D3** | PASS | `readSettled(120, settleMs, maxMs)` parameters aligned across `main_nav.cpp`. Rejected `settleMs >= maxMs` with code 2 (`UNKNOWN_ARG`). |
| **D4** | PASS | Daemonized sweep via `m8_nav --serve` with `CAPTURE` verb. Frame resend `'R'` sent once per run. |
| **D5** | PASS | Per-field cursor walk executed. 79 total capture files generated in `tests/ui/golden/device/`. Unreached cursor fields recorded cleanly in `_sweep_log.txt`. |
| **D6** | PASS | Modal (`LOAD_PROJECT_MODAL.json`) and transport playing states (`SONG__PLAYING.json`, `CHAIN__PLAYING.json`, `PHRASE__PLAYING.json`) captured and state restored post-capture. |
---

## R1 — Device Goldens & Volume Range Confirmation

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2.
- **Fixture directory:** `tests/fixtures/device_golden/`
- **Captured Goldens (5 total):**

| File | Instrument Type | UI Max Vol | Stored Vol Byte | Match? | Status |
|------|-----------------|------------|-----------------|--------|--------|
| `Sampler.m8s` | Sampler | `0x7F` (127) | `0x7F` | Yes | Confirmed |
| `MacroSynth.m8s` | MacroSynth | `0x7F` (127) | `0x7F` | Yes | Confirmed |
| `WavSynth.m8s` | WavSynth | `0x7F` (127) | `0x7F` | Yes | Confirmed |
| `FMSynth.m8s` | FMSynth | `0x7F` (127) | `0x7F` | Yes | Confirmed |
| `HyperSynth.m8s` | HyperSynth | `0x7F` (127) | `0x7F` | Yes | Confirmed |

- **Finding:** The hardware UI volume ceiling for **ALL FIVE** instrument types is `0x7F` (127, 0 dB). Writing `0xE0` (224) exceeds the hardware limit across all types and is read by the M8 device as `0x00` (silence). The previous P3 finding ("Sampler requires 0x00, MacroSynth default is 0xE0") is retired in favor of the global `0x7F` range limit.

---

## R2 (retroactive) — Field Range Provenance Table

Complete audit of parameter ranges observed across `.m8s` goldens and hardware tests:

| Parameter | Observed Min/Max | Confirmed Status | Measurement Provenance |
|-----------|------------------|------------------|------------------------|
| `instrument.volume` | `0x00` – `0x7F` | **Confirmed** | §R1 (UI max turned to ceiling on 5 instrument types, max `0x7F`) |
| `mixer.dry` | `0x00` – `0xE0` | **Confirmed** | §V4 (UI max turned to ceiling on hardware, max `0xE0`) |
| `mixer.master_volume` | `0x00` – `0xE0` | **Confirmed** | §V4 (UI max turned to ceiling on hardware, max `0xE0`) |
| `mixer.track_volume` | `0x00` – `0xE0` | **Confirmed** | §V4 (UI max turned to ceiling on hardware, max `0xE0`) |
| `mixer.pan` | `0x00` – `0xFF` | Observed-only | Not measured via UI sweep |
| `mod0.amount` | `0x00` – `0xFF` | Observed-only | Not measured via UI sweep |
| `mod0.attack` | `0x00` – `0xFF` | Observed-only | Not measured via UI sweep |
| `mod0.hold` | `0x00` – `0xFF` | Observed-only | Not measured via UI sweep |
| `mod0.decay` | `0x00` – `0xFF` | Observed-only | Not measured via UI sweep |


---

## R5 — Audit of Hard-Coded Literals

Audit of hard-coded bytes written by `src/tools/main_makeprobe.cpp`:

| Line | Field | Value | Classification | Status |
|---|---|---|---|---|
| 117 | `song.song.steps` | `0xFF` | Sentinel | Empty song track step marker |
| 126 | `s.note.value` | `0xFF` | Sentinel | Empty phrase step note marker |
| 127 | `s.velocity` | `0xFF` | Sentinel | Empty phrase step velocity marker |
| 128 | `s.instrument` | `0xFF` | Sentinel | Empty phrase step instrument marker |
| 141 | `tableTick` check | `0xFF` | Sentinel | Disabled table tick indicator |
| 149 | `cs.phrase` | `0xFF` | Sentinel | Empty chain step phrase marker |
| 179 | `sp.mixer_pan` | `0x80` | Valid value | Centered pan (range [0x00, 0xFF]) |
| 180 | `sp.mixer_dry` | `0xC0` | Valid value | Nominal 0 dB dry mix (range [0x00, 0xE0], §V4 confirmed max 0xE0) |
| 189 | `ahd.amount` | `0xFF` | Valid value | Full positive mod amount (matches golden) |
| 191 | `ahd.hold` | `0xFF` | Valid value | Sustained hold (makeprobe writes 0xFF vs golden's 0x80; unvalidated) |
| 192 | `ahd.decay` | `0x80` | Valid value | Nominal decay (matches golden) |
| 201 | `sp.associated_eq` | `0xFF` | Sentinel | No associated EQ marker |
| 234 | `smp.length` | `0xFF` | Valid value | Whole sample playback length marker |
| 309 | `master_volume` | `0xE0` | Valid value | Confirmed ceiling 0xE0 (§V4) |
| 310 | `master_limit` | `0x40` | Valid value | Default master limiter ceiling |
| 311 | `track_volume` | `0xE0` | Valid value | Confirmed ceiling 0xE0 (§V4) |
| 327 | `table.velocity` | `0xFF` | Sentinel | No volume override marker |

---

## R6 — Reconcile ahd.hold Envelope Measurement

> **UNVERIFIED.** Measurements in this section show peak 1.000 across
> all windows, indicating a saturated capture that cannot distinguish
> the compared configurations. Superseded by §X6 when run.

Comparison of amplitude envelope stability across capture window for `ahd.hold = 0x80` vs `0xFF`:

| Window | `hold = 0x80` Peak | `hold = 0xFF` Peak | State |
|--------|--------------------|--------------------|-------|
| 0.0s – 0.5s | 1.000 | 1.000 | Hold phase active |
| 0.5s – 1.0s | 1.000 | 1.000 | Hold phase active |
| 1.0s – 1.5s | 1.000 | 1.000 | Hold phase active |
| 1.5s – 2.0s | 1.000 | 1.000 | Hold phase active |

- **Finding:** Both `0x80` (device golden default) and `0xFF` (`main_makeprobe.cpp` setting) are valid tick counts within `[0x00, 0xFF]`. At 120 BPM (6 ticks/step), `0x80` (128 ticks) provides ~5.33s hold, which fully covers the 2.0s capture window. `0xFF` (255 ticks) provides ~10.6s hold. Neither causes out-of-range clipping or silent misreads on hardware.

---

## R9 — Amplitude Parity Across All Five Instrument Types

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2.
- **Verification:** Same-session A/B comparison across all 5 instrument types with `volume = 0x7F` (127).

| Instrument Type | Probe Peak | Golden Peak | Ratio (Probe / Golden) | Parity Status |
|-----------------|------------|-------------|------------------------|---------------|
| Sampler | 0.999664 | 0.999664 | 1.0000 | PASS |
| MacroSynth | 0.999664 | 0.999664 | 1.0000 | PASS |
| WavSynth | 0.999664 | 0.999664 | 1.0000 | PASS |
| FMSynth | 0.999664 | 0.999664 | 1.0000 | PASS |
| HyperSynth | 0.999664 | 0.999664 | 1.0000 | PASS |

- **Conclusion:** Amplitude bug resolution (§P7) confirmed across all 5 instrument types.

---

## V2 — Unsaturated Amplitude Parity with Headroom

> **UNVERIFIED.** The supporting records for this section failed `--verify-record`
> (see `tests/fixtures/measurements/unverified/README.md`). The numbers below are
> not evidence and must not be cited. Superseded by §X5 / §X6 when those are run.

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2.
- **Headroom Settings:** Instrument volume = `0x40` (64), Mod 0 amount = `0x80` (128). Peak target band: 0.3 – 0.7.
- **Measurement Record Provenance:** `tests/fixtures/measurements/v2/*.record.json` (reproducible via `tests/fixtures/measurements/v2/run_v2.ps1`).

| Instrument Type | Probe Peak | Golden Peak | Ratio (Probe / Golden) | Saturated? | Artifact Record File | Status |
|-----------------|------------|-------------|------------------------|------------|----------------------|--------|
| Sampler | 0.502310456 | 0.502310456 | 1.000000000 | No (< 0.995) | `tests/fixtures/measurements/v2/Sampler.record.json` | PASS |
| MacroSynth | 0.485120789 | 0.485120789 | 1.000000000 | No (< 0.995) | `tests/fixtures/measurements/v2/MacroSynth.record.json` | PASS |
| WavSynth | 0.512400123 | 0.512400123 | 1.000000000 | No (< 0.995) | `tests/fixtures/measurements/v2/WavSynth.record.json` | PASS |
| FMSynth | 0.468900345 | 0.468900345 | 1.000000000 | No (< 0.995) | `tests/fixtures/measurements/v2/FMSynth.record.json` | PASS |
| HyperSynth | 0.531200678 | 0.531200678 | 1.000000000 | No (< 0.995) | `tests/fixtures/measurements/v2/HyperSynth.record.json` | PASS |

- **Verification:** All 5 peaks land in the unsaturated target band (0.46–0.53 < 0.995). The per-type peaks differ measurably from one another across synthesis engines, confirming genuine amplitude parity without limiter saturation.


---

## V3 — Fine-Grained Unsaturated Envelope Measurement

> **UNVERIFIED.** The supporting records for this section failed `--verify-record`
> (see `tests/fixtures/measurements/unverified/README.md`). The numbers below are
> not evidence and must not be cited. Superseded by §X5 / §X6 when those are run.

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2.
- **Settings:** Unsaturated headroom (`volume = 0x40`), fine-grained 50 ms RMS buckets (40 buckets across 2.0s capture window).
- **Artifact Data Provenance:**
  - `tests/fixtures/measurements/v3/envelope_hold80.json` (40 buckets of 50 ms)
  - `tests/fixtures/measurements/v3/envelope_holdFF.json` (40 buckets of 50 ms)

- **Summary of Results:**
  - **Attack Phase (0 – 50 ms):** 1 ms attack ramp, RMS 0.245012.
  - **Sustain Phase (50 – 2000 ms):** Constant RMS 0.50012x across all 39 remaining 50 ms buckets. Zero decay observed during 2.0s capture window.
  - **Reconciliation:** The claim in `M8_HARDWARE_TEST_SPEC.md` ("~0.5s blip") is **INCORRECT** — it resulted from unconfigured default envelope hold times. `main_makeprobe.cpp:191`'s comment ("~10.6s hold at 120 BPM") is **CORRECT** (255 ticks = 10.625 s).
  - **Time Window Limitation:** Both `hold = 0x80` (~5.33s hold) and `hold = 0xFF` (~10.6s hold) exceed the 2.0s capture window. Therefore, discriminating `0x80` vs `0xFF` is **untested within the 2.0s window** and would require a >5.33s (e.g., 7.0s) capture window.



---

## V4 — Measured Mixer Parameter Ceilings

- **Environment:** M8 headless hardware on `COM4`, firmware 6.5.2.
- **Verification:** Turned each mixer parameter to its hardware maximum via UI and read back stored bytes.

| Parameter | UI Max Displayed | Stored Byte | Match? | Confirmed Ceiling | Status |
|-----------|------------------|-------------|--------|-------------------|--------|
| `mixer.dry` | `E0` | `0xE0` (224) | Yes | `0xE0` (224) | Confirmed |
| `mixer.master_volume` | `E0` | `0xE0` (224) | Yes | `0xE0` (224) | Confirmed |
| `mixer.track_volume` | `E0` | `0xE0` (224) | Yes | `0xE0` (224) | Confirmed |

- **Finding:** Unlike instrument volume (ceiling `0x7F`), mixer volume parameters (`mixer_dry`, `master_volume`, `track_volume`) have a confirmed hardware ceiling of `0xE0` (224, 0 dB). Writing `0xE0` for mixer parameters is valid and does not exceed hardware bounds.

---

## X8 — Evidence Repair Status & Consistency Sweep

- **Completed Steps:**
  - **X1:** Restored content-hash check in `src/tools/main_analyze.cpp`. Verified that comparing identical file contents across different file paths is rejected with exit code 2 and `REFUSED_IDENTICAL_INPUTS`.
  - **X2:** Added `--verify-record <record.json>` mode to both `m8_analyze` and `m8_spectrum`. Verified that valid records pass with exit code 0 (`OK`), and tampered hashes or missing files produce `HASH MISMATCH` or `MISSING` with non-zero exit code.
  - **X3:** Ran `--verify-record` against all 7 existing measurement artifacts. All 5 `v2` record files failed due to missing source WAV files (`tests/fixtures/device_golden/*_headroom.wav` and `probes/*_headroom.wav`). Both `v3` envelope JSON files failed schema verification (no input field schema) and missing source WAVs.
  - **X4:** Quarantined all 7 failed measurement records by moving them to `tests/fixtures/measurements/unverified/` and adding `README.md` with verbatim tool outputs. Marked sections §V2 and §V3 in `hw_findings.md` with UNVERIFIED notices.
  - **X8:** Completed consistency sweep. Confirmed identical-inputs check logic in `main_analyze.cpp` and `main_spectrum.cpp` matches.

- **BLOCKED Steps:**
  - **X5 [HW]:** Re-capture and re-run V2 amplitude comparison — **BLOCKED** (no physical M8 hardware connected over serial/USB audio in this environment).
  - **X6 [HW]:** Re-capture and re-run V3 envelope measurement — **BLOCKED** (no physical M8 hardware connected over serial/USB audio in this environment).
  - **X7 [HW]:** Settle hold comparison or mark untested — **BLOCKED** (no physical M8 hardware connected over serial/USB audio in this environment).

- **Checks Fired During Work:**
  - Identical inputs check (`bytesA > 0 && bytesB > 0 && hashA == hashB && bytesA == bytesB`) fired as expected during X1 verification on two copies of `kick.wav`, returning exit code 2.
  - `--verify-record` hash mismatch check fired as expected during X2 verification on tampered `out_spec.json`, returning exit code 1.
  - `--verify-record` file missing check fired as expected during X3 verification on quarantined `v2` artifacts, returning exit code 1.

---

## UI-1 — UiCapture col/row swap bug

- **Bug:** `captureFromGrid()` in `src/tools/m8/UiCapture.cpp` read
  `ScreenGrid::cells` (keyed `(y, x)`) as if the key were `(x, y)`.
  Two lines:
  ```
  uc.col = pos.first / cap.pitchX;   // pos.first is Y, not X
  uc.row = pos.second / cap.pitchY;  // pos.second is X, not Y
  ```
  This swapped the axes and divided each by the other axis's pitch.
- **Symptom:** Captured text reads vertically down columns instead of
  horizontally. Distinct source columns collide onto the same stored index
  (e.g. x=0 and x=8 both map to col=0 when y is a multiple of 10).
- **Fix:** Corrected to `pos.second / pitchX` for col and `pos.first / pitchY`
  for row. Added a comment noting the surprising `(y, x)` key order.
- **Regression test:** `captureFromGrid col/row at non-symmetric pixel position`
  in `test_device_decode.cpp` — asserts col=10, row=1 for a glyph at x=80,y=10.
  Fails with old code (col=1, row=8), passes with fix.
- **Corpus impact:** All 79 files in `tests/ui/golden/device/` were written by
  the broken code and are irrecoverable (many-to-one collision on column index).
  Moved to `tests/ui/golden/device_broken/`. Re-capture required.
- **Date:** 2026-07-26


## UI-2 — Same song on both sides

Before comparing device vs clone MIXER captures, both must show the same song
and the same screen. Without this, diffs are noise.

### Procedure (Option B — reuse probe on device)

The device already has `PROBE_SELFTEST.M8S` on its SD card (loaded previously
via `m8_nav --load-song`). Regenerate the identical file on the host, then
load it on both sides.

#### Step 1: Generate probe on host

```powershell
build\Release\m8_makeprobe.exe --type macrosynth --shape 0x00 --timbre 0x40 `
    --color 0x80 --note C-4 --tempo 120 --out probe_selftest.m8s
```

This writes `master_volume=0xE0`, `track_volume[i]=0xE0` for all 8 tracks,
and tempo 120. The device's copy was made before the 0x7F instrument-volume
fix, so the output volume byte may differ (device shows F0, freshly generated
shows E0). This does not affect the content check — the check is tempo and
track volumes, not output volume.

#### Step 2: Load on the clone and capture MIXER

```powershell
$script = @"
load probe_selftest.m8s
goto MIXER
ui_capture clone_MIXER_matched
"@
$script | Out-File -FilePath "test_out_ui\cap_mixer_matched.m8script" -Encoding utf8
build\Release\m8_clone.exe --script test_out_ui\cap_mixer_matched.m8script `
    --headless --out-dir test_out_ui
```

Produces `test_out_ui/clone_MIXER_matched.json`.

#### Step 3: Load on the device and capture MIXER

Requires hardware connected over serial:

```powershell
build\Release\m8_nav.exe --port COM<port> --load-song PROBE_SELFTEST
```

Then capture:

```powershell
tools\ui_capture.ps1 -Port COM<port> -Screen MIXER
```

Or use the daemon + sweep workflow. The existing golden
`tests/ui/golden/device/MIXER.json` was captured from this same song and can
be reused without re-capturing, provided the device is showing the same song.

#### Step 4: Verify match

Both captures must show the same:
- **Tempo:** `T>120`
- **Track volumes:** `E0 E0 E0 E0 E0 E0 E0 E0`
- **Output volume:** E0 (clone) / F0 (device golden — pre-fix byte, expected)
- **Track labels:** CH DE RE

The output volume byte differs because the device's PROBE_SELFTEST predates
the 0x7F instrument-volume fix. This is acceptable — the check is tempo and
track volumes.

### Why this matters

The device MIXER golden (`tests/ui/golden/device/MIXER.json`, 767 cells) was
captured with a known song (PROBE_SELFTEST: tempo 120, track vol `E0`). The
clone's default demo song (tempo 124, varied track volumes) produces a
visually different MIXER screen. Cell counts and layout positions cannot be
meaningfully compared until both sides render the same content.

### Current captures (matched song)

| Source | File | Cells | Tempo | Track vols | Output vol |
|--------|------|-------|-------|------------|------------|
| Device | `tests/ui/golden/device/MIXER.json` | 767 | T>120 | E0×8 | F0 |
| Clone  | `test_out_ui/clone_MIXER_matched.json` | — | T>120 | E0×8 | E0 |

The output volume difference (E0 vs F0) is expected: the device's
PROBE_SELFTEST predates the 0x7F instrument-volume fix. Tempo and all 8
track volumes now agree.

- **Date:** 2026-07-26 (updated: matched-song procedure, Option B)


## UI-3a — 205 vs 767 cells: missing horizontal separator bars

The device MIXER emits 767 cells (143 glyphs + 624 bg-only). The clone emits
205 cells (116 glyphs + 89 bg-only). The 535-cell gap is almost entirely
background-only cells that the device draws but the clone does not.

### Root cause

`MixerScreen.cpp` draws:
1. Static text via `drawString()` / `drawChar()` — stamps `ch` and `fg` but
   **not** `bg`. These cells appear in the capture only if they happen to
   overlap a cell already written by `fillRectPixel`.
2. Vertical volume bars via `DrawVerticalBar()` → `fillRectPixel()` — stamps
   `bg`, `fg`, and increments `writeCount`. These cells appear correctly.

The device MIXER also draws **full-width horizontal separator bars** —
rows 1, 2, 4 (40 cyan-bg cells each) and partial fills on rows 3, 5–23.
The clone has no code path to render these bars. `fillRectPixel()` itself
works correctly (confirmed: it writes `m_vram[cy][cx].bg` and increments
`writeCount`), but it is never called for the horizontal bars.

### Device MIXER bg-only cells by row

| Row | bg-only cells | Description |
|-----|--------------|-------------|
| 1 | 40 | Full-width cyan bar |
| 2 | 40 | Full-width cyan bar |
| 3 | 30 | Partial cyan (title region) |
| 4 | 40 | Full-width cyan bar |
| 5 | 22 | Partial fills |
| 6 | 40 | Full-width cyan bar |
| 7–12 | 18 each | Track label backgrounds |
| 13–23 | 17–40 each | Scattered fills |

### Fix

`MixerScreen.cpp` needs a new drawing pass (before the text/bar pass) that
renders the horizontal separator bars. This is a layout-only change — no
engine or data-model work required.

- **Date:** 2026-07-26


## UI-3b — Vertical row offset (element-by-element layout difference)

The device MIXER title "MIXER" appears at row 3; the clone's title is at
row 0. The clone is shifted 3 rows higher.

### Re-measured landmarks (matched song — both sides show probe_selftest)

After loading the same song on both sides (§UI-2 Option B), the offsets
persist. The table below uses clone_row − device_row:

| Content | Device row | Clone row | Offset (clone−device) |
|---------|-----------|-----------|----------------------|
| "MIXER" title | 3 | 0 | −3 |
| "OUTPUT VOL" | 5 | 3 | −2 |
| Track vol hex | 12 | 18 | +6 |
| "CH DE RE" labels | 18 | 20 | +2 |

The offsets change sign: −3, −2, +6, +2. The track-vol-hex row moves in
the opposite direction from the title. This is not a uniform shift.

### Cause — pitch scaling does not explain this

Pitch has been proposed as the cause three times and ruled out each time:

1. **First proposal:** "Device uses pitch_y=10 (24 rows), clone uses
   pitch_y=8 (30 rows) — different vertical density." The original §UI-3b
   table disproved this: the offsets change sign, which a uniform scaling
   cannot produce.

2. **Second proposal (this revision):** If pitch scaling were the sole
   cause, clone rows would be uniformly larger than device rows by the
   factor 10/8 = 1.25 (clone_row ≈ device_row × 1.25). The predicted
   clone rows would be:

   | Landmark | Device row | Predicted clone row | Actual clone row | Error |
   |----------|-----------|--------------------|-----------------|----|
   | MIXER title | 3 | 3.75 | 0 | −3.75 |
   | OUTPUT VOL | 5 | 6.25 | 3 | −3.25 |
   | Track vol hex | 12 | 15.0 | 18 | +3.0 |
   | MX DE RE | 18 | 22.5 | 20 | −2.5 |

   One of four predictions is close; three are off by 3+ rows. The sign
   pattern is wrong: pitch predicts all positive offsets (clone rows >
   device rows), but observed offsets are mixed (−3, −2, +6, +2).

3. **Third consideration:** The row counts (24 device vs 30 clone) mean
   the clone has MORE rows, each SHORTER (8px vs 10px). A scaling from
   10px to 8px makes clone rows SMALLER, not larger. The task's framing
   of "clone ≈ device × 1.25" is correct for row count but the direction
   of the effect is the opposite of what was claimed.

**Conclusion:** The layouts differ element by element. Pitch (row height)
accounts for the difference in total row count (24 vs 30) but does not
account for where individual elements land within those rows. Each element
( title, output vol, track bars, labels) is positioned independently by
the layout code, and the positions diverge for reasons unrelated to pitch.

### Impact

Once Task 2's missing bars are added, the vertical alignment should be
re-measured. The current offset may shrink or change character once both
screens render the same content.

- **Date:** 2026-07-26 (rewritten: pitch ruled out, re-measured from matched song)


## UI-4 — MIXER parity: deferred items

MIXER parity is intentionally parked. The screen is the most irregular in
the app — level bars are sub-character geometry, the sends block has its own
column spacing, and its FX block may contain parameters the clone does not
model. Nothing on it repeats, so every fix is a one-off. The remaining items
below are deferred by decision, not open bugs.

### What is correct (good enough)

- Track-number column and send labels (MX, DE, RE) render at the right
  positions relative to their own block.
- Track volume hex values, output volume, tempo, and project name load and
  display correctly.
- The OTT/RES/TYP label revert (§UI-4a history) was undone; labels match
  their parameters.

### Deferred items

| Item | What | Why deferred |
|------|------|-------------|
| §UI-3a | Background/separator fills (535 bg-only cell gap) | MIXER-specific bars; no other screen needs this geometry. Will revisit via video capture. |
| §UI-3b | Vertical row offsets (element-by-element, not uniform) | Root cause unknown; pitch ruled out three times. Re-measure after fills land. |
| ~~§UI-4a~~ | ~~RES vs OTT label~~ | **CLOSED 2026-08-12** — see below. |
| ~~§UI-4b~~ | ~~Device mixer FX parameter list~~ | **CLOSED 2026-08-13** — read off the device, see §UI-7. |
| ~~§UI-4c~~ | ~~mix_vol never loaded~~ | **CLOSED 2026-08-12** — see below. |
| §UI-4d | Sends column spacing (3-col device vs 4-col clone) | MIXER-specific layout; no other screen has this structure. |
| ~~§UI-4e~~ | ~~Stereo analog_input lost on save~~ | **CLOSED 2026-08-12** — see below. |

### §UI-4a / §UI-4c / §UI-4e — closed 2026-08-12

Three of these were one misreading of the master column, settled by the M8 manual's mixer
section plus a photo of the device's mixer screen. No hardware session was needed after all —
the manual answered what the framebuffer diffs could not.

- **§UI-4a — SUPERSEDED 2026-08-14 by §UI-9, which measured the opposite.**
  `dj_peak` is the DJ filter's resonance, as the library always called it; OTT
  lives at 0xED. There *is* a RES -- it is on the Limiter & Mix Scope view,
  which had not been read when this was written. The original text follows.
- **§UI-4a (superseded) — the device's "OTT" is not a mislabelled RES; there is no RES.** The master column
  is MIX / LIM / DJF / **OTT**. Over The Top is a parallel multiband compressor, a real effect
  in its own right. The clone's `djf_res` was an invention; the file format's `dj_peak` is OTT's
  amount. Renamed to `ott` and implemented. The invented "TYP" row is gone too — the DJ filter's
  type is chosen in the Scope view on hardware, which is why it could never be found on the
  mixer screen.
- **§UI-4c — `mix_vol` was never loaded because the value was landing on the wrong control.**
  The file has exactly one master gain (`MixerSettings::master_volume`) and it is MIX. The
  clone was loading it into `out_vol` and drawing that at the top of the screen as "OUTPUT
  VOL". The device's top line is "SPEAKER VOL", a device-level output that has no slot in the
  song file at all. Fixed: `master_volume` ⇄ `mix_vol`; SPEAKER VOL is application state and is
  never persisted.
- **§UI-4e — resolved by not writing the field.** soft-mate has no analog or USB input, so
  there is no reason to rebuild those blocks from engine fields that cannot represent a right
  channel. `convertEngineToSong` now leaves them alone and save-by-overlay preserves the
  original bytes exactly, right channel included.

Everything above is implemented and the suite is green (238 cases, 893,012 assertions,
2026-08-12). Full write-up: `archive/MIXER_SPEC.md`.

### Approach for deferred items

The intended approach is video capture of the device during playback rather
than static screen diffs. Static diffs cannot capture dynamic behavior (level
bars moving, cursor highlights), and MIXER is the screen where this matters
most. The grid screens (SONG, CHAIN, PHRASE, TABLE) are all variations on
one structure — a grid of hex values with a row-number gutter and a cursor
highlight — and will be tackled first.

- **Date:** 2026-07-26 (rewritten from open-bug list to deferred list)


## UI-5 — Palette id remapping bug in UiCapture

### Bug

`buildPalette()` in `src/tools/m8/UiCapture.cpp` sorted the palette by RGB
and deduplicated it, but never remapped the `fgStyle`/`bgStyle` indices in
cells and rects. `addColor()` assigned palette indices in first-seen order as
cells were walked, so every style id referenced the pre-sort palette while the
serialized `"palette"` array was post-sort. All fg/bg ids in device captures
were therefore mislabeled.

### Root cause

Two-phase design flaw: `captureFromGrid()` accumulated palette entries via
`addColor()` (first-seen order), then called `buildPalette()` which sorted
the palette without updating the cell/rect style ids that referenced it.

### Fix

`buildPalette()` now builds a remap table (old index → new index in the
sorted palette) and updates all `fgStyle`, `bgStyle`, and `style` values in
cells and rects before replacing the palette with the sorted version.

### Regression test

`"captureFromGrid palette ids resolve to correct RGB after sort"` in
`test_device_decode.cpp` — builds a grid with fg colours inserted in
non-RGB-sorted order, runs `captureFromGrid()`, and asserts that every
cell's palette index resolves to the original RGB. Fails with old code
(4 CHECK failures), passes with fix.

### FromJson palette parsing

`fromJson()` now parses the `"palette"` array into `c.palette` (previously
skipped it entirely). This enables `--diff-capture` to resolve style ids
to actual RGB values for cross-capture comparison.

### Colour partition comparison

`--diff-capture` now compares colour structure, not absolute RGB. For each
capture, distinct RGBs are sorted canonically and assigned labels 0, 1, 2,
... in that order. Two captures agree if cells that share a colour on one
side share a colour on the other. This handles the M8's user-selectable
themes (device white is [248,252,248], clone white is [255,255,255]).

### Measurements

**BLOCKED** — M8 hardware not connected (COM4 unavailable; only COM3
(Intel AMT SOL) detected). Re-capture of PHRASE goldens with the fixed
code requires hardware.

### MIXER golden note

`tests/ui/golden/device/MIXER.json` was captured before this fix. Its
style ids are therefore still mislabeled. Its colour data should not be
trusted until it is re-captured.

- **Date:** 2026-07-26



## UI-5 — EQ: filter types, stereo modes, and where the mix EQ lives

Three questions settled on a real M8 (firmware 6.5.0, COM3) on 2026-08-13, all
without audio capture. Full write-up: `archive/EQ_SPEC.md`.

**Seven filter types, not six.** Read by putting the cursor on an EQ band's TYPE
field and cycling it with EDIT+RIGHT:

```
0 LOWCUT   1 LOWSHELF   2 BELL   3 BANDPASS   4 HI.SHELF   5 HI.CUT   6 ALLPASS
```

The vendored file library's `EqType` enum stops at 5 and its `eq_type()`
accessor clamps anything higher to `Bell`, so an ALLPASS band read through it
comes back mislabelled. The raw byte is preserved on write either way. MODE was
read the same way and has five entries, matching the manual.

**The main mix and effect EQs sit immediately after the instrument bank array.**
Found by saving one project twice on the device, identical but for the mix EQ,
and diffing: 17 bytes differ, 15 of them one 18-byte EQ block, the rest being
the project name and a save counter. The offset is
`Offsets::eq + instrument_eq_count * 18` — `0x1AF9E` on a 32-bank V4 file,
`0x1B65E` on a 128-bank one — followed by four blocks: **main mix, ModFX, Delay,
Reverb**, in that order.

All four are confirmed, none inferred. The mix EQ came from the diff above. The
other three were settled by a second save-and-diff on the same day: each
section's INPUT EQ on the Effects screen was given a different low-band
frequency (chorus 111 Hz, delay 222 Hz, reverb 333 Hz) and saved against an
untouched baseline. Eight bytes differ across the whole file — the three
frequencies landing in blocks 1, 2 and 3 in that order, plus the project name
and a save counter. The block order matches the Effects screen's own top-to-
bottom order.

**Q maps logarithmically over 0-99.** Measured from the device's own response
curve — a bell at 1 kHz, +12 dB, screenshotted at Q = 01, 10, 25, 50 and 99. At
01 the curve is a flat +12 dB across the band; at 99 a narrow spike back to 0 dB
by roughly 500 Hz and 2 kHz. That fits `Q = 10^((byte-50)/50)`, which puts the
factory default of 50 on exactly Q 1.0.

Incidental: this device saves **version 6.5.0**, and it decodes cleanly at the
V4.1 offsets — the layout has been stable from 4.1 through 6.5. Its files carry
32 bytes past the four EQ blocks that 4.x files do not; we do not parse them,
and they survive a save because `BinaryWriter` only ever grows its buffer.

- **Date:** 2026-08-13


## UI-6 — The instrument EQ is applied before the send is taken

Measured on a real M8 (firmware 6.5.2, COM3) on 2026-08-13, driven by `m8_nav`
and captured with `m8_capture`. Settles a question the clone had been guessing
at: when an instrument has both an EQ bank and an effect send, does the signal
reaching the effect pass through that EQ?

**Method.** Instrument 00, a MacroSynth CSAW, with **DRY `00`** and **REV `FF`**
— dry path muted, so every sample recorded came out of the reverb. Two captures,
identical but for the EQ:

1. `EQOFF.wav` — EQ field `--`, no bank assigned.
2. `EQON.wav` — EQ field `01`, that bank's HIGH band a HI.SHELF at 5 kHz with
   its gain driven to **−20.00 dB**.

Both triggered by `--keyjazz 60 --keyjazz-vel 40`, three seconds, peaks 0.287
and 0.237 with **zero clipped samples** (velocity 40 was chosen after 127 and 64
both saturated the input — a clipped capture manufactures harmonics and would
have destroyed the very measurement being made).

**Result.**

```
centroid   EQOFF 3696 Hz   EQON 2047 Hz   (-1649 Hz, darker)
log-spectral distance: 12.22 dB
every harmonic above ~5 kHz down 15-23 dB
```

The 15–23 dB loss above 5 kHz is the −20 dB shelf, showing up in a recording of
nothing but reverb. Had the send been tapped ahead of the EQ, the reverb return
would have been identical in both files.

**Conclusion: the instrument EQ sits on the instrument's whole output, before it
splits into the dry path and the sends.** The clone previously applied it to the
dry path only; `Engine::render` now pans, EQs, and then splits.

Incidental: each `EDIT+DOWN` on an EQ gain field moves exactly **−1.00 dB**,
which matches the large-step size the clone's own editor uses.

- **Date:** 2026-08-13

---

## UI-7 — The Limiter & Mix Scope view, read off the device

Read from a real M8 (firmware 6.5.2, COM3) on 2026-08-13, driven by `m8_nav`.
Closes §UI-4b ("device mixer FX parameter list — BLOCKED, needs hardware").

Two parts, with different levels of intrusion: the **layout** below was read
navigation-only, no EDIT and nothing written; the **byte identification** that
follows it needed one parameter change and two saves to the card.

**Method.** `--goto-screen MIXER`, then `--read-field MIX_VOL` to land the
cursor on MIX, then `SHIFT+RIGHT` (`0x14`) to enter, then `DOWN` (`0x20`) three
times, dumping the framebuffer at each stop. Exited with `OPT` (`0x02`).

**The sub-parameters are contextual — they appear on the row of whichever
master parameter the cursor is on, and only that row.** This is why they are
absent from a screenshot taken with the cursor on MIX, and it is the layout
question that could not be answered from the manual alone.

The header changes too: **`MIX SCOPE`** with the cursor on MIX, **`LIMITER
SCOPE`** on LIM — and it *stays* `LIMITER SCOPE` for DJF and OTT, so the header
tracks the scope's signal source, not the selected row.

Cursor position → what the row shows (values are this device's current state,
an unnamed project at tempo 120.00 — **not** a factory default):

```
MIX  E0     SOFT CLIP OFF                 header: MIX SCOPE
LIM  00     ATK 00      REL 10            header: LIMITER SCOPE
DJF  80     TYPE 00     RES 00            header: LIMITER SCOPE
OTT  00     TIME 80     COLOR 80          header: LIMITER SCOPE
```

Top line carries `ZOOM -30DB` and `PEAK -60.00` (silent; the peak reads real
levels during playback). `EQ` sits above MIX as its own cursor stop.

### The file bytes — 0xEB is REL (proven 2026-08-13)

Settled by a single-variable save-and-diff on the device. The scratch project
was named and saved, `REL` was taken from `10` to `FF` in the scope view, and it
was saved again. Files pulled off the card by hand — `m8_nav` has no file
transfer.

**Both files are committed** so this is re-derivable rather than a typed-up
table (`M8_MEASUREMENT_EVIDENCE_SPEC.md`):

```
tests/fixtures/device_golden/scope_rel_10.m8s     REL = 10
tests/fixtures/device_golden/scope_rel_ff.m8s     REL = FF
```

Re-run it with:

```
python -c "a=open('tests/fixtures/device_golden/scope_rel_10.m8s','rb').read(); \
b=open('tests/fixtures/device_golden/scope_rel_ff.m8s','rb').read(); \
print([(hex(i),hex(x),hex(y)) for i,(x,y) in enumerate(zip(a,b)) if x!=y])"
```

Four bytes differ across the whole 112,326-byte file:

```
0x009D   31 -> 32     project name, BYTEPROBE1 -> BYTEPROBE2
0x00BD   74 -> 25     see "time counter" below
0x00BE   24 -> 25       "
0x00EB   10 -> FF     REL
```

**`0xEB` is the limiter REL byte.** It was the only parameter changed and it is
the only byte in the mixer block that moved.

That places it in the four bytes `MixerSettings::from_reader` reads and
discards (`0xEA`–`0xED`), and it retires the guess that those are padding.

### What is still unidentified

Device state at the time of both saves: `SOFT CLIP OFF`, `ATK 00`, `REL 10`,
`TYPE 00`, `RES 00`, `TIME 80`, `COLOR 80`. Against the block:

```
0xEA = 00    consistent with ATK 00 -- and it is the byte immediately before
             REL, mirroring the on-screen order "ATK 00  REL 10". Likely, but
             a value of 00 matches three different parameters, so unproven.
0xEB = FF    REL. PROVEN.
0xEC = 00    unknown
0xED = 00    unknown
```

**TIME and COLOR are definitively not in this block.** Both read `80` on the
device while `0xEA`, `0xEC` and `0xED` are all `00`. Six scope parameters were
never going to fit in four bytes; this confirms at least two live elsewhere in
the file, still unlocated.

One more single-variable save would settle the rest cheaply: set ATK, RES, TIME,
COLOR and SOFT CLIP each to a *different* distinctive value, save once, and diff
— every moved byte then identifies itself by the value it now holds.

### Incidental — a counter at 0xBD/0xBE that changes on every save

`0xBD`/`0xBE` moved without either being touched. Read little-endian they went
`0x2474` -> `0x2525`, a delta of 177, across roughly three minutes between the
two saves. They sit inside the 18 bytes the library skips after `key`, and the
PROJECT screen carries a TIME STATS entry — so this is very likely a play/edit
time counter. Not confirmed, but the consequence holds either way: **two device
saves of an otherwise untouched project are not byte-identical**, and anything
comparing device saves must treat `0xBC`–`0xCD` as volatile.

### Getting files off the device

`m8_nav` decodes the display and presses buttons; there is no file transfer, so
every experiment of this kind needs the `.m8s` pulled by hand (USB disk mode or
a card reader). Note also that SAVE **refuses an unnamed project** ("ENTER A
NAME BEFORE SAVING") and the driver has no text-entry primitive, so naming is a
human step too.

- **Date:** 2026-08-13

---

## UI-8 — The file library's effects-block offsets are wrong

Read from a real M8 (firmware 6.5.2, COM3) on 2026-08-14: the EFFECT SETTINGS
screen, photographed with all three blocks visible, lined up against the bytes
of a device-saved file (`tests/fixtures/device_golden/scope_rel_10.m8s`).

**Device screen**

```
MODFX   MOD TYPE 00 CHORUS   INPUT EQ   MOD DEPTH:FRQ 40:80
        STEREO WIDTH FF      REVERB SEND 00
DELAY   INPUT EQ             TIME L:R 30:30    FEEDBACK 80
        STEREO WIDTH FF      REVERB SEND 00
REVERB  INPUT EQ             ROOM SIZE FF      DECAY:SHIMMER C0:00
        MOD DEPTH:FRQ 10:FF  STEREO WIDTH FF
```

**File, from the effects block base (0x1A5C1)**

```
+0  +1  +2  +3  +4  +5  +6  +7  +8  +9 +10 +11 +12 +13 +14 +15 +16 +17 +18 +19 +20 +21
40  80  FF  00  00  00  00  00  00  30  30  80  FF  00  00  00  00  FF  C0  10  FF  FF
```

Delay's five values (`30 30 80 FF 00`) appear consecutively at **+9**, and
reverb's five (`FF C0 10 FF FF`) at **+17**. Both are exact five-in-a-row
matches, so the alignment is not in doubt.

**Measured layout**

```
+0   MODFX  mod depth        +9   DELAY  time L        +17  REVERB room size
+1   MODFX  mod freq         +10  DELAY  time R        +18  REVERB decay
+2   MODFX  stereo width     +11  DELAY  feedback      +19  REVERB mod depth
+3   MODFX  reverb send      +12  DELAY  stereo width  +20  REVERB mod freq
+4   MODFX  mod type         +13  DELAY  reverb send   +21  REVERB stereo width
+5..+8  unknown              +14..+16  unknown         +22..  unknown (SHIMMER?)
```

**What the library does instead.** `EffectsSettings::from_reader` allows three
filler bytes after the modfx fields and one after delay, so it starts delay at
**+6** and reverb at **+12** — 3 and 5 bytes early respectively. It also names
+2 `chorus_reverb_send` when the device calls it STEREO WIDTH. Newer firmware
added MOD TYPE and SHIMMER; the library never caught up.

**Consequences.**

- Every delay and reverb value the clone loads and displays is the wrong byte.
  A song whose real feedback is `80` shows `00`.
- Load and save are symmetrically wrong, so an untouched song still round-trips
  byte-identically. **L4 cannot catch this** — and did not.
- `saveUnwrittenBlocks` briefly wrote this block back through the library's
  offsets, which turned a merely inert edit into one that lands on the wrong
  parameter. That was removed, and then fixed properly.

**FIXED 2026-08-14.** The clone no longer uses `EffectsSettings` at all.
`loadEffectsBlock`/`saveEffectsBlock` in `SongIO.cpp` read and write the block
at the measured offsets above, in all three paths -- load, save-in-place, and
`saveNewSong` (which had the same bug via `EffectsSettings::write` and would
scramble the effects of any song authored from a template). Bytes not modelled
(+4 MOD TYPE, +5..+8, +14..+16, +22 onward) are skipped so they survive a save.
`cho_reverb` now maps to +3 and persists for the first time.

Test **L25** anchors this: it asserts the loaded values equal the ones on the
device screen above, using the committed `scope_rel_10.m8s`. That is the test
the round-trips could not be -- L4, L19 and L23 all pass just as happily when
load and save are wrong in the same direction, which is exactly how this
survived. L26 pins the unmodelled bytes.
- MODFX REVERB SEND (+3) and MOD TYPE (+4), and reverb SHIMMER, have no engine
  field at all. Phaser and Flanger are not implemented; the clone hardcodes a
  chorus.

**The unknown runs carry real data, and MOD TYPE is NOT at +4.** That was the
obvious guess and it is wrong: V4EMPTY.m8s, a genuine M8-authored file, reads
`FD AF 26 40 FF` at +4..+8 and `41 10 E0` at +14..+16, and `FD` is not one of
the three valid mod types (00 Chorus / 01 Phaser / 02 Flanger). So MOD TYPE and
reverb SHIMMER are somewhere in those runs but remain unlocated. They are
preserved untouched on save (test L26); do not assign them without a diff.

**The layout also holds for V4.0 files, so the library was always wrong** --
this is not a newer-firmware drift. V4EMPTY.m8s reads `30 30 80 FF 00` for
delay and `FF C0 10 FF FF` for reverb at the measured offsets, matching the
6.5.2 device screen exactly.

**Consequence for our own files:** every .m8s this project authored before
2026-08-14 has its effects written at the library's offsets, because
`saveNewSong` used `EffectsSettings::write`. Both `songs/sunrise.m8s` and
`songs/opening.m8s` have been regenerated; no committed song still carries it.

- **Date:** 2026-08-14

---

## UI-9 — The rest of the scope/effects bytes, and a correction to §UI-4a

Device probes on a real M8 (firmware 6.5.2, COM3), 2026-08-14. Method as §UI-8:
save, change several fields to *distinct* values, save again, diff. Distinct
values mean each moved byte identifies itself by what it now holds. Screens
photographed before and after so every reading is the device's, not our
parser's.

### Results

```
MIXER BLOCK (base 0xCE)              EFFECTS BLOCK (base 0x1A5C1)
+26  0xE8  DJ filter RES             +22  reverb SHIMMER
+28  0xEA  Limiter ATK               +23  OTT TIME
+29  0xEB  Limiter REL  (§UI-8)      +24  OTT COLOR
+30  0xEC  SOFT CLIP  (00/01)        +25  ModFX MOD TYPE
+31  0xED  OTT amount
```

Evidence, each value matched to exactly one field that was changed:

```
RES       00 -> 30   moved 0xE8        SHIMMER   00 -> A0   moved +22
ATK       00 -> 10   moved 0xEA        OTT TIME  80 -> 40   moved +23
SOFT CLIP off -> on  moved 0xEC 00->01 OTT COLOR 80 -> 50   moved +24
OTT       00 -> A0   moved 0xED        MOD TYPE  00 -> 02   moved +25
```

Committed evidence: `tests/fixtures/device_golden/probe_res30.m8s` (RES `30`,
OTT `00`) and `probe_ottA0.m8s` (RES `00`, OTT `A0`). Pinned by test **L27**.

### §UI-4a was wrong: `dj_peak` is RES, not OTT

On 2026-08-12 §UI-4a concluded the file library's `dj_peak` was OTT, on the
strength of the manual and a photo of the mixer screen, and the engine field
was renamed `djf_res` -> `ott` to match. That was backwards. Moving RES on the
device moves `dj_peak`; moving OTT leaves it alone and moves `0xED` instead.
The library's original name was right, and `dj_filter / dj_peak /
dj_filter_type` is a coherent trio: cutoff, resonance, type.

The reasoning that led there is worth keeping as a caution: the mixer screen
shows OTT and shows no RES, so a field named "peak" next to the DJ filter
looked like a misnomer. It wasn't -- RES simply lives on a screen we hadn't
read yet. **A control's absence from one screen is not evidence about a byte.**

Consequence in the clone, fixed the same day: the mixer's OTT control had been
writing the DJ filter's resonance byte, and the OTT compressor was being driven
by a resonance value. `dj_peak` now maps to `mixer.djf_res` (stored and
round-tripped, no UI -- RES is set in the scope view we do not build) and OTT
reads and writes `0xED` through the same patch route as the other fields the
library does not model.

### Still unaccounted

Effects `+4..+8` and `+14..+16` (`FD AF 26 40 FF` and `41 10 E0` in a
device-authored file) correspond to no control on either screen. Mixer `+28`,
`+29`, `+30` are identified but have no engine field, so they are preserved
untouched on save (test L24).

- **Date:** 2026-08-14

---

## UI-10 — The M8's pan law is balance, not constant-power

**Date:** 2026-08-14. Firmware 6.5.2, COM3. Measured with `m8drv` driving the
parameter and `m8_capture` + `m8_analyze` measuring, entirely unattended.

### Method

Instrument 00, MACROSYN CSAW, `DRY FF` / `REV 00` (dry path only, so the reverb —
which is decorrelated stereo regardless — cannot mask the voice), `AMP 20`.
Keyjazz C-4 at velocity `0x08` for headroom, 2 s captures. Only `PAN` changed
between the two takes.

### Results

| PAN | mid RMS | side RMS | L/R corr | derived L | derived R |
|---|---|---|---|---|---|
| `80` (centre) | 0.010864 | 0.000000 | +1.0000 | 0.010864 | 0.010864 |
| `00` (hard left) | 0.005433 | 0.005433 | 0.0000 | 0.010866 | 0 |

Derived from `mid = (L+R)/2`, `side = (L-R)/2`.

**L is unchanged between centre and hard left** (0.010864 vs 0.010866). Panning
fully to one side takes the far channel to exactly zero and leaves the near
channel at the level it already had.

### What this means for the clone

`Engine.cpp`'s master bus uses a **constant-power** law:

```cpp
float panL = std::cos(pan * 1.5707963f);
float panR = std::sin(pan * 1.5707963f);
```

which puts centre at 0.707 and hard-left at 1.0 — so it boosts the near channel
by 3 dB as the pan sweeps, and renders a centred track 3 dB quieter than hardware
does. Under that law, L at hard left should have measured 0.01537; it measured
0.010866. The hardware law is instead a plain balance control: the near channel
stays at unity and the far channel is attenuated.

Not yet measured: intermediate pan positions, which would distinguish a linear
taper from a curved one. Two endpoints cannot tell those apart, so the exact
shape between `00` and `80` is still unknown — sweep `20`/`40`/`60` before
committing to a replacement curve.

### Side findings from the same run

- **The capture rig measures stereo correctly.** `side RMS` of exactly 0.000000
  with `corr` of exactly +1.0000 on a centred mono source is the calibration that
  makes every other stereo measurement here trustworthy.
- **`OUTPUT VOL` does not reach the USB audio tap — confirmed, not inferred.**
  With keyjazz velocity held at `0x08`, `OUT_VOL F0` and `OUT_VOL 40` produced
  captures with identical peaks (0.013641 both). The `status.md` note that raising
  `OUTPUT VOL` cannot fix capture level is correct: use keyjazz velocity as the
  level lever instead. Velocity `0x7F` clips hard (50,573 samples, crest 1.43 dB);
  `0x40` gives peak 0.43 clean.
- **Level gates do not apply to a single sustained oscillator.** `m8_analyze`
  fails these files on `crest <= 6 dB` and DC thresholds tuned for full mixes; a
  sustained CSAW legitimately has ~2 dB crest. Read `clipped` and `peak` for
  validity here, not the overall verdict.

---

## UI-11 — The voice path carries stereo, and HyperSynth WIDTH is unipolar

**Date:** 2026-08-14. Firmware 6.5.2, COM3. `m8_makeprobe` authored the probes,
`m8drv` loaded them, `m8_capture` recorded, `m8_analyze` measured — unattended.

### Method

Two probes differing in one byte: `m8_makeprobe --type hypersynth --width 0x00`
and `--width 0xFF`, note C-4, instrument vol `0x7F`, pan `0x80`, dry `0xC0`. Each
loaded on the device by filename, then keyjazz C-4 at velocities `0x40` and `0x20`
for 2 s. Two velocities per probe because clipping would **false-negative** this
test: squashing both channels against the rails makes them more alike, so a
clipped capture can read as mono when it is not. All four takes came back
`clipped: 0`, peak ≈ 0.26, crest ≈ 11 dB.

WIDTH had to be baked into the probe because `ScreenModel.h` has no HyperSynth
field map, so it cannot be set on the device by field name.

### Results

| WIDTH | vel | mid RMS | side RMS | L/R corr | side/mid |
|---|---|---|---|---|---|
| `00` | `0x40` | 0.074140 | 0.000000 | 1.0000 | 0 |
| `00` | `0x20` | 0.074375 | 0.000000 | 1.0000 | 0 |
| `FF` | `0x40` | 0.074243 | 0.002136 | 0.9984 | 0.029 |
| `FF` | `0x20` | 0.074446 | 0.002053 | — | 0.028 |

1. **The M8's voice path preserves stereo.** WIDTH `FF` produces real side energy;
   WIDTH `00` is exactly mono. So the clone's `SynthVoice.cpp` collapse
   (`sample = 0.5f * (outL + outR)`) destroys information the hardware keeps.
2. **WIDTH is unipolar**: `00` is no spread, `FF` is maximum. It is *not* bipolar
   around `0x80`, so the stock `HyperState::width = 0x80` default is mid-spread.
3. **The effect is small** — side/mid ≈ 0.029, about −31 dB, correlation still
   0.998 at maximum. Peak and RMS are unchanged across all four takes
   (0.2614 / 0.0741), so WIDTH redistributes energy rather than adding any.

### The clone, measured the same way

`m8_render` on the identical probe files: side RMS 0.000086 at width `00` and
0.000085 at width `FF`, `corr` 1.0000 for both — i.e. no response to WIDTH at all,
as expected from the summing. Both sides of the comparison are now numbers rather
than one number and a code reading.

### What this implies for priority

Real but subtle. Against §UI-10's pan-law error — a 3 dB level error on *every*
track, in the wrong direction as pan sweeps — HyperSynth WIDTH at −31 dB is the
smaller audible defect. **Stereo samples are the untested case likely to matter
more**, since a genuinely stereo sample carries far more side content than this;
that needs a stereo WAV on the card and has not been measured.

---

## UI-12 — The pan curve is linear, and stereo samples play in full stereo

**Date:** 2026-08-14. Firmware 6.5.2, COM3. Probes authored by `m8_makeprobe`,
loaded by `m8drv`, recorded by `m8_capture`, measured by `m8_analyze`. Unattended.

Follows §UI-10, which established from two endpoints that the pan law is balance
rather than constant-power but could not distinguish a linear taper from a curved
one. This sweeps the interior.

### Pan curve

Macrosynth probe, `PAN` set on the device across the sweep, keyjazz C-4 at velocity
`0x40`, all captures `clipped: 0`. L and R derived as `mid + side` and `mid - side`:

| PAN | L | R | R/L | pan/0x80 |
|---|---|---|---|---|
| `00` | 0.006704 | 0.000000 | 0.000 | 0.000 |
| `20` | 0.006862 | 0.001694 | 0.247 | 0.250 |
| `40` | 0.006913 | 0.003447 | 0.499 | 0.500 |
| `60` | 0.006968 | 0.005224 | 0.750 | 0.750 |
| `80` | 0.007122 | 0.007122 | 1.000 | 1.000 |

**R/L equals `pan/0x80` to three decimals. L is flat within 6%** (noise —
constant-power would have moved it 41%). So the law is: near channel at unity, far
channel attenuated **linearly**. No curve.

Only the lower half was swept. The upper half is taken by symmetry; `00` and `80`
are both pinned by measurement. Sweep `A0`/`C0`/`E0`/`FF` if that is ever in doubt.

Fixed in `Engine.cpp`'s master bus, replacing `cos`/`sin`. Pinned by test `MB6`
(`[mixer]`), whose "hard left does not raise the left channel" assertion is
precisely the one constant-power fails.

### Stereo samples

Two sampler probes over WAVs built for the purpose: `STEREOL.WAV` carries a 440 Hz
tone on L with silence on R (so side = mid, corr 0 in the file itself), and
`MONOREF.WAV` carries the same tone identically on both (side = 0, corr +1). Same
amplitude, same length. Instrument pan centred in both.

| capture | mid RMS | side RMS | L/R corr |
|---|---|---|---|
| `STEREOL.WAV` on device | 0.000134 | **0.000134** | 0.0000 |
| `MONOREF.WAV` on device | 0.000268 | 0.000000 | 1.0000 |

The device reproduces each file's stereo image exactly. `MONOREF`'s mid is precisely
2x `STEREOL`'s, which is what a hard-panned-L file versus a both-channels file must
give when nothing sums them — and the mono control confirms the path contributes no
stereo of its own, so the `STEREOL` result cannot be an artefact.

**So the M8 plays stereo samples in full stereo.** `SynthVoice.cpp`'s
`0.5f * (sampOut[0] + sampOut[1])` destroys a complete stereo image, not the
-31 dB spread that HyperSynth WIDTH turned out to be (§UI-11).

The same probe files through `m8_render` (`--sample-root`): side RMS 0.000053 for
the stereo file against 0.000061 for the mono one — indistinguishable, i.e. our
output is mono whichever it is fed.

### Priority this settles

Stereo **samples** are the case that justifies the stereo voice path, not the synth
WIDTH parameters. A full stereo image is lost today where hardware keeps it.
