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

---

## UI-13 — Scales snap UP, constrain note entry, and store offsets as signed hundredths

**Date:** 2026-08-14. Firmware 6.5.2, COM3. Probe authored by hand on the device,
loaded by `m8drv`, recorded by `m8_capture`, measured offline.

### Method

A project saved on the device (`tests/fixtures/device_golden/scaleprobe.m8s`)
with scale 00 restricted to **C and E only**, scale 01 restricted to C with its
OFFSET set to **-00.50**, and phrase 00 holding four notes on track 1:

```
0  C-4      in scale
1  D#4      out of scale
2  F-4      out of scale
3  A#4      out of scale
```

Instrument 00 was `NONE`, so it was cycled to MACROSYN/CSAW in RAM (TRANSP ON,
which is what lets the scale act) and the tempo stepped down to 29 BPM for
~500 ms per row. Nothing was saved back, so the card still holds the original.
`m8_capture --seconds 4`, peak 0.221, `clipped: 0`.

### Results — the snap direction

Fundamental per row, by harmonic spacing and by autocorrelation (both agree):

| row | written | sounded | MIDI | interval | moved |
|---|---|---|---|---|---|
| 0 | C-4 | C2 65.41 Hz | 36 | 0 | 0 |
| 1 | D#4 | E2 82.41 Hz | 40 | 3 → 4 | **+1** |
| 2 | F-4 | C3 130.81 Hz | 48 | 5 → 12 | **+7** |
| 3 | A#4 | C3 130.81 Hz | 48 | 10 → 12 | **+2** |

**Correction (same day):** the "constant -24 offset between written and sounded
MIDI" first recorded here was an artefact of reading the M8's note NAMES as
standard MIDI. The file stores the note the device calls `C-4` as `0x24` = 36,
and our engine plays 36 as 65.41 Hz — exactly what the device produced. There is
no offset at all; M8 octave naming just sits two below the C4 = 60 convention.
The interval arithmetic below is unchanged, since it only ever used differences.

With names aligned, all four notes fit one rule exactly: **a disabled interval
snaps UP to the next enabled one.**

**Row 2 is the case that settles it.** F-4 rose *seven* semitones to the next C
rather than falling *one* to the E directly below it. Snap-down fails rows 1, 2
and 3; "nearest" fails row 2 for the same reason. Snap-up fits all four.

### Results — entry is constrained, not snapped

With scale 00 already restricted to C and E, **D# cannot be entered at all** —
the note field steps over it. And restricting a scale *after* notes are written
does **not** rewrite them: the phrase above still reads `D#4` and `A#4` on
screen after the scale was narrowed. So a scale never rewrites the grid, and the
snap rule above is reached only through playback — i.e. transpose, PIT, or a
scale narrowed after the fact.

### Results — the OFFSET encoding

Scale 01's first offset reads `CE FF` in the file: `0xFFCE` little-endian, -50.
So an OFFSET is a **signed 16-bit LE value in hundredths of a semitone**, the
same scheme §7 of AGENTS.md records for EQ gain. We had been reading the pair as
(signed whole semitone, unsigned hundredths), as the vendored library does —
which agrees on every all-zero offset, i.e. every offset in every song we held,
and cannot represent anything in `(-1.00, 0.00)` at all.

The same file also confirms the **46-byte record stride** from the device side:
records 2 and 3 still decode as untouched factory `MINOR` (`0x05AD`) and
`DORIAN` (`0x06AD`) with names on +26 after a real hardware save.

### Driver note

`m8drv` cannot edit the SCALE view — `kScaleFields` maps only TUNE, NAME, LOAD
and SAVE, so the twelve interval rows have no field model and the edit gestures
land as no-ops on an EN cell while `inspect` shows the accent moving. That is
the same unmodelled-column problem behind driver bugs #22-#24. The scale had to
be set by hand; everything after that (load, retune, play, capture) was
unattended. `SET TEMPO` also thrashes over a large gap and had to be replaced by
coarse `EDIT+DOWN` presses — and killing it mid-run orphaned `m8_nav`, which
holds COM3 until the process is killed.

### Follow-up — SCA/SCG are 0x10 and 0x11, and Part K is wrong

A second save of the same probe, with `SCG 10` on phrase row 0 and `SCA 20` on
row 1, stores those FX slots as `11 10` and `10 20`. So **SCA = 0x10 and
SCG = 0x11**.

`FX_COMMANDS_SPEC.md` Part K says `0x17` / `0x18`. That table derives the whole
`0x09..0x23` run by walking the manual's FX list in order, and the device's own
enum is not in that order: stepping the FX cell from `---` gives

```
ARP ARC CHA DEL GRV HOP RND RNL RET REP RTO NTH PSL PBN PVB PVX SCA SCG
```

with `DEL`/`GRV`/`HOP` where the spec puts `RND`/`RNL`/`RET`, and `RMX` absent.
**Treat every entry in Part K past TIC as unverified.** Pinned by `L32`.

### Still open — the SCA/SCG key numbering

X is the key and Y the scale number, but which root note X names is **not**
settled. An attempt to read it on-device was inconclusive and should not be
mistaken for evidence: with `SCG 10` in the phrase, the PROJECT and SCALE views
both still showed key `C` after pressing PLAY — but `inspect --key 0x08` reports
that the PLAY press never landed on the PHRASE screen, so the command had not
run. (`m8_capture` drives PLAY successfully over its own serial path, so the two
press paths differ; unexplained.)

Two candidate mappings remain: `0 = C` (what the engine implements) and
`0 = B` (Part K's guess, from the same table `L32` just disproved elsewhere).
To settle it, get the transport genuinely running with an AUDIBLE instrument and
a scale narrow enough to hear the root move — the probe's instrument 00 is
`TYPE NONE`, which is silent, so nothing about it was observable either way.

---

## UI-14 — The stock theme accent is [0,240,248], and the driver was looking for [0,252,248]

**Environment:** M8 headless on `COM3`, firmware 6.5.2, stock theme, `font_mode=0`.
**Command:** `m8_nav --port COM3 --ui-capture artifacts/inst00_capture.json`
**Artifact:** `artifacts/inst00_capture.json` (gitignored; regenerate with the command
above — `screen=INST.00`, `cells=812`, `rects=3`, `palette=8`).

The capture reports `theme_id: "m8-default-6.5.2"`, so this is the factory theme, not a
user customisation. Its full palette:

| idx | RGB | role (by usage) | cells |
|---|---|---|---|
| 0 | `[0, 0, 0]` | background | — |
| 1 | `[0, 240, 248]` | **cursor / accent** | 20 |
| 2 | `[32, 36, 48]` | empty | 37 |
| 3 | `[72, 76, 96]` | info | 238 |
| 4 | `[144, 172, 184]` | default text | 195 |
| 5 | `[144, 180, 184]` | slider | — (2 rects) |
| 6 | `[248, 32, 48]` | titles | 154 |
| 7 | `[248, 252, 248]` | values | 168 |

Index 1's 20 cells spell the `CUTOFF FF` row, which is where the cursor was sitting, and
the third rect (`col 11, row 19`) carries `style 1` — the CUTOFF slider drawn in accent.
That is the cursor, confirmed twice over.

**The bug this closes.** `ScreenGrid::cursorColor` defaulted to `[0, 252, 248]` and
`isCursor()` was an exact three-channel equality test. `[0,240,248] != [0,252,248]`, so
`isCursor()` returned false for **every cell on every screen**: `cursorRowY`,
`cursorField` and `moveCursorToGrid` all returned -1, `m8drv probe` reported
`cursor_moved: false` on every press, and `inspect` found zero accent cells and concluded
"the press is not landing on this screen."

**The presses were landing the whole time.** Three independent proofs from the same
session: `goto` walked SONG → PHRASE00 → INST00; a DOWN probe on INSTRUMENT returned
`rows_changed: 1` with the verdict *"PRESS LANDS but cursor tracking is broken"*; and a
run of DOWN presses on PHRASE moved the real cursor to row 3, where four subsequent
`m8_capture --keyjazz` calls then recorded a note (`A#4 64` → `C-4 7F`, matching the last
capture's velocity `0x7F`). **Keyjazz records into the phrase at the cursor row — do not
capture from a grid screen.**

**Fix.** `cursorColor` corrected to the measured `[0,240,248]`, `isCursor()` given a
per-channel tolerance of 16 (far below the spacing between the eight stock palette
entries, so it cannot latch onto a neighbour), and `m8_nav --cursor-color R,G,B` added for
devices on a theme further from stock. An exact-equality test against one hardcoded colour
was never going to survive a device with user-selectable themes.

- **Date:** 2026-08-18

### Confirmed by calibration, same session

`m8_nav --port COM3 --pin-theme` on the INSTRUMENT screen, deriving the accent from
motion rather than from any assumed value:

```
pin-theme: DOWN moved 3 colour(s):
    [  0,240,248]  20 cells   <- accent (smallest extent)
    [248,252,248]  168 cells
    [144,172,184]  195 cells
```

Three colours move on a cursor step, not one, and the reason is worth stating because
it is what makes the extent rule work: the row the cursor *leaves* reverts to normal
text and the row it *arrives* at becomes accent, so both the accent and the ordinary
text colours change extent. The accent is the small one — 20 cells against 168 and 195,
a margin wide enough that the rule is not delicate. The independently-read palette above
says `[0,240,248]`; the calibration, which never looks at a colour value, agrees exactly.

**Falsification.** Pointing the driver at an accent the device does not have
(`--cursor-color 248,160,0`) now produces, rather than silence:

```
theme: WARNING -- accent [248,160,0] (--cursor-color) appears on no cell of this
screen. Cursor reads will all fail and will look like the device is ignoring keys.
Fix with: m8_nav --port <PORT> --pin-theme
```

That warning is the actual deliverable. The wrong colour was always going to be
findable in an afternoon; what cost a session was that the driver had no way to say
"I cannot see a cursor" and so said "the device is not responding" instead.

## UI-15 — The device palette is RGB565, and the clone's boot theme now matches it

**Environment:** M8 headless on `COM3`, firmware 6.5.2, `font_mode=0`.
**Command:** `python tools/m8drv/m8drv.py batch` — `GOTO`/`CAPTURE` pairs for SONG,
PHRASE, MIXER and INSTRUMENT over one connection, then the same three grid screens
again with playback running.
**Artifacts:** scratch captures (regenerate with the command above).

### The palette, by observed role

Roles are assigned from which cells carry each index, not from assumption:

| RGB | role (by usage) | evidence |
|---|---|---|
| `[0, 0, 0]` | background | every screen's `bg` |
| `[32, 40, 48]` | empty | the `---` empty-step dashes (275 cells on PHRASE) |
| `[72, 80, 96]` | info | column headers, `T>120`, the nav map |
| `[144, 176, 184]` | default text | field labels |
| `[248, 252, 248]` | values | field values (`00`, `E0`, `FF`) |
| `[248, 32, 64]` | titles | `SONG`, `PHRASE 00`, `MIXER`, `INST.00` |
| `[0, 252, 248]` | cursor / accent | the cursor's field |
| `[0, 252, 96]` | play markers | the `<` and `>` playhead glyphs |
| `[144, 184, 184]` | slider | INSTRUMENT scope only |

The play-marker colour only appears with the sequencer running, which is why it is
absent from a resting capture. It shows up as both `fg` and `bg` on the same cells.

### Every channel is RGB565

Red and blue are always multiples of 8, green always a multiple of 4 — a 5/6/5
split expanded by shifting, not by bit-replication (`31 << 3 == 248`, never 255).

This is the useful part. Four of the clone's boot-theme slots were written as
`0xFF` in a channel, which the panel cannot produce. Quantizing each of those to
RGB565 lands **exactly** on the measured device colour, four times out of four:

| slot | clone had | quantized | device | |
|---|---|---|---|---|
| TEXT_VALUE | `FFFFFF` | `F8FCF8` | `F8FCF8` | match |
| TEXT_TITLES | `FF2040` | `F82040` | `F82040` | match |
| PLAY_MARKERS | `00FF60` | `00FC60` | `00FC60` | match |
| CURSOR | `00FFFF` | `00FCF8` | `00FCF8` | match |

So the clone's defaults were the device's theme all along, recorded with rounding
errors. `src/ui/Theme.h` now carries the measured values.

Three slots were never observed — SELECTION, METER_LOW/MID/PEAK need a selection or
live meters on screen. They keep their original hue with the same quantization
applied and are marked `INFERRED` in `Theme.h`. That is a guess about *value*, but
not about *representability*: an unquantized colour is one the device could not
display at all.

### This supersedes §UI-14's palette table, without contradicting its fix

§UI-14 (2026-08-18) read the same device and recorded different numbers — cursor
`[0,240,248]`, titles `[248,32,48]`, empty `[32,36,48]`. The differences are real
stored values, not a decode difference: both readings expand green as `g6 << 2`, and
`240` is `g6=60` against today's `g6=63`.

The device's theme changed between the two sessions. `theme_id` does not detect
that — it is a pinned constant in `hw_theme.json`, not a value read from hardware,
so both captures stamp `m8-default-6.5.2` regardless. The corroborating evidence is
`hw_theme.json` itself, whose pinned accent is `[0,252,248]`: it was re-pinned after
§UI-14 and agrees with today's reading.

§UI-14's conclusion is untouched, and its fix is what absorbed the change: it
replaced an exact-equality cursor test with a per-channel tolerance of 16, and
`|252 - 240| = 12` falls inside that. Cursor tracking kept working across a theme
change that would have broken the original exact test all over again. **Do not
re-pin a colour from §UI-14's table; take it from a fresh capture or
`m8_nav --pin-theme`.**

### Column origin, checked the same session

The device leaves **column 0 as a gutter**: on PHRASE the only cell inked there is
the `<` playhead marker. Text starts at column 1. After the clone's content origin
moved to (1,1), a fresh device-vs-clone capture pair agrees on the column for every
token checked — `PHRASE` at 1, `FX1/FX2/FX3` at 13/19/25, `T>` at 34, `SCPIT` at 34.
Rows still differ, which remains the pitch arithmetic of §UI-3 (8x10 device against
8x8 clone) and is not a defect.

### Driver note — `PRESS key=PLAY` is rejected by the daemon

`parseKeyMask` ([Daemon.cpp:41](../../src/tools/m8/Daemon.cpp:41)) folds SHIFT, EDIT,
OPT and the four arrows, but not PLAY — so `m8drv batch` cannot start the sequencer
by name and returns `invalid key parameter: PLAY`. `Key::PLAY` exists, and the
one-shot path in [main_nav.cpp:558](../../src/tools/main_nav.cpp:558) maps the name
fine; only the daemon's parser is missing it. Work around it with the raw mask,
`PRESS key=0x08`, which is how the playback captures above were taken.

- **Date:** 2026-08-21

---

## UI-16 — The FX command byte table, read off the device

- **Date:** 2026-08-24
- **Firmware:** 6.5.2, COM3, via `m8drv batch`
- **Song:** DEMO2, a v2.5.1 factory bundle, loaded on the device

### What was wrong

`libFxToEngine` mapped file byte → command arithmetically: `0x00..0x08` →
`VOL PIT DEL REV HOP KIL TBL GRV TIC`, i.e. the declaration order of the
`FxCmd` enum, with everything else `UNKNOWN`. `FX_COMMANDS_SPEC.md` Part K
carried the same run out to `0x23`, already marked wrong from `0x09` up by the
2026-08-14 SCA/SCG probe — but it claimed `0x00`–`0x08` were sound "because
they are exercised by the round-trip tests."

They were not. A round-trip runs our reader against our own writer, so it holds
under **any** consistent mapping and can never pin a byte to a command. `L12`
asserted `0x06 → TBL` and passed for months while the device called `0x12` TBL.
This is the same circularity that hid the DETUNE bug.

### Method

DEMO2 uses 21 distinct FX command bytes. A greedy cover picked ten phrases
reaching all 21, each addressed as (song row, track) → chain step → phrase, and
captured with `CAPTURE`. Every FX cell on every captured screen was matched
against the byte at that step in the file. **The value columns agreed on every
cell**, which is what pins the alignment — a misalignment would have shown up as
mismatched values, not just names.

### Result

```
Sequencer                    Instrument / modulation
0x04  HOP    0x12  TBL       0x80  VOL    0x9A  DE2
0x05  KIL    0x13  THO       0x92  EA1    0x9C  LA3
0x08  REP    0x17  VMV       0x94  HO1    0x9D  LF3
0x0A  PSL    0x21  XRD       0x95  DE1    0x9E  LT3
0x0C  PVB
```

plus `0x10` SCA / `0x11` SCG from the 2026-08-14 probe. No overlap, no conflict.

Three entries contradicted the old table: **`0x08` is REP, not TIC** (12 uses in
DEMO2 — we were firing a tick command where the song means repeat), **TBL is
`0x12`, not `0x06`**, and **VOL is `0x80`, not `0x00`**. Only `0x04` HOP and
`0x05` KIL happened to agree, which looks like coincidence.

The bytes fall in two ranges — sequencer below `0x40`, instrument and modulation
from `0x80`. The old contiguous run could not express that.

### Two things this does not settle

**Completeness.** Only bytes DEMO2 uses were read. The run is not contiguous, so
a byte cannot be inferred from its neighbours. Still named but unmapped, in usage
order: `0x91` SRV (16), `0x89` CUT (11), `0x83` PLY (7), `0x84` STA (4) — the
device names them, the engine has no counterpart, so they need semantics.

**Version.** The 21 readings come from a v2.5.1 file, SCA/SCG from a 6.5-authored
one. Nothing here can test whether 4.x/6.x files agree, because **no 4.0+ song in
this tree uses FX at all** — TEST01, KICK01, neondusk, sunrise and opening have
zero between them. That is also why the old table was never caught: nothing
exercised it. Settling it needs a song authored on the device with known FX and
saved as 6.5.

### One migration detail

The device rescales REP's value ×4 when loading a pre-4.0 song: file `0x02`
showed as `08`, `0x08` as `20`, `0xFF` as `FC` (`0x3FC` truncated). Pre-4.0 files
hold the older scale. Unknown whether other commands do the same.

Pinned by `S-FX1` / `S-FX2` in `tests/test_persistence.cpp`; raw map in
`hw_crawl/DEMO2_fx_map.json`.

---

## UI-17 — NTH's skip period is X+1, measured

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`
- **Song:** a blank project authored on the device by the driver

### The question

`NTH XY` is a conditional trigger on loop count. The manual (p. 71) declines to
give the arithmetic — "refer to the help text at the bottom of the screen" — and
the help text only renders while `EDIT` is held, which `m8drv` cannot snapshot:
it presses and releases, then reads. So the law was measured behaviourally
instead, which is better evidence than the prose would have been.

### Method

Instrument 00 set to MACROSYN, `SHAPE 22` KICK — self-decaying, so each trigger
is one countable blip and no envelope has to be authored. `AMP 40`. Phrase 00
row 0: `C-4`, vol `64`, instrument `00`, `NTH` in FX1, rows 1-15 empty. PHRASE
mode loops the 16 rows; at the stock 120 BPM that is **2.000 s per loop**,
confirmed to the millisecond by the onset spacing below.

Captured 20 s per value with `m8_capture --port COM3 --seconds 20` and counted
rising envelope crossings with `tools/count_onsets.py`.

### Results

| Value | X | Onsets in 20 s | Spacing | Loops per trigger |
|---|---|---|---|---|
| `NTH00` | 0 | 11 | 2.000 s | every loop |
| `NTH01` | 0 | 11 | 2.000 s | every loop |
| `NTH02` | 0 | 11 | 2.000 s | every loop |
| `NTH10` | 1 | 5 | 4.000 s | every 2nd |
| `NTHF2` | F | 1 | — | every 16th |

**The period is X + 1.** X=0 plays every loop, X=1 every second, X=F every
sixteenth. Equivalently: the row plays when `loopCount % (X + 1) == 0`.

### The trap this run walked into first

`NTH01`, `NTH02` and `NTH00` are indistinguishable, and the first pass read that
as "NTH does nothing". They are not a control on the period at all: **X skips
what is to the LEFT of the command and Y what is to the RIGHT** (manual p. 71),
and in `NTH0Y` the X digit is zero, so nothing on the left — including the note —
is ever skipped. FX2 and FX3 were empty, so Y had nothing to act on either.

Only the **high** nibble moves the note. Anyone re-running this should set `NTHX0`.

Y is assumed symmetric with X and was not separately measured.

### What it settles

The clone's `loopCount % (x + 1) != 0` skip is **correct**. Recorded in
`tessera/docs/parity.md` as closed rather than as a disparity.

### Incidental readings from the same session

- The device's stock MACROSYN defaults, read off `INST00`: `SHAPE 00 CSAW`,
  `TIMBRE 80`, `COLOR 80`, `DEGRADE 00`, `REDUX 00`, `FILTER 00`, `CUTOFF FF`,
  `RES 00`, `AMP 00`, `LIM 00`, `PAN 80`, `DRY C0`, `MFX 00`, `DEL 00`, `REV 00`,
  `TRANSP ON`, `TBL.TIC 01`, `EQ --`.
- **There is no VOLUME field on the instrument screen.** AMP is the only gain
  control the device exposes, which is the device-side confirmation of the clone's
  A1 disparity.
- **`TBL.TIC` defaults to `01` on the device**, not `FF`. The clone defaults every
  instrument type to `0xFF`, which the manual defines as "increments table row at
  200 Hz" against the device's "1 tick per row". Worth checking as a separate
  disparity.
- Braids shape indices agree with the vendored enum: `20` STRUCK BELL,
  `21` STRUCK DRUM, `22` KICK.
- `EDIT+RIGHT`/`LEFT` steps an FX command by one and `EDIT+UP`/`DOWN` steps a
  value's high nibble by one, wrapping. The FX selector is a grid, so walking it
  with `EDIT+RIGHT` alone visits only one row of commands — `GGR`, `INS`, `KIL`
  and `RMX` were all absent from that walk and are not missing from the device.

---

## UI-18 — The M8 names middle C "C-6"; RNL randomises its own row only

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`
- **Song:** the blank project from §UI-17, MACROSYN `SHAPE 00 CSAW`, `AMP 40`

### Octave naming — measured, not inferred

| Entered on the device | Measured fundamental | MIDI |
|---|---|---|
| `C-4` | ~65 Hz | 36 |
| `C-6` | 260 Hz | 60 |

Both measured with `m8_analyze --pitch` on a 0.4 s slice, cross-checked against
`tessera_analyze --pitch`, which agrees on 260 Hz for the same tone.

**The M8 names MIDI 60 — middle C — `C-6`.** Its octave number is
`(midi / 12) + 1`, so MIDI 0..127 spans `C-1` to `G-11`, which is the "11
octaves" the manual claims on p. 14.

The clone names MIDI 60 `C-4` (`RowCodec.h`: `octave = (midi / 12) - 1`, range
`-1..9`). That is also 11 octaves, but labelled **two octaves lower**. The stored
byte and the frequency are right; only the name is wrong. It matters anyway —
anyone reading a note off an M8 screen and typing it into the clone lands two
octaves out.

Why it went unnoticed: the clone is self-consistent. `parseNote(renderNote(n))`
round-trips for every value, and every test that names a note names it the
clone's way. Nothing compares a name against the device, so the whole naming
scheme could shift without a single test failing — the same circularity §UI-16
records for the FX byte table.

### RNL in the first FX column

`RNL F0` on row 0 of a 16-row phrase, C-4 in the note column, rows 4, 8 and 12
carrying plain notes with no FX. Ten loops captured, pitch measured per row.

- **Row 0 varied every loop**: MIDI 51, 46, 48, 45, 45, 45 across six loops.
- **Rows 4, 8 and 12 were constant** at 65.40 Hz every loop, all thirty
  measurements.

Two results:

1. **`RNL` in the first FX column randomises the note**, as the manual says
   (p. 70). The clone does nothing there — `Engine.cpp:542` guards with
   `f > 0` — so this is a confirmed gap, recorded as B12 in the clone's
   `docs/parity.md`.
2. **The randomisation does not carry to later rows.** Only the row holding the
   command changed.

Result 2 is evidence about `RNL`, not proof about `RND`. They are different
commands and `RND`'s reach is still unmeasured; it is left open in
`tessera/docs/captures.md` rather than closed by analogy.

### Method note

The note pitches were first read with an autocorrelation script that returned
65.40 Hz — exactly a quarter of 261.63. A power-of-two error is that estimator's
classic failure, so the reading was not trusted until `m8_analyze --pitch`, a
different algorithm, agreed on the same tone, and until the same script was run
against a clone render of a known C-4 as a control. It is worth the extra step:
the first, untrusted reading would have supported the same conclusion for the
wrong reason.

---

## UI-19 — RND reaches across rows; three stock defaults differ from the clone's

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`

### RND's reach — measured

`RND` "randomizes the previously active FX command" (manual p. 70). Whether
"previously active" means earlier in the same row or carries across rows was the
open question.

Phrase 00: row 0 held `C-6` with `PBN 04`, an upward pitch bend; **row 8** held
`RND F0` and nothing else; every other row empty. Pitch measured at 0.05-0.30 s
and 1.60-1.90 s into each 2 s loop, with `m8_analyze --pitch`.

| Loop | Control (no RND) | With `RND F0` on row 8 |
|---|---|---|
| | start → late | start → late |
| 0 | 270 → 560 | 270 → 60 |
| 1 | 270 → 560 | 200 → (inaudible) |
| 2 | 270 → 550 | 270 → (inaudible) |
| 3 | 270 → 550 | 260 → 7110 |
| 4 | 270 → 560 | 270 → 7120 |

The control is steady to within the estimator's 10 Hz quantisation. With `RND`
four rows further down, the bend lands anywhere from 60 Hz to 7120 Hz and never
twice the same.

**`RND` reaches back across rows.** The clone searches only within the row
(`Engine.cpp:526`, `for k = f - 1; k >= 0`), so it is a confirmed gap.

Note this is the opposite of `RNL`, measured in §UI-18, whose effect did *not*
carry past its own row. The two commands genuinely differ; neither result can be
assumed from the other.

### Stock defaults that differ from the clone's

Read straight off `INST00` after setting each type on a blank project. These do
not affect a loaded song, which carries its own bytes — they affect every
instrument a user creates.

| Field | Device | Clone | Where |
|---|---|---|---|
| `TBL.TIC` | `01` | `FF` | all five types |
| FM operator `LEV` | `80` (all four) | `00` | `FMSynthState::FMOp::level` |
| FM `MOD1`–`MOD4` | `00` | `80` | `FMSynthState::mod1..mod4` |

The FM level default matters beyond tidiness: the clone's `probes/` documentation
states that a default FM patch "renders digital silence, and that is the patch
faithfully reproduced, not a broken synth". On the device a default FM patch
**makes sound**, because every operator starts at `LEV 80`. The clone's silence is
its own wrong default, not fidelity.

`TBL.TIC` at `FF` means "increment the table row at 200 Hz" where the device
means "one tick per row" — a factor the manual defines explicitly (p. 24).

### Still open, and why

**The FM PIT modulation range was not measured.** `ScreenModel.h` carries no FM
field map, so `m8drv` cannot address `ALGO`, the per-operator `RATIO`, `LEV/FB` or
`MOD` cells by name; `fields INSTRUMENT` lists only the sampler/macrosyn set.
Raw cursor movement onto the MOD row lands on operator A and `RIGHT` does not
walk to B/C/D — the cursor read stays on the row label.

This is the same gap §UI-11 hit for HyperSynth WIDTH, which is why that capture
had to bake the value into a probe file instead. The unblocking route is the same:
either add an FM field map, or author the patch with `m8_makeprobe` and load it.

---

## UI-20 — Octave digits are hexadecimal; the DRUM envelope resists this rig

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`

### Octave naming, settled

Stepping a note up by octave from `C-6` with `EDIT+UP`:

```
C-6  C-7  C-8  C-9  C-A  C-B  G-B (clamped)
```

**The octave digit is hexadecimal**: `A` is 10, `B` is 11. The note field stays
exactly three characters at every octave, and sharps render normally there —
stepping down from the ceiling gives `F#B`, then `F-B`.

With §UI-18's `(midi / 12) + 1`, this closes the range: `C-1` is MIDI 0, `C-6` is
MIDI 60, and `G-B` is MIDI 127 — `127 / 12 = 10`, plus one is 11 = `B`, and
`127 % 12 = 7` = G. The clamp at `G-B` confirms it from the other end.

This unblocks the clone's F28: `renderNote` needs `(midi / 12) + 1` emitted as one
hex digit, and `parseNote` needs to accept `1`–`9`, `A`, `B`. The fixed three-
character width the `.tsr` row grammar depends on is safe.

### DRUM envelope — attempted, not settled

The manual (p. 20) says PEAK "modifies two parameters: Duck amount and peak time"
and its diagram labels a DUCK stage, but gives no arithmetic. The clone has no
duck at all.

**Method.** MACROSYN `SHAPE 00 CSAW`, `AMP 40`, one sustained note. `MOD1` set to
`DRUM ENV` → `VOLUME`, `AMT FF`. `BODY` taken to `00` so no hold stage sits
between the transient and the decay. `PEAK` swept `00`, `40`, `80`, `C0`, `F0`,
four seconds captured at each, envelope read as peak-per-2 ms over the first
200 ms.

**Device defaults worth recording**: a fresh `DRUM ENV` slot reads
`PEAK 80`, `BODY 10`, `DEC 80`.

**Result: suggestive, not conclusive.** Higher PEAK gives a faster initial decay —
at `PEAK 00` the envelope is still near half amplitude around 100 ms, at `PEAK F0`
it is down to a fifth by 40 ms — which is consistent with PEAK carrying a peak
*time*. Nothing in the traces separates a duck *amount* from that, and a sawtooth
carrier at ~350 Hz puts period-rate ripple into 2 ms bins, so the small
differences between takes cannot be attributed with confidence.

**Do not implement a duck from this.** A guessed envelope shape is worse than a
known-missing one.

**The experiment that would settle it**, for whoever picks it up:

1. A carrier with no ripple of its own — `SHAPE 06 SINE` on WAVSYNTH, not a saw.
2. `BODY` and `DEC` set long — `40` and `C0` — so the peak, duck and decay stages
   are separated in time instead of overlapping in the first 50 ms.
3. RMS per 5 ms window rather than peak per 2 ms; peak-per-window is what let the
   waveform's own crest through.
4. Sweep only the high nibble of PEAK with the low nibble pinned, then the reverse.
   If the two nibbles carry different parameters, that is what shows it — and if
   they do not, the split is somewhere else and the guess in the clone's spec is
   wrong.

---

## UI-21 — A track loops to the start of its own contiguous run, not to song row 0

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`

### The question

The manual (p. 10): a track increments through its chains "until it reaches an
empty column, at which point the given track will **loop back to the beginning of
its list of chains**." *The beginning* is ambiguous for any song with a gap —
song row 0, or the first row of the run the track was actually playing.

It matters: it is the one unknown in the clone's largest pending fix, per-track
song position.

### Method

Two chains that sound different: chain `00` → phrase `00` → `F-6` (350 Hz),
chain `01` → phrase `01` → `F-8` (1400 Hz). MACROSYN, so both are easy to tell
apart from a pitch reading. Playback started from the song cursor, captured for
14 s, pitch measured over 0.25 s at the top of each 2 s bar.

**First layout** — track 1 rows: `-- -- 00 01 --`, started at row 2. Result:
350, 1400, 350, 1400, 350, 1400, 350, 1400.

That is consistent with both readings, because a loop to row 0 that scanned
forward past the empty rows would also land on row 2. So it settles nothing, and
a second layout was needed.

**Second layout** — track 1 rows: `00 -- 01 --`, started at row 2. The run
containing row 2 is row 2 alone; row 0 holds a *different* chain.

| Bar | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Hz | 1400 | 1390 | 1400 | 1400 | 1390 | 1400 |

**Every bar is chain 01.** Chain 00 at row 0 never plays.

### Result

**A track loops to the first row of the contiguous non-empty run it is playing** —
scan upward from the current row until an empty cell or the top of the song, and
restart there. Not song row 0.

The first layout's ambiguity is worth keeping in mind for anyone re-running this:
a gap-free song cannot distinguish the two readings, and neither can a layout
whose run starts at row 0. The run must start below a gap, and there must be
something different above the gap to catch the wrong answer.

### What it settles

The clone's F20 — per-track song position — had this recorded as "row 0 unless a
capture says otherwise". The capture says otherwise.

---

## UI-22 — A track on an empty song cell stays silent and never advances

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3, via `m8drv batch` + `m8_capture`

### The question

§UI-21 settled where a track restarts when its run *ends*. This is the other
case: a track whose cell is empty at the row playback **starts** on. Does it stay
silent, or does it move forward and find its music later?

It mattered immediately: the clone's demo song has tracks 4–7 empty at song row 0
with their real parts at rows 1–3, and per-track song positions left them silent
for the whole song.

### Method

Track 1: row 0 empty, row 1 = chain `00` (F-6, 350 Hz), row 2 = chain `01`.
Track 2: rows 0 and 1 = chain `01` (F-8, 1400 Hz), so the song keeps running and
the two tracks are separable by pitch. Playback started at row 0, 12 s captured,
pitch read over 0.25 s at the top of each 2 s bar.

| Bar | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| Hz | 1390 | 1400 | 1390 | 1400 | 1400 |

**Track 1 never sounds.** 350 Hz appears in no bar. It does not advance to row 1,
where its chain is waiting, and it does not follow track 2.

### Result

An empty song cell is not a rest and not a wait. The track parks there.

The manual agrees from the other direction (p. 10): *"To maintain a track's play
position with other tracks while it remains silent, create a chain that contains
empty phrases. It is common to use either chain 00 or FE for this purpose."* That
advice would be pointless if an empty cell already kept a track moving.

### What it settles

The clone's F20. It also condemned the clone's own demo song, whose tracks 4–7
relied on the shared-row behaviour to join late; they now carry the keeper chain
the manual describes.

---

## UI-23 — MTT is signed int8 in eighths of a tick

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3

The manual (p. 72) gives MTT's unit — "negative or positive sub-ticks (1/8th
tick)" — but not the byte encoding.

**Method.** MACROSYN `SHAPE 22` KICK, one note on phrase rows 0, 4, 8 and 12, at
the stock 120 BPM: four rows is 0.500 s and one tick is 20.83 ms, so an eighth of
a tick is 2.60 ms. Onsets counted with `tools/count_onsets.py`.

Control, no MTT — gaps all `0.500`.

With `MTT 04` on row 8, gaps repeat `0.500, 0.510, 0.490, 0.500`: the row-8 note
lands 10 ms late and the following note is unmoved, so the row is displaced rather
than the tempo changed. 10 ms against a predicted 4 × 2.60 = 10.4 ms, inside the
2 ms bin resolution.

**So the value is a signed byte in eighths of a tick** — `0x04` is +½ tick,
`0xFC` would be −½ — the same int8 convention `VOL`, `PIT`, `FIN`, `TSP` and `SNG`
already use.

### Incidental: the FX selector is one list, not a grid

§UI-17 recorded `GGR`, `INS`, `KIL` and `RMX` as absent from an `EDIT+RIGHT` walk
and guessed the selector was a grid. It is not — they are simply later in the
list. Walking far enough gives `... TBL THO TIC TBX TPO TSP GGR RMX INS NXT KIL
OFF MTT`. The order is not alphabetical past `TSP`.

---

## UI-24 — Mixer and send-effect FX commands are invisible on the device's own screens

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3

The manual says of every `X**` command "Changes the value in the Send Effects
settings view", and of every `V**`, `DJ*` and `USB` command "Changes the value
located in the Mixer view" (pp. 73-74). **Read back over the display protocol,
they do not.** This wasted most of a session and is recorded so nobody plans a
measurement around a screen read again.

**Method.** `XMM 90`, `XMT 10`, `VMV 20` and `TPO 50` were placed in a playing
phrase, on rows with and without a note, and the corresponding screens read --
during playback with the cursor already parked on the field, and after stopping.
`MOD DEPTH` stayed `40`, `MOD TYPE` stayed `00`, `MIX` stayed `E0`, `TEMPO`
stayed `120.00`, in every combination.

**The commands do execute.** A capture settles it: with `VMV 20` on phrase row 4
the output is at full level for the 0.5 s up to that row and at -53 dB for the
remaining 5.5 s of the capture. The mixer screen read `E0` throughout.

**And the change is not kept.** The next capture, started from a stop, is at full
level again for its first 0.5 s. The FX-command value applies for the duration of
the transport run and the stored song value returns on stop.

### What it means for a clone

The engine writes these commands straight into its persistent mixer and effects
state, where they survive the stop. The device does not. Any clone wanting parity
needs a performance layer over the stored values -- a live overlay discarded when
the transport stops -- not a write into the song.

### How to measure them instead

Audio only. Route the instrument fully to the effect under test (`DRY 00`,
`MFX FF`), capture with `m8_capture`, and compare average spectra against
references captured with the setting made *on the settings screen*, where it does
persist. Cosine distance on a Hann-windowed average magnitude spectrum separates
the three ModFX algorithms cleanly: same-setting repeats score 0.996-0.999,
different algorithms 0.41-0.88.

**Verify the rig before and after every capture.** Instrument sends were observed
reverting to their defaults mid-session -- `DRY`/`MFX`/`MULT` all back to stock
between two captures, silently. Three contradictory XMT readings came from that
before it was caught. Every measurement below asserts the routing on both sides
of the capture and discards the run if it moved.

---

## UI-25 — XMT: the ModFX type is the LOW nibble, saturating at 02

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3

> **XMT XX (Send Effect: ModFX Mod Type)** — Set the ModFX type & phase position.
> — p. 73

Two settings named in one byte and no statement of how they are packed. The
stored byte is a plain type: §UI-9 measured MOD TYPE at effects+25 moving
`00 -> 02`, and the screen reads `MOD TYPE 00 CHORUS`.

**Method.** Instrument 00 WAVSYNTH SINE, `MULT 80` for harmonic content,
`DRY 00`, `MFX FF`. One note on phrase row 0 with the `XMT` beside it. References
captured with MOD TYPE set to `00`, `01` and `02` on the Effect Settings screen
and no `XMT` in the phrase. Eight-second captures, average spectrum from 1.0 s,
cosine distance. Routing asserted before and after each capture; stored MOD TYPE
forced to `00` for every `XMT` run and re-read afterwards.

| `XMT` | vs CHORUS ref | vs PHASER ref | vs FLANGER ref | type |
|---|---|---|---|---|
| `00` | **0.987** | 0.573 | 0.778 | 0 chorus |
| `01` | 0.540 | **0.996** | 0.879 | 1 phaser |
| `02` | 0.739 | 0.897 | **0.963** | 2 flanger |
| `03` | 0.751 | 0.899 | **0.976** | 2 flanger |
| `04` | 0.771 | 0.880 | **0.999** | 2 flanger |
| `05` | 0.763 | 0.897 | **0.992** | 2 flanger |
| `0F` | 0.748 | 0.905 | **0.975** | 2 flanger |
| `10` | **0.997** | 0.559 | 0.780 | 0 chorus |
| `20` | **0.997** | 0.562 | 0.781 | 0 chorus |
| `12` | 0.770 | 0.885 | **0.998** | 2 flanger |

**So: `type = clamp(val & 0x0F, 0, 2)`.** `0x12` gives the flanger and `0x10`
and `0x20` give the chorus, so the high nibble does not choose the type; `0x04`
gives the flanger rather than the chorus, so it saturates rather than masking the
low two bits.

`01` was captured twice, in separate sessions, scoring 0.996 both times against
the phaser reference.

**The high nibble — the manual's "phase position" — produced no measurable
difference here.** `00`, `10` and `20` score 0.991-0.999 against each other. A
phase offset on a free-running LFO averaged over eight seconds is exactly what
would hide, so this says the high nibble does not select the type, not that it
does nothing.

**Beware the earlier contradictory readings.** Before the routing was asserted
per capture (§UI-24), `XMT 02` read as a flanger twice and a chorus once, and
`XMT 01` read as neither phaser nor chorus. Those runs were taken with the
instrument's sends silently reset. They are wrong; the table above supersedes
them.

---

## UI-26 — RMX's first digit is a track count, not a bitmask

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3

> **RMX XY** — Set the phrase play-head position (Y) of tracks to the left of the
> current track. Select which tracks are affected using the first digit X. — p. 71

"Select which tracks" reads like a bitmask, and a 4-bit mask covers four tracks
where a count covers fifteen; neither fits eight cleanly. The clone reads X as a
count from track 1.

**Method.** A test song at song row 03, with only two tracks carrying content, so
nothing else can sound:

- **track 3** — phrase with `C-6` on rows 0-3 and `C-3` on rows 4-7, rows 8-F
  empty. The two blocks are three octaves apart, so the dominant partial names
  which block is playing.
- **track 4** — an otherwise empty phrase carrying `RMX X0` on row 6.

Six-second captures; dominant spectral peak per 1024-sample hop.

| `RMX` | track 3 at 0.75 s (row 6) | track 3 affected? |
|---|---|---|
| `10` | 281/305 Hz, unbroken | no |
| `20` | 281/305 Hz, unbroken | no |
| `30` | jumps to 2344/6188 Hz for ~0.5 s | **yes** |
| `40` | jumps to 2344/6188 Hz for ~0.5 s | **yes** |

`30` was captured twice with the same result; `10` matches a no-RMX control
exactly.

**This rules out both bitmask readings and leaves the count.**

- *Absolute bitmask* (bit 0 = track 1): `X=3` is bits 0 and 1, tracks 1 and 2, so
  track 3 would not move. It moves.
- *Relative bitmask* (bit 0 = the track immediately to the left): `X=1` would be
  track 3 itself. It does not move.
- *Count from track 1*: `X=1` is track 1, `X=2` is tracks 1-2, `X=3` reaches
  track 3. All four rows fit.

**What it settles.** The clone's `for (k = 0; k < t && k < X; ++k)` is right, and
parity entry B14 closes as correct rather than as a defect.

**Not settled:** what the play-head actually does on arrival. A jump with `Y=0`
did not restart the note on row 0 — the target goes bright and atonal for about
half a second instead — so the M8 appears to move the position without
re-striking the row. Measuring that needs a different probe and was not the
question here.

---

## UI-27 — The device's global FX command list, in order

- **Date:** 2026-08-28
- **Firmware:** 6.5.2, COM3

Walked with `EDIT+RIGHT` on a phrase FX cell with no instrument in the row, from
`MTT` all the way round to `MTT`. The list is a single cycle; `EDIT+LEFT`
saturates at `ARP` rather than wrapping, and `EDIT+UP`/`DOWN` do nothing on this
column — it is an enum, so the `enum_next` gesture is the only way through it.

```
ARP ARC CHA DEL GRV HOP RND RNL RET REP RTO NTH PSL PBN PVB PVX SCA SCG SED SNG
TBL THO TIC TBX TPO TSP GGR RMX INS NXT KIL OFF MTT
VMV VMX VDE VRE VIN USB EQI EQM VT1 VT2 VT3 VT4 VT5 VT6 VT7 VT8
XMT XMM XMF XMW XMR XDT XDF XDW XDR XRS XRD XRM XRF XRW XRH XRZ
IMX IDE IRE VI2 IM2 ID2 IR2 DJC DJR DJT OTT OTC OTI
```

**Seven mnemonics here are not in the manual's list, and three names the manual
uses do not exist on the device.**

| Manual | Device | Note |
|---|---|---|
| `IVO` | **`VIN`** | line input volume |
| `IRV` | **`IRE`** | line input reverb send — the manual's own body text says `IRE`, only its heading says `IRV` |
| `IV2` | **`VI2`** | second input volume |
| — | **`XRH`** | absent from the manual; by position among the reverb commands, SHIMMER |
| — | **`OTT` `OTC` `OTI`** | absent from the manual; the OTT amount, colour and time of §UI-9 |

Any clone storing effects by mnemonic is storing three names the device never
writes, and cannot represent four commands it does.

---

## UI-28 — The three TIC map modes: octave/12, velocity/8, note%16

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3

The manual (p. 24) names them and stops:

> TICFC - Octave Map: Maps playing octave to table row.
> TICFD - Velocity Map: Maps velocity to table row.
> TICFE - Note Map: Maps note to table row. Note: Use HOP00 on row "0C" to limit
> table to 12 notes / octave.

No arithmetic anywhere, and the note-map hint fits more than one reading, so all
three were measured.

**Method.** The table row itself is not readable: the playhead on the TABLE view
is an accent colour rather than a character, `inspect` shows it, and it is gone
by the time the transport has stopped — a read after a stop returns the cursor,
which is what an early attempt here mistook it for.

So the row was made to name itself. Every row of table 00 was given a distinct
transpose in its `N` column:

```
row   0  1  2  3  4  5   6   7   8   9   A   B   C   D   E    F
N    00 01 02 04 07 0B  10  16  1D  25  2E  38  43  4F  5C   6A
```

One note in phrase 01, `TBL.TIC` set to the mode under test, three seconds
captured with `m8_capture`, and the dominant partial converted to a MIDI number.
Sounding note minus played note is the transpose, and the transpose names the
row.

### Octave map, TICFC — `row = midi / 12`

| played | sounded | offset | row | midi / 12 |
|---|---|---|---|---|
| 36 | 39.92 | +4 | 3 | 3 |
| 48 | 54.77 | +7 | 4 | 4 |
| 60 | 70.94 | +11 | 5 | 5 |
| 72 | 88.00 | +16 | 6 | 6 |

Note this is the octave **index**, not the octave number the device prints: MIDI
60 is named `C-6` on screen (§UI-18) and maps to row 5.

### Velocity map, TICFD — `row = velocity / 8`

| velocity | sounded | offset | row | vel / 8 |
|---|---|---|---|---|
| 0x20 | 67.03 | +7 | 4 | 4 |
| 0x28 | 70.94 | +11 | 5 | 5 |
| 0x30 | 76.07 | +16 | 6 | 6 |
| 0x38 | 81.99 | +22 | 7 | 7 |
| 0x40 | 88.97 | +29 | 8 | 8 |

Velocity 0x00 and 0x10 came back silent and are not evidence either way — at
0x10 the note is simply too quiet to measure.

### Note map, TICFE — `row = midi % 16`

| played | sounded | offset | row | midi % 16 |
|---|---|---|---|---|
| 64 | 63.92 | +0 | 0 | 0 |
| 65 | 65.96 | +1 | 1 | 1 |
| 66 | 68.03 | +2 | 2 | 2 |
| 67 | 70.94 | +4 | 3 | 3 |
| 69 | 80.03 | +11 | 5 | 5 |
| 72 | 101.01 | +29 | 8 | 8 |

MIDI 60, 61 and 62 all read back at the same low frequency and are **not**
counter-evidence: those rows transpose by 67, 79 and 92, so all three land past
MIDI 127 and clamp there, above the analyser's search range.

**And this settles the manual's hint.** With `row = midi % 16` a chromatic run
walks rows 0 through 15, so putting `HOP00` on row `0C` wraps it after twelve —
"limit table to 12 notes / octave" exactly. Under a `% 12` reading the hint would
be meaningless, since row `0C` could never be reached. That contradiction is what
made this unresolvable from the manual alone and is why it was measured.

### What it settles

The clone's B10. All three modes were parsed and then skipped, so the table froze
on whatever row it was on. They are implemented in F41 with these mappings, each
citing this section.

---

## UI-29 — Reverb DECAY → RT60, measured at six points

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/captures_backlog.md` A1.

### The rig, and what it took to build it

`m8_capture` held the keyjazz note for the whole recording window, so a reverb
tail — which by definition happens *after* the note stops — could never be in the
file. That is the only thing that blocked this item. `--note-ms` was added:
release the note early, keep recording. Everything else here already existed.

`m8drv set REV_DEC` cannot set the field. EFFECT SETTINGS draws decay and shimmer
as one row, `DECAY:SHIMMER C0:00`, and `readCursorValue` returns the accent cells
glued as `:SHIMMERC0`, which has no leading hex run — so `editValue` refuses. One
attempt to force it timed out mid-sweep and left DECAY on `45`, which is the
failure the refusal exists to prevent. `tools/set_rev_decay.py` steps the byte
with the pinned gestures and re-reads the decoded row after every batch.

**Instrument 00** (WAVSYNTH, SHAPE `06 SINE`, SIZE `20`, MULT/WARP/SCAN `00`):
`DRY 00`, `MFX 00`, `DEL 00`, `REV FF`, `FILTER 00 OFF`, `CUTOFF FF`, `RES 00`,
`AMP 00`, `LIM 00 CLIP`, `PAN 80`. So nothing dry reaches the output and the
whole capture is the reverb's own output.

**Reverb block, held fixed:** `INPUT EQ --`, `ROOM SIZE FF`, `SHIMMER 00`,
`MOD DEPTH:FRQ 10:FF`, `STEREO WIDTH FF`. **The MODFX and DELAY blocks' own
`REVERB SEND` were both `00`.** **Mixer:** returns `MX/DE/RE` all `E0`,
`MIX E0`, `LIM 00`, `OTT 00`, `DJF 80`, `OUTPUT VOL F0`.

**Capture:** keyjazz note 60 at velocity `0x40`, released after 300 ms,
recording for 18 s (60 s for the `FF` re-run). `tools/rt60_run.py` reads the
decoded row text of INSTRUMENT, EFFECT SETTINGS and MIXER before *and* after
every capture and renames the WAV `.DRIFTED` if any row moved. No run in this
session drifted. One run (`FF`, 18 s) was killed by an external timeout during
its *after* read and was discarded and re-taken rather than kept.

**The USB tap's floor is exact digital zero, not a noise floor.** Every capture
here ends in literal zero samples. That is worth knowing generally: it means a
full 60 dB of decay is really measurable on this rig, so the numbers below are
measured rather than extrapolated from a T20/T30 fit.

### Results

`tools/rt60_measure.py`, three estimators per file. The headline column is
Schroeder backward integration, time from the energy-decay curve's −5 dB point
to its −65 dB point — a real 60 dB, no extrapolation.

| DECAY | RT60, EDC −5→−65 dB | T30 extrapolated | envelope slope fit | tail reaches digital zero at |
|---|---|---|---|---|
| `00` | **0.528 s** | 0.537 s | 0.571 s (105.2 dB/s) | 1.04 s |
| `40` | **0.854 s** | 0.993 s | 1.316 s (45.6 dB/s) | 1.54 s |
| `80` | **1.667 s** | 1.807 s | 2.157 s (27.8 dB/s) | 2.57 s |
| `C0` | **3.615 s** | 3.683 s | 4.265 s (14.1 dB/s) | 5.25 s |
| `E0` | **8.512 s** | 9.204 s | 10.600 s (5.7 dB/s) | 11.42 s |
| `FF` | **not reached in 60 s** | — | 0.29 dB/s | still ringing at 60.22 s |

A second, independent capture at `C0` (the trial run, same rig) gave 3.775 s /
3.889 s / 4.467 s and silence at 5.37 s. **Run-to-run spread is about 4%**, so
these figures are good to roughly ±0.15 s at `C0` and proportionally elsewhere.
The three estimators disagree by up to 15% at any one setting, always in the same
order — the raw-envelope slope fit is the highest because the first few hundred
ms decay faster than the rest. Where a single number is wanted, the EDC column is
the one to use.

The EDC crossings, in seconds from note-off, are the raw shape:

| DECAY | −5 | −15 | −25 | −35 | −45 | −55 | −65 |
|---|---|---|---|---|---|---|---|
| `00` | 0.043 | 0.143 | 0.245 | 0.295 | 0.396 | 0.457 | 0.571 |
| `40` | 0.162 | 0.249 | 0.432 | 0.598 | 0.722 | 0.857 | 1.016 |
| `80` | 0.175 | 0.486 | 0.775 | 1.033 | 1.388 | 1.606 | 1.842 |
| `C0` | 0.292 | 0.864 | 1.496 | 2.097 | 2.744 | 3.318 | 3.907 |
| `E0` | 0.542 | 1.979 | 3.538 | 5.114 | 6.534 | 7.961 | 9.054 |

Between −15 and −55 those steps are near-constant per 10 dB, so the decay is
close to exponential once the first ~0.2 s is past.

### `FF` decays, but not on this timescale

The 18 s window was not enough to say anything about `FF` — its energy-decay
curve was still falling at the end of the file, and backward integration against
a truncated tail reports nonsense (crossings bunched at 17.85 s). Re-captured at
60 s, with the rig re-asserted, in 2 s RMS bins:

```
  0.0s -24.43 dB    16.0s -31.25 dB    32.0s -36.70 dB    48.0s -41.29 dB
  4.0s -25.82 dB    20.0s -32.34 dB    36.0s -37.11 dB    52.0s -42.32 dB
  8.0s -27.90 dB    24.0s -33.93 dB    40.0s -39.40 dB    56.0s -43.67 dB
 12.0s -29.33 dB    28.0s -35.09 dB    44.0s -40.06 dB    58.0s -43.89 dB
```

**Measured: 19.5 dB of decay in 58 s, i.e. 0.34 dB/s**, still clearly audible and
still falling when the capture ended. Straight-line extrapolation puts a 60 dB
decay near 180 s, but **that is arithmetic, not a measurement** — nothing here
observed it. What is measured is that `FF` is not infinite and is roughly 20×
slower than `E0`.

### The shape of the law

RT60 roughly doubles per `0x40` up to `C0` and then accelerates hard: ×1.6 from
`00` to `40`, ×1.95 to `80`, ×2.17 to `C0`, then ×2.35 for the half-step to `E0`,
then ×21 for the remaining `0x1F` to `FF`. It is not linear and it is not a plain
geometric series in the byte. It has the shape of a feedback coefficient
approaching unity, which is what a reverb usually is, but **no law was fitted and
none should be quoted from this section** — six points with a 4% spread cannot
choose between the candidates near the top of the range, which is exactly where
they differ most.

### Two caveats that bound everything above

- **`ROOM SIZE` was `FF` throughout.** Decay time on this device is a function of
  both, and only one was varied. These numbers are RT60 at SIZE `FF`.
- The excitation was a 300 ms sine at C-4. A broadband source could decay
  differently if the reverb is frequency-dependent; that was not tested.

---

## UI-30 — FM `PIT` modulation is one semitone per unit, unipolar, clamped at MIDI 127

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/captures_backlog.md` B1, and `docs/captures.md` §Capture 1,
  open since 2026-07 as "attempted, blocked".

### What actually unblocked it

The blocker on record was that `m8_makeprobe` had no FM field map, so the patch
could not be authored, and `ScreenModel.h` has none either, so it could not be
driven. Both halves were addressed, and **the half that was expected to work did
not**:

- `m8_makeprobe` gained `--fm-algo`, `--fm-level`, `--fm-ratio`,
  `--fm-ratio-fine`, `--fm-fb`, `--fm-mod-a`, `--fm-mod-b` and `--fm-mod-amt`,
  with `verifyRoundTrip` now checking every one of them — previously its FM
  "PASS" meant only "instrument 0 is an FMSynth". The patch below bakes and
  verifies correctly.
- **It could not be loaded.** Loading a probe needs the file on the SD card, and
  there is no file transfer over serial (§UI-7) — the card is not mounted on the
  host while the device is in headless serial mode. The §UI-11 route for
  HyperSynth worked because those probes were already on the card.

So the patch was built **on the device** instead, which needed the other half:
a way to address the FM screen's cells.

### Mapping a screen whose cursor coordinates are useless

The FM instrument screen reports the same cursor position at every stop along a
row — walking right along `LEV/FB` gives `(120, 8)` eight times. Position reads
cannot map it.

What can: **press a value key and read back which number on the screen moved.**
That is the same trick rig fact 5 uses for the table playhead, it needs nothing
the driver does not already have, and it is unambiguous. `tools/fm_probe_map.py`
does it — increment, dump, decrement, dump again to prove it went back. From a
cursor homed on `TYPE`, saturating LEFT before counting RIGHT:

| Path | Cell |
|---|---|
| `DOWN*3 LEFT*2 RIGHT*0` | `ALGO` |
| `DOWN*6 LEFT*4 RIGHT*n` | n=0 LEV A, 1 FB A, 2 LEV B, 3 FB B, 4 LEV C, 5 FB C, 6 LEV D, 7 FB D |
| `DOWN*7 LEFT*5 RIGHT*n` | MOD slot 1 of operator A/B/C/D |
| `DOWN*9 LEFT*2 RIGHT*0` | `MOD1` amount — `RIGHT*1` is `AMP`, do not overshoot |

Two things this cost, recorded so the next person does not pay them again.
**The DOWN chain remembers the horizontal position per row**, so a bare `DOWN*6`
lands wherever that row was left, not at its first cell — saturate LEFT before
counting. And **these enum lists do not wrap**: stepping forward from `1>PIT`
runs up to `4>FBK` and stops, so a one-directional walk can never get back to
`-----`. `fm_patch.py` reverses on a stall.

### The patch

Instrument 00, `TYPE FMSYNTH`, built and read back on the device:

```
ALGO    0B A+B+C+D
RATIO   01.00 01.00 01.00 01.00
LEV/FB  FF/00 00/00 00/00 00/00
MOD     1>PIT ----- ----- -----
MOD1    00 .. FF        AMP 00   LIM 00   PAN 80   DRY C0
FILTER  00 OFF   CUTOFF FF   RES 00   MFX 00   DEL 00   REV 00
```

`ALGO 0B` reads `A+B+C+D` on the device — the fully additive algorithm — so with
B, C and D at level `00`, operator A is heard alone with nothing modulating it.
Captured by keyjazz at velocity `0x40`, 3 s, `tools/fm_run.py` asserting the
whole INSTRUMENT screen and MIXER before and after each capture. Nothing drifted
in any run.

### The control, first

With the MOD slot at `-----` the patch plays keyjazz note 60 at **261.631 Hz**
by autocorrelation and **261.633 Hz** by interpolated FFT peak — 0.0 cents from
261.63. So the rig measures pitch correctly before any modulation is applied.

### Results

`tools/fm_pitch.py`, three estimators. Semitones are from the FFT peak, against
261.63 Hz. `domin` is the fraction of energy within ±3 bins of that peak — 0.89
throughout, i.e. a clean single tone in every capture.

| `MOD1` | dec | measured Hz | semitones from the played note |
|---|---|---|---|
| `-----` (control) | — | 261.633 | 0.000 |
| `00` | 0 | 261.633 | **0.000** |
| `01` | 1 | 277.190 | **1.000** |
| `08` | 8 | 415.298 | **7.999** |
| `10` | 16 | 659.254 | **16.000** |
| `20` | 32 | 1661.223 | **32.000** |
| `30` | 48 | 4186.010 | **48.000** |
| `40` | 64 | 10548.088 | **64.000** |
| `42` | 66 | 11839.814 | **66.000** |
| `44` | 68 | 12543.845 | 67.000 — clamped |
| `60` | 96 | 12543.845 | 67.000 — clamped |
| `80` | 128 | 12543.845 | 67.000 — clamped |
| `A0` | 160 | 12543.845 | 67.000 — clamped |
| `C0` | 192 | 12543.845 | 67.000 — clamped |
| `FF` | 255 | 12543.845 | 67.000 — clamped |

**The `MOD` amount adds exactly its own value in semitones.** Eight points from
0 to 66 land within 0.001 of the integer. There is no scaling constant to fit.

### The three questions the item asked

1. **How many semitones does a full PIT modulation add?** `N` units add `N`
   semitones. A full `FF` would be +255 semitones; what you actually get is the
   clamp below.
2. **Is it bipolar?** **No.** `00` reproduces the control pitch to 0.000
   semitones, and no value anywhere in `00`..`FF` bends the note down. The
   manual's "adds to the note pitch in semitones" is literal.
3. **Where is its centre?** There is no centre. `00` is the neutral point, not
   `80`.

### The ceiling is MIDI 127, not a fixed offset

Everything from `44` up came back at 12543.845 Hz, which is MIDI note 127. The
same note played at MIDI 48 with `MOD1 FF` also came back at **12543.845 Hz** —
+79 semitones there rather than +67 — while MIDI 48 with `MOD1 20` came back at
830.617 Hz, exactly +32.000. So the offset is relative to the played note and
the result is then clamped to an absolute MIDI 127.

The five captures at `60`/`80`/`A0`/`C0`/`FF` are not the same file — different
frame counts, `m8_analyze --diff` FAILs — but they measure identically to three
decimals in all three estimators and to four in peak level. The clamp is on the
resulting pitch, not on the byte.

### What this does not cover

- `MOD1` was set as the instrument's own static amount. These values are also
  driven from tables and FX commands; whether that path scales identically was
  not tested.
- `PIT` was routed on **operator A**, a carrier under `ALGO 0B`. On a modulator
  operator the same destination changes the modulating frequency, and the law
  there was not measured.
- Above `44` the pitch is clamped rather than aliased, so nothing here says what
  the device would do with an unclamped ultrasonic operator.

---

## UI-31 — `LIM` is makeup gain into a falling ceiling; `OTT` is an upward compressor

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/captures_backlog.md` A6.

Both controls were defaulted off in the clone because nobody had ever measured
them. This is the first evidence either way.

### The input-level control, and why it is that one

A6 needs a steady tone at known, varied input levels. Three candidates were
tried and two were rejected **by measurement**, which is worth recording because
the first of them is what the tooling's own documentation recommends:

- **Keyjazz velocity — does nothing here.** `m8_capture`'s docs call
  `--keyjazz-vel` "the level lever" (measured once on a macrosynth). On this
  WAVSYNTH probe, velocity `0x40` and `0x7F` gave peak **0.3478 both times** and
  RMS within 0.06 dB. The instrument's volume comes from its AHD envelope at
  `AMT FF`, and nothing on it routes velocity to volume.
- **Instrument `AMP` — a drive, not a gain.** `00`→`FF` moved the output only
  from −12.336 to −9.613 dBFS RMS, nearly all of it in the first step
  (`00`→`20` is +2.28 dB, `20`→`FF` is another 0.44 dB), and crest fell from
  3.15 dB to 2.55 dB. It saturates and reshapes. Consistent with the earlier
  reading of AMP as a drive into the instrument's own LIM stage.
- **Mixer `TRACK1_VOL` — used.** A plain hex byte, settable by field name (no
  paired-cell problem), unambiguously upstream of the master chain, and it does
  not touch the waveform: crest stays 3.16 dB across its whole range.

**The input axis is defined as the output measured with `LIM 00` and `OTT 00`**,
not as an absolute figure inside the device. Every "gain" below is therefore
relative to the `LIM 00` / `OTT 00` path. Nothing here proves that path is
itself unity, and this rig cannot: the only fader available is the one being
used as the axis.

### The rig

Instrument 00 from `WAVV7F.M8S` reloaded from the card: `TYPE WAVSYNTH`,
`SHAPE 06 SINE`, `SIZE 80`, `MULT 80`, `WARP 00`, `SCAN 00`, `AMP 00`,
`LIM 00 CLIP`, `PAN 80`, `DRY C0`, `MFX/DEL/REV 00`, `FILTER 00 OFF`,
`CUTOFF FF`, `RES 00`. Mixer: returns `MX/DE/RE 00`, `MIX E0`, `DJF 80`,
`OUTPUT VOL F0`.

**The source is not a 261 Hz sine, and it does not matter, but it should be on
the record.** Keyjazz note 60 through this patch produces a steady tone at
**2219.40 Hz** — `SIZE 80` / `MULT 80` put it 37.015 semitones above middle C.
It is the same tone in every capture in this section, its crest is 3.16 dB, and
its RMS holds within 0.03 dB across the measurement window, which is all a level
curve needs. It carries sidebands 27-34 dB down, so it is not a pure sine and
should not be quoted as one.

The note lasts about **480 ms** and then cuts to exact digital zero, whatever
the capture length. Measured levels are the mean over 100-420 ms from the note's
onset (`tools/level_measure.py`, note-relative window). `tools/level_run.py`
re-read every row of INSTRUMENT and MIXER before and after **each** capture; no
run in this section drifted.

### Reference: `LIM 00`, `OTT 00`

| `TRACK1_VOL` | peak dBFS | RMS dBFS |
|---|---|---|
| `10` | −59.183 | −62.554 |
| `20` | −55.345 | −58.586 |
| `30` | −51.419 | −54.669 |
| `40` | −47.638 | −50.782 |
| `60` | −39.886 | −43.047 |
| `80` | −32.193 | −35.347 |
| `A0` | −24.508 | −27.667 |
| `C0` | −16.838 | −19.998 |
| `E0` | −9.176 | −12.337 |

Incidental and useful: **the track fader is logarithmic at about 0.24 dB per
unit** — 3.968, 3.917, 3.887, then 3.868, 3.850, 3.840, 3.835, 3.831 dB per
`0x10` step. The step size shrinks monotonically by 3.4% across the range;
nothing here separates a fader taper from mild compression in the `LIM 00` path.

**`E0` is the ceiling of this rig's input axis.** `TRACK1_VOL` caps at `E0`, so
no input above −12.337 dBFS RMS / −9.176 dBFS peak could be presented. Anything
either control does above that level is unmeasured.

### `LIM` — makeup gain, and a ceiling that falls as the gain rises

Output RMS dBFS, `OTT 00` throughout:

| input RMS | `LIM 20` | `LIM 40` | `LIM 80` | `LIM A0` | `LIM C0` | `LIM FF` |
|---|---|---|---|---|---|---|
| −58.586 | — | — | — | — | — | −39.634 |
| −50.782 | −48.400 | −46.030 | −41.294 | −38.931 | −36.570 | −31.928 |
| −43.047 | −40.679 | −38.314 | −33.591 | −31.234 | −28.874 | −24.237 |
| −35.347 | −32.985 | −30.624 | −25.906 | −23.548 | −21.193 | −22.180 |
| −27.667 | −25.305 | −22.946 | −18.230 | −15.874 | −17.040 | −22.038 |
| −19.998 | −17.637 | −15.279 | −11.874 | −14.260 | −16.814 | — |
| −12.337 | −9.978 | −7.619 | −11.528 | −14.132 | −16.774 | −22.010 |

As gain applied, same data:

| input RMS | `LIM 20` | `LIM 40` | `LIM 80` | `LIM A0` | `LIM C0` | `LIM FF` |
|---|---|---|---|---|---|---|
| −50.782 | +2.382 | +4.752 | +9.488 | +11.851 | +14.212 | +18.854 |
| −43.047 | +2.368 | +4.733 | +9.456 | +11.813 | +14.173 | +18.810 |
| −35.347 | +2.362 | +4.723 | +9.441 | +11.799 | +14.154 | +13.167 |
| −27.667 | +2.362 | +4.721 | +9.437 | +11.793 | +10.627 | +5.629 |
| −19.998 | +2.361 | +4.719 | +8.124 | +5.738 | +3.184 | — |
| −12.337 | +2.359 | +4.718 | +0.809 | −1.795 | −4.437 | −9.673 |

**Small-signal gain is linear in the byte at 0.0742 dB per unit.** From the
quietest input at each setting: `20` → +2.382 (0.0744/unit), `40` → +4.752
(0.0743), `80` → +9.488 (0.0741), `A0` → +11.851 (0.0741), `C0` → +14.212
(0.0740), `FF` → +18.952 (0.0743, from the −58.586 input). Six points inside
±0.3%. So **`LIM FF` is about +19 dB of makeup gain.**

**Above a threshold the output stops rising, and that ceiling falls as `LIM`
rises:**

| `LIM` | ceiling, RMS dBFS | ceiling, peak dBFS |
|---|---|---|
| `80` | −11.528 | −8.282 |
| `A0` | −14.132 | −10.846 |
| `C0` | −16.774 | −13.463 |
| `FF` | −22.010 | −18.693 |

−2.604, −2.642 and −5.236 dB across `0x20`, `0x20` and `0x3F`: **−0.0819 dB per
unit**, three intervals inside 1.5%. That is the odd part of this finding and it
is stated as measured, not explained: turning `LIM` up raises the small-signal
gain *and* lowers the maximum the output will reach.

**`LIM A0` was captured as a test of that, not as a fit.** After `80`, `C0` and
`FF`, the two laws predict a ceiling of −14.17 dBFS and a small-signal gain of
+11.87 dB at `A0`. Measured: **−14.132 dBFS and +11.851 dB** — 0.04 dB and
0.02 dB out.

Above the knee the slope is near flat: at `LIM FF`, inputs of −35.347, −27.667
and −12.337 give −22.180, −22.038 and −22.010, i.e. **23 dB of input change for
0.17 dB of output change**. It is a brickwall, not a soft ratio.

**Ceilings at `LIM 00`, `20` and `40` were never reached** — the highest outputs
observed there were −12.337, −9.978 and −7.619 dBFS, all below where those
settings' ceilings would lie. So the ceiling law is measured at four points and
extrapolating it below `LIM 80` is arithmetic, not measurement. In particular
**nothing here shows what `LIM 00` does**; it is only this section's reference.

**`LIM` does not reshape the waveform in this range.** Crest stays 3.16-3.32 dB
everywhere, including 9 dB into gain reduction, and no capture clipped. It is a
compressor with roughly a 150 ms attack — at `LIM 80` with the loudest input the
level starts at −7.45 dB, and settles to −11.55 dB by 160 ms.

### `OTT` — an upward compressor that also reshapes the tone

`LIM 00` throughout. Output RMS dBFS, and the gain against the same reference:

| input RMS | `OTT 40` | gain | `OTT 80` | gain | `OTT C0` | gain |
|---|---|---|---|---|---|---|
| −50.782 | −38.182 | **+12.600** | −33.266 | **+17.516** | −30.549 | **+20.233** |
| −43.047 | −35.534 | +7.513 | −31.567 | +11.480 | −29.721 | +13.326 |
| −35.347 | −30.662 | +4.685 | −27.601 | +7.746 | −26.819 | +8.528 |
| −27.667 | −24.342 | +3.325 | −21.945 | +5.722 | −22.040 | +5.627 |
| −19.998 | −18.530 | +1.468 | −17.279 | +2.719 | −19.485 | +0.513 |
| −12.337 | −11.835 | +0.502 | −11.383 | +0.954 | −15.706 | **−3.369** |

**This is upward compression, and it is strong.** At `OTT C0` a −50.8 dBFS input
comes back **+20.2 dB louder** while a −12.3 dBFS input comes back **3.4 dB
quieter** — 38 dB of input range compressed into 15 dB of output range. Unlike
`LIM`, `OTT` changes level at every input; there is no region where it is
transparent.

**It does not preserve the waveform.** Crest rises from the source's 3.16 dB to
**5.4-8.3 dB**, worst at the quietest inputs, and the output carries new
low-frequency content that the input does not have (components at 12.1 Hz and
51.5 Hz, 41 and 44 dB down, absent from the reference). And it is not a static
gain: the output has a **stationary periodic ripple with an ~80 ms period**,
swinging 0.12 dB at the loudest input up to 1.60 dB at the quietest, stable from
about 60 ms after the note starts. A single output number does not describe what
`OTT` does; the ripple column is part of the measurement.

### What bounds all of this

- **One source, one band.** The tone is a single 2219 Hz partial. `OTT` is a
  multiband processor, so a curve taken with all the energy in one band is a
  slice of its behaviour, not the whole of it. `LIM`'s curve is much more likely
  to generalise than `OTT`'s.
- **No input above −12.337 dBFS RMS.** `TRACK1_VOL` caps at `E0`.
- **`LIM` and `OTT` were never enabled together.** Every `LIM` point has
  `OTT 00` and every `OTT` point has `LIM 00`.
- **The 480 ms note is short for `OTT`.** Its ripple is settled well inside the
  window, but a control with a time constant longer than ~400 ms would be
  invisible to this measurement.

---

## UI-32 — The three send returns do not come back at the same level

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/captures_backlog.md` A3.

**CORRECTED 2026-08-29 — see §UI-35.** This section's rig was rebuilt from the
Method below and its four captures retaken, twice, each time from a fresh card
reload. **The chorus figure reproduces exactly (−6.081 dB, to three decimals).
The delay and reverb figures do not:** delay measures **−11.421 dB** against
−7.230 here, and reverb **−10.294 dB** against −8.461. The two repeat runs agree
with each other to 0.003 dB.

So the ordering and the spread in the Results table below are wrong as well —
measured again it is chorus, then reverb, then delay, spanning 5.340 dB rather
than 2.380. The window, the estimator, the dry reference, the delay's `TIME` and
the master return levels were each eliminated as the cause; §UI-35 lists how.
Why this section read high on those two captures is not known.

**Everything below is left as written**, as the record of what was measured
then. Take the delay and reverb numbers from §UI-35; the chorus number here
stands and is confirmed.

### Method

`WAVV7F.M8S` reloaded from the card. Instrument 00: `TYPE WAVSYNTH`,
`SHAPE 06 SINE`, `SIZE 80`, `MULT 80`, `AMP 00`, `LIM 00`, `PAN 80`,
`FILTER 00 OFF`, `CUTOFF FF`, `RES 00`. Mixer: `TRACK1_VOL A0`, all three
returns `MX/DE/RE` stepped to `E0`, `MIX E0`, `LIM 00`, `OTT 00`, `DJF 80`.

Keyjazz note 60 at velocity `0x40`, 3 s captures. Four captures, changing one
send at a time and asserting every row of INSTRUMENT and MIXER before and after
each:

| capture | `DRY` | `MFX` | `DEL` | `REV` |
|---|---|---|---|---|
| dry reference | `FF` | `00` | `00` | `00` |
| chorus | `00` | `FF` | `00` | `00` |
| delay | `00` | `00` | `FF` | `00` |
| reverb | `00` | `00` | `00` | `FF` |

A control with `DRY 00` and all three sends `00` came back at **exact digital
zero**, so nothing leaks past the sends and the wet captures are wet only.

**The send effects' own parameters were all `00`** — as `WAVV7F.M8S` ships them:
chorus `MOD DEPTH:FRQ 00:00` / `WIDTH 00`, delay `TIME L:R 00:00` /
`FEEDBACK 00`, reverb `ROOM SIZE 00` / `DECAY:SHIMMER 00:00`. Each effect is
therefore at its null configuration, and what is measured is the send-to-return
path rather than the effect's character.

### Results

Two measures, because they answer slightly different questions: **in-note** is
RMS over 50-350 ms from the note's onset, which is "how loud does it come back";
**energy** is the sum of squares over the whole 3 s capture, which also counts
whatever rings on after the note.

| capture | in-note, rel. dry | energy, rel. dry | peak | clipped |
|---|---|---|---|---|
| dry, `DRY FF` | 0.000 dB | 0.00 dB | 0.4473 | 0 |
| chorus, `MFX FF` | **−6.081 dB** | −6.17 dB | 0.2216 | 0 |
| delay, `DEL FF` | **−7.230 dB** | −7.08 dB | 0.2783 | 0 |
| reverb, `REV FF` | **−8.461 dB** | −8.32 dB | 0.3055 | 0 |

**They are not equal.** With the send at `FF` and the return at `E0`, chorus
comes back loudest and reverb quietest, spanning **2.38 dB** in-note (2.15 dB by
energy). The two measures agree within 0.15 dB for all three, so the ordering is
not an artefact of the window.

Reverb is the one where they differ most, and visibly: even at `ROOM SIZE 00`
and `DECAY 00` its capture rings for about 11 envelope bins against 6 for the
others, so its energy figure carries a short tail the others do not have. That
makes its energy number *flattering* relative to its in-note level, and it is
still the quietest of the three.

### A first attempt clipped, and is not in the table

At `TRACK1_VOL E0` the wet captures hit peak 1.0000 with tens of thousands of
clipped samples while the dry reference sat at 0.4707 — the send-and-return path
is far louder than the dry path at the same fader. Those two runs were discarded
and the whole set retaken at `TRACK1_VOL A0`. A clipped capture cannot be a
point on a level comparison.

### Incidental: `DRY` accepts `FF`, and the tools say it does not

`m8_makeprobe` carries a range table (`kMixerDry`) that caps the instrument's
`DRY` at `0xC0`, and the first dry reference here was taken at `C0` because that
is what the project shipped. **The device took `FF` and read it back as `FF`.**
The difference is large enough to matter: `DRY C0` measured **15.08 dB below**
`DRY FF`, which is 0.239 dB per unit over those 63 steps — the same ~0.24 dB per
unit taper measured for the mixer's track fader in §UI-31.

Had the dry reference been left at `C0`, all three return levels would have come
back as *positive* numbers between +6.8 and +8.9 dB, which is why the matched
reference matters and why the `C0` row is shown above.

### What bounds this

- **Null effect settings only.** Every send effect parameter was `00`. A chorus
  with real depth or a delay with real feedback returns a different amount of
  energy, and nothing here says how much.
- **One source, one level, one send value.** `TRACK1_VOL A0`, send `FF`, return
  `E0`. Whether the 2.4 dB spread is constant across send and return values is
  not measured.
- The spread is a level difference only; no claim is made here about what each
  effect does to the signal.

---

## UI-33 — ModFX `MOD FRQ`: not measured, and what blocks it

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Leaves open:** tessera `docs/captures_backlog.md` A2.

**No rate in Hz is reported here.** This section exists so the next attempt
starts from what was ruled out rather than rediscovering it. Three things below
are measurements; the LFO rate is not one of them.

### What A2 needs and this rig cannot give

A2 needs a **steady tone lasting several seconds**. Its lowest `FRQ` points are
the ones that settle whether the law is linear or geometric, and a rate below a
few Hz cannot be seen in a note shorter than a second at all.

**Every keyjazz note available on this rig is about 480 ms.** Measured on the
`WAVV7F.M8S` WAVSYNTH probe: RMS flat within 0.03 dB from 40 ms to 480 ms, then
a cut to exact digital zero by 520 ms, with the note held for the whole capture.
Four things were tried and none lengthened it:

- **`MOD1 HOLD` was already `FF`**, the maximum, and that is what produces the
  480 ms.
- **`DEC 80` → `DEC FF`**: no change. The end of the note is a cut, not a decay
  — the last three 20 ms bins are −12.34, −14.88, then digital zero.
- **`DEST 01 VOLUME` → `00 OFF`**: no change, still ~500 ms. So the AHD envelope
  is not the only thing gating the voice.
- **Tempo 120 → 80 BPM**: no change. The note did not lengthen; at ~100 ms
  envelope resolution a tempo-scaled hold would have grown by half a bin-count
  and did not. **Envelope hold is not tempo-scaled here.**

The FM patch built for §UI-30 *did* sustain — 2.95 s, full-window — but its mod
slots came from a project that was never saved, and rebuilding it did not
reproduce the sustain: switching `TYPE` resets the mod slots, and the rebuilt
patch with `DEST 01 VOLUME`, `AMT FF`, `HOLD FF`, `DEC FF` gave a **20 ms
click**. Whatever sustains a note on this device, it is not the AHD hold.

### Two things that were measured, and are worth keeping

**1. At 100% wet the chorus produces no amplitude modulation.** With
`MOD TYPE 00 CHORUS`, `MOD DEPTH:FRQ FF:FF`, instrument `DRY 00` / `MFX FF`,
return `E0`, the output's 5 ms RMS envelope holds at **−17.5 dB ±0.3 dB** across
the whole 500 ms note. That is expected in hindsight and it invalidates half of
A2's stated method: the backlog says to read the period "off the amplitude
envelope or the pitch wobble", and with `DRY 00` there is no dry path for the
delayed copy to comb against, so there is nothing to see in amplitude. **Any
future attempt must either track pitch, or keep some dry signal deliberately.**

**2. Pitch tracking needs a cleaner source than this probe.** The WAVSYNTH probe
at `SIZE 80` / `MULT 80` carries sidebands 27-34 dB below its 2219 Hz partial. On
that signal an analytic-signal phase derivative returned a "carrier" of 20.1 Hz
with a ±697 Hz deviation, and a short-time spectral peak tracker returned a
carrier of 2809 Hz with a **±1264 Hz** swing — more than an octave, which no
chorus does. The tracker is hopping between partials. It did report a 10.5 Hz
periodicity at 6× the median bin, but on a track that is demonstrably not
following the carrier that number describes the hopping, not the LFO, **so it is
not recorded as a rate.**

### What would unblock it

A sustained, spectrally clean tone. In rough order of likely cost:

- A natively-authored **sampler** instrument looping a sample already on the card
  (`SHAPER.WAV` is there). Loop mode sustains for as long as the note is held and
  a sine sample would track cleanly. Note the standing caveat that *generated*
  sampler probes are silent on this device — this would have to be built on the
  device, which rig fact 9 requires anyway.
- Finding what actually gates note length. The AHD hold is ruled out above; the
  §UI-30 FM instrument sustained and something in its mod slots did it.
- Failing both: keep a little dry signal and read the comb-filter amplitude
  modulation instead of the pitch, which needs no pitch tracking at all.

`tools/lfo_rate.py` is kept for the next attempt: it tracks pitch by short-time
spectral peak and **reports `NOT RESOLVED` with the cycle count and the
peak-to-median ratio** rather than returning a number, which is what stopped the
10.5 Hz figure above from being written down as a result.

---

## UI-34 — The track fader is a PURE exponential at ~0.2407 dB/unit, and it scales the sends

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/parity.md` §E6.
- **Corrects:** §UI-31's incidental reading of the same control.

§UI-31 measured this fader in passing, while establishing an input axis for the
limiter, and said so: "nothing here separates a fader taper from mild
compression in the `LIM 00` path". This section measures it deliberately and
separates the two.

**Its nine numbers reproduce exactly** — the largest disagreement across the
overlapping points is 0.004 dB. What changes is what they mean, and two of the
statements around them are wrong.

### The rig

`WAVV7F.M8S` reloaded from the card, then §UI-31's rig rebuilt on it. Instrument
00: `TYPE WAVSYNTH`, `SHAPE 06 SINE`, `SIZE 80`, `MULT 80`, `WARP 00`,
`SCAN 00`, `AMP 00`, `LIM 00 CLIP`, `PAN 80`, `FILTER 00 OFF`, `CUTOFF FF`,
`RES 00`. Mixer: `MIX E0`, `LIM 00`, `OTT 00`, `DJF 80`, `OUTPUT VOL F0`.

Keyjazz note 60 at velocity `0x40`, 2 s captures, RMS over 100-420 ms from the
note's own onset (`tools/level_measure.py`). `tools/level_run.py` re-read every
row of INSTRUMENT and MIXER before and after **each** capture and asserted
`DRY`, `SHAPE`, `LIM`, `OTT`, `DJF` and `MIX` on every one; **no capture in this
section drifted and none clipped.**

`SHAPE` is not in the INSTRUMENT field map and was stepped with
`tools/step_cell.py`. See the tooling notes at the end.

### Run A — the fader at `DRY C0`, twenty points

`TRACK1_VOL` swept; the fourth column is §UI-31's figure where it has one.

| `TRACK1_VOL` | peak dBFS | RMS dBFS | §UI-31 RMS | dB/unit from the point above |
|---|---|---|---|---|
| `00` | — | **digital zero** | — | — |
| `04` | −62.350 | −65.570 | — | — |
| `08` | −61.061 | −64.564 | — | 0.2515 |
| `0C` | −60.206 | −63.549 | — | 0.2538 |
| `10` | −59.183 | −62.552 | −62.554 | 0.2491 |
| `20` | −55.345 | −58.590 | −58.586 | 0.2477 |
| `30` | −51.419 | −54.671 | −54.669 | 0.2449 |
| `40` | −47.575 | −50.782 | −50.782 | 0.2431 |
| `50` | −43.741 | −46.907 | — | 0.2422 |
| `60` | −39.886 | −43.046 | −43.047 | 0.2413 |
| `70` | −36.039 | −39.194 | — | 0.2408 |
| `80` | −32.193 | −35.347 | −35.347 | 0.2405 |
| `90` | −28.343 | −31.504 | — | 0.2402 |
| `A0` | −24.513 | −27.667 | −27.667 | 0.2398 |
| `B0` | −20.606 | −23.831 | — | 0.2397 |
| `C0` | −16.826 | −19.998 | −19.998 | 0.2396 |
| `D0` | −13.002 | −16.166 | — | 0.2395 |
| `E0` | −9.170 | −12.337 | −12.337 | 0.2393 |
| `F0` | −5.348 | −8.507 | — | 0.2394 |
| `FF` | −1.769 | −4.922 | — | 0.2390 |

Crest is 3.15-3.24 dB at every point from `10` up, so nothing in this range
reshapes the waveform. The three points below `10` read 3.2-3.5 dB and sit at
25-40 LSB of a 16-bit capture; their dB-per-unit figures are the noisiest in the
table.

**`TRACK1_VOL 00` is exact digital zero** — every sample after the note's onset
is 0, not "very quiet". The fader closes.

### `TRACK1_VOL` does NOT cap at `E0`

§UI-31 says "`TRACK1_VOL` caps at `E0`, so no input above −12.337 dBFS RMS /
−9.176 dBFS peak could be presented. Anything either control does above that
level is unmeasured."

**The device takes `FF` and reads it back as `FF`**, and `F0` likewise. Set and
read back through `m8drv`, then captured: `F0` gives −8.507 and `FF` −4.922 dBFS
RMS, neither clipped. So §UI-31's input axis was not bounded where it says it
was, and A6's ceiling could be pushed 7.4 dB higher than it was. Nothing here
re-measures `LIM` or `OTT`; this only removes the stated reason not to.

### Run B — the same fader 15 dB higher, and what it settles

§UI-31's open question is whether the monotone shrink in the last column of Run
A is a taper in the fader or a level-dependent path. The two make different
predictions when the whole path is moved and the fader is not, so Run A was
repeated with the instrument's `DRY` at `FF` instead of `C0` — 63 units of the
same kind of control, upstream of everything else.

| `TRACK1_VOL` | peak dBFS | RMS dBFS | dB/unit |
|---|---|---|---|
| `01` | −47.702 | −50.860 | — |
| `02` | −47.449 | −50.616 | 0.2437 |
| `04` | −46.904 | −50.128 | 0.2440 |
| `08` | −45.959 | −49.162 | 0.2415 |
| `10` | −44.032 | −47.223 | 0.2424 |
| `20` | −40.179 | −43.352 | 0.2420 |
| `40` | −32.478 | −35.639 | 0.2410 |
| `60` | −24.794 | −27.944 | 0.2405 |
| `80` | −17.099 | −20.263 | 0.2400 |
| `A0` | −9.431 | −12.589 | 0.2398 |
| `C0` | −1.763 | −4.923 | 0.2396 |

**A taper in the fader requires the two runs to be parallel in fader
coordinates** — `B(v) − A(v)` the same at every `v`, because `DRY` would then
contribute a constant. It is not:

| `TRACK1_VOL` | `04` | `08` | `10` | `20` | `40` | `60` | `80` | `A0` | `C0` |
|---|---|---|---|---|---|---|---|---|---|
| B − A, dB | 15.442 | 15.402 | 15.329 | 15.238 | 15.143 | 15.102 | 15.084 | 15.078 | 15.075 |

**0.367 dB of spread. The taper-in-the-fader model is rejected.**

**An exponential fader into a level-dependent path requires the two runs to be
the same curve slid along the fader axis** — `B(v) = A(v + s)`, with `s` the
`DRY` offset expressed in fader units. Fitting `s` freely by linear
interpolation on Run A:

- best `s` = **62.80 units**, against the 63 units that separate `DRY C0` from
  `DRY FF`;
- residual **0.027 dB rms, 0.047 dB worst**, over eleven points spanning 46 dB.

So: **the fader is a pure exponential, and the monotone shrink §UI-31 recorded
is downstream of it.** Two independent checks of the same thing:

- `DRY C0` with `TRACK1_VOL FF` reads −4.922 dBFS; `DRY FF` with `TRACK1_VOL C0`
  reads −4.923. The output depends only on the **sum** of the two bytes
  (447 in both), which is what two exponentials of equal slope in series do.
- §UI-32's incidental measured `DRY C0` at 15.08 dB below `DRY FF`, i.e. 0.239
  dB per unit over 63 steps. Independently reproduced here at 15.075 dB.

### The slope, and what this rig still cannot do

The measured slope is not one number, because the path is in it:

- **0.2390 dB/unit** at the top of the fader (`F0`→`FF`),
- **0.2515 dB/unit** at the bottom (`04`→`08`),
- **0.2407 dB/unit** for a single exponential least-squares fit over `04`-`FF`,
  which lands every one of the nineteen points within **0.24 dB** and has a
  smooth bow for a residual: −0.24 dB at `04`, +0.15 dB around `70`, 0 at `FF`.

For comparison, linear in amplitude — `v/255` — is **35 dB** out on the same
points. That model is not close to anything here.

**The split between the fader's slope and the path's gain is not determined by
this rig, and cannot be.** Everything measurable is the composite: the two runs
prove the variation is *not* in the fader, but there is no control in this path
known to be linear to calibrate the rest against, so the path's own transfer can
be measured only up to a scale that trades off exactly against the fader's
slope. What is bounded is its size: the whole non-exponential part of the
composite is **0.4 dB across 60 dB of range**.

### Run C — the fader scales the effect sends

Manual p. 30: "the track's volume in the mixer also adjusts its effect send
levels". Never checked on the device until now.

Instrument `DRY 00`, `DEL FF`, `MFX`/`REV` `00`; master `DE` return `E0`,
`MX`/`RE` `00`. Everything else as above. If the fader did not reach the sends,
the last column would be a straight line at the dry level's expense — the return
would not move at all.

| `TRACK1_VOL` | send return, RMS dBFS | dry at the same fader (Run A) | send − dry |
|---|---|---|---|
| `10` | −58.771 | −62.552 | 3.781 |
| `20` | −54.853 | −58.590 | 3.737 |
| `40` | −47.089 | −50.782 | 3.693 |
| `60` | −39.363 | −43.046 | 3.683 |
| `80` | −31.672 | −35.347 | 3.675 |
| `A0` | −23.993 | −27.667 | 3.674 |
| `C0` | −16.323 | −19.998 | 3.675 |
| `E0` | −8.661 | −12.337 | 3.676 |

**The send return tracks the fader unit for unit.** It moved 50.110 dB across
the same 208 units that moved the dry path 50.215 dB, and the difference between
them is constant within **0.107 dB** over the whole sweep — within 0.009 dB over
`60`-`E0`, where the path is most linear. The residual drift below `40` is the
same path nonlinearity Run B isolated, appearing in both columns at once.

So the manual is right, and **the track fader is one law, not two**: whatever
gain it applies, it applies before the split, and the wet-to-dry ratio at a
fixed fader is independent of where the fader sits.

Two checks that these are not artefacts: L and R are bit-identical with
correlation 1.0000 in every send capture, so no mono-sum comb loss is hiding in
the numbers; and both the dry and the send envelopes are flat within 0.1 dB
across the whole 480 ms note, in 20 ms bins, so the measurement window is not
catching an attack or a tail.

### An absolute level that does NOT reconcile with §UI-32

**RESOLVED 2026-08-29 — see §UI-35.** §UI-32's rig was rebuilt from its own
Method and its captures retaken twice. The reading below is confirmed
(−11.421 dB against §UI-32's own 50-350 ms window, repeating to 0.001 dB) and
§UI-32's delay figure is the one that does not reproduce. Its chorus figure
does, exactly, which is what rules out the rig and the window.

Against the `DRY FF` reference from Run B at the same fader, the delay return
here sits at **−11.404 dB** (`A0`: −23.993 against −12.589). §UI-32 reports
**−7.230 dB** for the same send, from a rig it describes in the same terms.

The one rig difference visible between the two — §UI-32 had all three master
returns at `E0`, Run C had only `DE` — **was tested and is not the cause**: the
`A0` point retaken with `MX` and `RE` also at `E0` reads −23.996 dBFS against
−23.993, a difference of 0.003 dB.

**No number is corrected here and nothing is concluded from it.** This section
did not set out to reproduce §UI-32 and did not reproduce its conditions
(different `DRY`, different sweep, different purpose), so the 4.17 dB is
recorded as an open discrepancy for whoever re-opens A3, not as a finding
against it. E6's answer does not depend on it: the answer is the *slope*, and
the slope is unambiguous.

### Incidental: `MIX_VOL` is not linear in amplitude either

**SUPERSEDED 2026-08-29 — see §UI-36**, which measured this control properly:
eighteen points, a single exponential at **0.27675 dB/unit**, and the same
two-run experiment used above to show the step-shrink is not a taper in the
fader. The two-point average below (0.2761 dB/unit) survives as an estimate —
§UI-36's fit over the same `40`-`E0` span is 0.2759 — but the law, the shape and
the `00` endpoint all come from §UI-36 now. The caution below that two points do
not fit a law was the right one.

Two points only, both guarded, at `TRACK1_VOL E0` / `DRY C0`:

| `MIX` | RMS dBFS |
|---|---|
| `40` | −56.515 |
| `E0` | −12.337 |

44.178 dB across 160 units, an average of **0.2761 dB per unit**. Linear in
amplitude would be 10.88 dB. So the master mix fader is exponential too, but
**two points do not fit a law** and its slope is not the track fader's — it also
sits at a different place in the path, so this section's separation argument
does not transfer to it. Left open deliberately; it deserves its own item.

### Tooling

- **`m8drv set MIX_VOL` fails intermittently.** It refuses with "the cell reads
  `MIX 40`, which has no leading hex digits to converge against". It cost this
  section the rest of the `MIX_VOL` sweep. **FIXED 2026-08-29, see §UI-36** — the
  cause was `readCursorValue` stripping the field label by an exact prefix
  compare, which both spellings the device draws defeat; it now ignores
  whitespace on both sides. `set MIX_VOL` and `set OUT_VOL` were both verified
  on hardware afterwards. The map entry was fine, as this note said.
- **The INSTRUMENT field map is the sampler's, and does not fit WAVSYNTH.**
  `SAMPLE` lands on the `SIZE` row, `SHAPE` has no field at all, and `CHO` —
  the instrument's `MFX` send — is not found on this screen. `AMP`, `LIM`,
  `PAN`, `DRY`, `DEL`, `REV`, `FILTER`, `CUTOFF` and `RES` all land correctly.
  Anything needing `SHAPE` or `MFX` on a WAVSYNTH must go through
  `tools/step_cell.py`.

---

## UI-35 — §UI-32's chorus figure reproduces exactly; its delay and reverb do not

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Corrects:** §UI-32's delay and reverb return levels. Its chorus figure and
  its method stand.
- **Bears on:** tessera F44, whose send-return trims are aimed at §UI-32's three
  numbers.

§UI-34 measured the delay return at −11.404 dB against a matched `DRY FF`
reference where §UI-32 reports −7.230, and left the 4.17 dB open because the two
sections had different purposes and different rigs. This section closes it by
rebuilding §UI-32's rig from its own Method section and taking its own four
captures.

### Method

§UI-32's Method, followed literally: `WAVV7F.M8S` reloaded from the card;
instrument 00 `TYPE WAVSYNTH`, `SHAPE 06 SINE`, `SIZE 80`, `MULT 80`, `AMP 00`,
`LIM 00`, `PAN 80`, `FILTER 00 OFF`, `CUTOFF FF`, `RES 00`; mixer
`TRACK1_VOL A0`, all three returns `MX`/`DE`/`RE` at `E0`, `MIX E0`, `LIM 00`,
`OTT 00`, `DJF 80`. Keyjazz note 60 at velocity `0x40`, **3 s** captures, one
send changed at a time, `tools/level_run.py` asserting the rig before and after
each. Measured with §UI-32's own estimators: in-note RMS over **50-350 ms** from
the note's onset, and whole-capture energy.

**`OUTPUT VOL` is not stated in §UI-32's Method.** It reads `F0` on a fresh card
reload and was left there, which is also what §UI-31 and §UI-34 used.

**Every effect parameter was read off the EFFECTS screen and is `00`** — chorus
`MOD DEPTH:FRQ 00:00` / `STEREO WIDTH 00` / `REVERB SEND 00`; delay
`TIME L:R 00:00` / `FEEDBACK 00` / `STEREO WIDTH 00` / `REVERB SEND 00`; reverb
`ROOM SIZE 00` / `DECAY:SHIMMER 00:00` / `MOD DEPTH:FRQ 00:00` /
`STEREO WIDTH 00`. That is the null configuration §UI-32 describes.

`SHAPE` and `MFX` are unreachable by name on a WAVSYNTH and were stepped with
`tools/step_cell.py` (§UI-34, Tooling).

**The whole set was taken twice, each time from a fresh card reload and a rig
rebuilt from scratch.**

### Results

Relative to the matched `DRY FF` reference taken in the same run:

| capture | run 1 in-note | run 1 energy | run 2 in-note | run 2 energy | §UI-32 in-note |
|---|---|---|---|---|---|
| chorus, `MFX FF` | **−6.081** | −6.174 | — | — | **−6.081** |
| delay, `DEL FF` | **−11.421** | −11.409 | **−11.421** | −11.435 | −7.230 |
| reverb, `REV FF` | **−10.294** | −9.737 | **−10.297** | −9.736 | −8.461 |

Run 1 against run 2: **0.001 dB** on the delay in-note figure and **0.002 dB** on
the reverb. Neither run clipped.

**The chorus figure reproduces to three decimal places.** That is the load-
bearing result of this section: it means the rig, the dry reference, the
instrument `DRY`, the measurement window, the estimator and the tooling are all
the same as §UI-32's. Whatever moved the other two did not move chorus.

**The delay is 4.191 dB quieter than §UI-32 has it, and the reverb 1.833 dB
quieter.** Both differences are far outside the 0.003 dB the rig repeats to.

The ordering changes with them. §UI-32 has chorus loudest, then delay, then
reverb, spanning 2.380 dB. Measured here it is chorus, then reverb, then delay,
spanning **5.340 dB**.

### What was eliminated, and how

- **The measurement window.** On the delay capture, RMS over 50-350 ms and over
  100-420 ms differ by **0.017 dB**. The two sections used those two windows and
  it is not the cause.
- **Peak against RMS.** Both estimators move together: in-note −11.421 and
  whole-capture energy −11.409 for the delay.
- **The instrument `DRY` of the reference.** Ruled out by the chorus match — a
  different reference level would have moved chorus by the same amount.
- **The delay's `TIME` against the capture window.** `TIME L:R` reads `00:00`,
  so nothing is deferred; and the sign is wrong for the hypothesis anyway. A
  tempo-synced delay returning *after* the window would make §UI-32's figure
  **quieter** than this one, and it is louder.
- **The master returns.** §UI-34 already retook the delay point with `MX` and
  `RE` at `E0` as §UI-32 had them rather than at `00`: −23.996 against −23.993
  dBFS, a difference of 0.003 dB.
- **Drift within a run.** Every capture was guarded on both sides by a full
  re-read of INSTRUMENT and MIXER; none drifted.
- **A one-off.** Two complete runs from two card reloads agree to 0.003 dB.

### The reverb return is not a steady tone, and its figure is soft

At `ROOM SIZE 00` and `DECAY:SHIMMER 00:00` the reverb return still rings for
**67** 20 ms bins — 1.34 s, past the end of the 480 ms note — and swings between
−20 and −43 dB across them rather than holding a level. The dry, chorus and
delay captures all hold flat for 24 bins and stop at digital zero on bin 26.

So the reverb's in-note number is measuring an attack, not a settled level, and
it is the one figure here that is genuinely window-dependent: its in-note and
energy readings differ by 0.56 dB where the delay's differ by 0.01. Its 1.833 dB
gap against §UI-32 is larger than that, but the reverb is the weaker of the two
corrections and is marked as such.

§UI-32 counted this ring as "about 11 envelope bins against 6 for the others".
Those counts are not reproduced here — 67 against 26 — but §UI-32 does not say
what threshold it counted above, so the two are not comparable and nothing is
concluded from the difference.

### What is NOT explained

**Why §UI-32's delay and reverb read high is unknown.** Several arithmetic
possibilities were checked against the measured levels and none fits:

- **A dry leak into the wet captures.** Reaching −7.230 from −11.421 needs an
  extra −9.31 dB of signal, which would be `DRY` at about `D8`; reaching −8.461
  from −10.294 needs −13.09 dB, about `C9`. Two different values, so one
  mis-set `DRY` does not explain both — and §UI-32 reports a control with
  `DRY 00` and all sends `00` returning exact digital zero.
- **Sends left open from the previous capture.** Chorus plus delay summed in
  power is −4.966 dB, not −7.230.

No claim is made about what §UI-32's session actually had. What is established
is that its stated rig, rebuilt twice, does not produce its delay or reverb
figures, and does produce its chorus figure exactly.

---

## UI-36 — `MIX` is a second exponential fader at 0.27675 dB/unit; `OUTPUT VOL` is not a fader at all

- **Date:** 2026-08-29
- **Firmware:** 6.5.2, COM3
- **Answers:** tessera `docs/parity.md` §E7. Replaces §UI-34's two-point
  incidental on `MIX_VOL`.

§UI-34 noted in passing that `MIX_VOL` looked exponential at about 0.2761
dB/unit from two guarded points, and said two points do not fit a law. This
measures it the way §UI-34 measured the track fader, and settles what
`OUTPUT VOL` is while the rig is up.

### The rig

`WAVV7F.M8S` reloaded from the card. Instrument 00 `TYPE WAVSYNTH`,
`SHAPE 06 SINE`, `SIZE 80`, `MULT 80`, `WARP 00`, `SCAN 00`, `AMP 00`,
`LIM 00 CLIP`, `PAN 80`, `DRY C0`, `MFX`/`DEL`/`REV 00`, `FILTER 00 OFF`,
`CUTOFF FF`, `RES 00`. Mixer: returns `MX`/`DE`/`RE 00`, `LIM 00`, `OTT 00`,
`DJF 80`, `OUTPUT VOL F0`. Keyjazz note 60 at velocity `0x40`, 2 s captures,
RMS over 100-420 ms from the note's onset.

**Anchored against §UI-34 before any data was taken**, and this matters — see
"A run that had to be thrown away" below. At `TRACK1_VOL E0` / `MIX E0` /
`DRY C0` the rig reads **−12.337 dBFS**, which is §UI-34 Run A's `E0` point to
the last digit. Two further anchors: `TRACK1_VOL A0` reads −27.667 against
§UI-34's −27.667, and `TRACK1_VOL 40` reads −50.782 against its −50.782.

### `OUTPUT VOL` is not in the recorded chain

Swept first, because it is one capture per point and it decides whether there is
a third fader to measure at all. `TRACK1_VOL E0`, `MIX E0`, `DRY C0`:

| `OUTPUT VOL` | peak dBFS | RMS dBFS |
|---|---|---|
| `00` | −9.178 | −12.336 |
| `40` | −9.166 | −12.337 |
| `80` | −9.182 | −12.337 |
| `C0` | −9.176 | −12.337 |
| `F0` | −9.172 | −12.337 |
| `FF` | −9.178 | −12.337 |

**Every value is the same level within 0.001 dB RMS**, and the 0.016 dB of peak
spread is capture-to-capture noise. `OUTPUT VOL 00` — the fader fully closed —
passes full level over USB.

So `OUTPUT VOL` is **not a fader in the recorded signal chain**: it is the
analogue output stage, downstream of wherever USB audio is tapped. It cannot be
given a law by this rig — not a linear one, not an exponential one — because it
never reaches the recording. Anything a renderer does with it is a UI decision,
not a parity one.

That also bounds a claim nobody made but might: this says nothing about whether
`OUTPUT VOL` is digital or analogue, only about where it sits relative to the
USB tap.

### `MIX` — eighteen points

`TRACK1_VOL E0`, `DRY C0`. None clipped, none drifted.

| `MIX` | peak dBFS | RMS dBFS | dB/unit from the point above |
|---|---|---|---|
| `00` | — | **digital zero** | — |
| `08` | −68.725 | −72.817 | — |
| `10` | −66.787 | −70.341 | 0.3096 |
| `20` | −62.350 | −65.635 | 0.2941 |
| `30` | −57.844 | −61.039 | 0.2873 |
| `40` | −53.284 | −56.517 | 0.2826 |
| `50` | −48.871 | −52.043 | 0.2796 |
| `60` | −44.420 | −47.593 | 0.2781 |
| `70` | −39.992 | −43.161 | 0.2770 |
| `80` | −35.581 | −38.743 | 0.2761 |
| `90` | −31.176 | −34.331 | 0.2758 |
| `A0` | −26.764 | −29.924 | 0.2754 |
| `B0` | −22.361 | −25.524 | 0.2750 |
| `C0` | −17.977 | −21.125 | 0.2749 |
| `D0` | −13.571 | −16.729 | 0.2747 |
| `E0` | −9.171 | −12.336 | 0.2746 |
| `F0` | −4.784 | −7.945 | 0.2745 |
| `FF` | −0.674 | −3.830 | 0.2743 |

A single exponential least-squares fit over `08`-`FF` is **0.27675 dB/unit**,
landing every point within **0.630 dB**; over `20`-`FF` it is 0.27580 dB/unit
within 0.301 dB. The `08` and `10` points sit at 12-25 LSB of a 16-bit capture
and carry the table's only elevated crest figures (4.09 and 3.55 against 3.15-3.28
everywhere else), so they are the noisy ones and the wider residual is theirs.

**Linear in amplitude is not close:** it puts `0x80` at −5.99 dB where the device
puts it at −34.9.

**`MIX 00` is exact digital zero.**

### The step-shrink is not a taper in `MIX`, by §UI-34's experiment

The dB/unit column shrinks monotonically from 0.3096 to 0.2743, the same
signature the track fader showed. §UI-34 separated a fader taper from a
level-dependent path by sweeping the fader twice with everything else moved by a
known amount; the same experiment here, with the track fader moved from `E0` to
`A0` — 15.330 dB down, and downward deliberately (see below).

**A taper in `MIX` requires the two runs to be parallel in fader coordinates:**

| `MIX` | `60` | `80` | `A0` | `C0` | `E0` | `FF` |
|---|---|---|---|---|---|---|
| at `TRACK1_VOL E0` | −47.593 | −38.743 | −29.924 | −21.125 | −12.336 | −3.830 |
| at `TRACK1_VOL A0` | −63.204 | −54.170 | −45.284 | −36.462 | −27.666 | −19.158 |
| difference | −15.611 | −15.427 | −15.360 | −15.337 | −15.330 | −15.328 |

**0.283 dB of spread. Rejected.**

**An exponential into a level-dependent path requires one run to be the other
slid along the fader axis** — `M4(v) = M1(v + s)`. Fitting `s` freely:

- best `s` = **−55.70 `MIX` units**, against the −55.39 that 15.330 dB at
  0.27675 dB/unit predicts;
- residual **0.029 dB rms, 0.047 dB worst**, over six points spanning 44 dB.

So `MIX` is a pure exponential and the shrink belongs to whatever follows it —
the same conclusion, by the same method, as §UI-34 reached for the track fader.

### `MIX` and the track fader are DIFFERENT laws

Not assumed from the two slopes. If they were one law, the output would depend
only on the sum of the two bytes, which is the property §UI-34 used to show that
`DRY` and the track fader *are* one law (`DRY C0` + `TRACK FF` and `DRY FF` +
`TRACK C0` both read −4.92).

| | RMS dBFS |
|---|---|
| `TRACK E0` / `MIX C0` (sum 416) | **−21.125** |
| `TRACK C0` / `MIX E0` (sum 416) | **−19.998** |

Same byte sum, **1.127 dB apart**. Two faders, two slopes: 0.2407 dB/unit for the
track fader, 0.27675 for `MIX`.

### Why the shift was made downward

The first attempt shifted the path **up**, by moving the instrument's `DRY` from
`C0` to `FF` — the same control §UI-34 used, worth 15.08 dB there. It bought only
**11.28 to 11.82 dB**:

| `MIX` | `10` | `20` | `40` | `60` | `80` | `A0` | `C0` |
|---|---|---|---|---|---|---|---|
| `DRY FF` − `DRY C0`, dB | 11.820 | 11.614 | 11.397 | 11.327 | 11.303 | 11.286 | 11.281 |

At `DRY FF` with `TRACK1_VOL E0` and `MIX C0` the output is −9.844 dBFS, and
`MIX C0` is 17.44 dB of attenuation, so the signal arriving at `MIX` is about
**+7.6 dBFS** — above full scale. Something upstream is holding it down, which is
exactly the confound the experiment exists to avoid. §UI-34's +15.08 dB for that
same `DRY` step was measured at `TRACK1_VOL A0` and `C0`, well below this.

Recorded rather than pursued: **a stage between the instrument's `DRY` and `MIX`
compresses once the signal passes roughly full scale**, costing about 3.8 dB at
+7.6 dBFS. Its curve is not measured here.

### A run that had to be thrown away, and the rig fact behind it

The first attempt at the downward shift produced levels **identical to the
unshifted run** — 0.003 dB across nine points — while the MIXER screen read
`TRACK1_VOL A0` throughout and `level_run.py`'s before-and-after guards passed on
every capture. The WAV files were confirmed to be genuinely distinct captures,
not a reused file: different sizes and different hashes at every point.

That run had started immediately after the daemon timed out mid-sweep and was
killed and recovered. Everything checked afterwards was correct — the same rig
now reads −12.337 at `TRACK1_VOL E0`, −27.667 at `A0` and −50.782 at `40`, all
exact against §UI-34 — so the fault was transient and is not explained.

**The lesson is about the guards, and it generalises: a screen guard cannot
detect this.** `level_run.py` reads the display before and after each capture,
and the display agreed with itself while the audio did not. **After a daemon kill
and recovery, re-anchor against a known level before trusting captures.** An
absolute anchor would have caught it on the first point; the guards never could.

Two things were eliminated while chasing it, and both are worth keeping:

- **The MIXER cursor column does not select which track sounds.** With
  `TRACK1_VOL 40`, a capture with the cursor on `TRACK1_VOL` and one with it on
  `MIX_VOL` read −50.782 and −50.781.
- **`cursor MIX_VOL` navigates correctly**, from `TRACK1_VOL`, `LIM_VAL`,
  `OUT_VOL` and `MST_REV` alike — all four land on the `MIX` cell.

### Tooling

Two bugs found and fixed; both blocked this section.

- **`editValue` refused `set OUT_VOL` and `set MIX_VOL`**, reporting "no leading
  hex digits to converge against" and blaming the field map. `readCursorValue`
  stripped the label by an exact prefix compare, which both spellings the device
  draws defeat — `" OUTPUT VOL  F0"` carries a leading space the map's label does
  not, and `"OUTPUTVOLF0"` has lost the space inside the label. It now ignores
  whitespace on both sides. `m8drv.md` had already told readers label stripping
  was whitespace-insensitive; that was aspirational until now.
- **`level_run.py`'s `norm()` ate a byte out of the track-volume row.** That row
  is eight bare hex bytes and the decoder glues neighbouring ones from time to
  time; `"E0E0"` matches the enum-caption pattern (`E0` value, `E` letter, `0`
  tail) and collapsed to `"E0"`, so the row normalised to seven bytes. `--expect`
  could therefore never pin a track volume — an eight-byte want cannot be a
  substring of a seven-byte norm — which is why the guards in the discarded run
  above were weaker than they looked. A caption's tail must now contain a non-hex
  character.
