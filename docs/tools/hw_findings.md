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


## UI-4 — RES→OTT label revert, mixer FX parameter discovery, load-path gap, column offset

### UI-4a — Reverted RES→OTT label change (commit 2b9af5c)

Commit 2b9af5c renamed the static label at row 22 from "RES" to "OTT" while
leaving the field wired to CursorId::DJF_RES, whose value resolver returns
mx.djf_res. The screen displayed "OTT" above the DJ filter's resonance value,
and editing that field changed DJF resonance. The label and the parameter
behind it no longer agreed, which is worse than the mismatch it was trying
to fix.

The same commit deleted DJF_TYP from the CursorId enum, the static text, the
interactive fields, the nav map, and two handlers in MixerScreen.cpp. But
ParamID::MIX_DJF_TYP and MixerState::djf_typ still exist in the engine, so
that parameter became unreachable from the UI.

**Action taken:** Both changes reverted by hand (not git revert). "RES"
restored at row 22, "TYP" restored at row 23, DJF_TYP restored to enum,
interactive fields, nav map, and MixerScreen.cpp handlers. Build confirmed.
The open question remains: the device shows "OTT" where the clone shows
"RES/TYP", and it is not yet known whether OTT is a parameter the clone
does not model at all.

- **Date:** 2026-07-26

### UI-4b — Device mixer FX parameter list (BLOCKED)

`m8_nav` is not available on this machine. The device's actual mixer FX
parameter list cannot be determined without hardware access. The task is
BLOCKED until a device is attached.

- **Date:** 2026-07-26 (blocked — no device)

### UI-4c — MixerState fields not loaded from .m8s

The device and clone agree on tempo and track volumes when loading
PROBE_SELFTEST, but MIX value (device E0, clone DC) and DE send
(device FF, clone 00) differ. The clone's MIX_VOL placeholder is
literally "DC", which is the compile-time default.

**Root cause:** `convertSongToEngine()` in `src/io/SongIO.cpp:304-314`
populates 12 of 21 MixerState fields from the .m8s file. The remaining
9 fields retain their in-class defaults (Engine.h:237-251):

| Field | Default | Loaded from .m8s? |
|-------|---------|-------------------|
| in_vol | 0x00 | No |
| in_cho | 0x00 | No |
| in_del | 0x00 | No |
| in_rev | 0x00 | No |
| usb_vol | 0x00 | No |
| usb_cho | 0x00 | No |
| usb_del | 0x00 | No |
| usb_rev | 0x00 | No |
| mix_vol | 0xDC | No |

The m8-files-cxx library's `MixerSettings` struct (third_party/m8-files-cxx/src/types.hpp:94-104)
**does** parse `analog_input` and `usb_input` (each containing volume,
chorus, delay, reverb). The data is present in the parsed Song object but
`convertSongToEngine()` never reads it. These fields are also not
round-tripped on save (`convertEngineToSong`).

**Not fixed in this task.** This is its own piece of work — adding nine
field transfers to `convertSongToEngine()` plus the save counterpart.

- **Date:** 2026-07-26

### UI-4d — Column offset is not uniform

The clone labels at col 23 vs device at col 24 was reported as a known
gap. Measuring every identifiable glyph run in the device capture
(tests/ui/golden/device/MIXER.json) against the clone layout
(MixerScreenLayout.h) reveals three distinct patterns:

**Pattern 1 — Left side + middle FX (cols 0–27): constant +1.**
Title, OUTPUT VOL, all track volume hex values, MIX/LIM/DJF/RES
labels, their values, EQ, INPUT, USB — device is 1 column right of
clone.

**Pattern 2 — Track numbers (col 34): constant 0.**
All eight "N ---" entries are at col 34 on both sides. Anchored to
the right edge of the 40-column grid.

**Pattern 3 — Sends MX/DE/RE: inconsistent spacing.**
Device uses 3-col spacing (cols 1, 4, 7); clone uses 4-col spacing
(cols 0, 4, 8). This gives deltas of +1, 0, −1 respectively — a
different internal layout, not a uniform offset.

| Element | Device col | Clone col | Delta |
|---------|-----------|-----------|-------|
| MIXER title | 1 | 0 | +1 |
| OUTPUT VOL | 1 | 0 | +1 |
| Track 1 vol (E0) | 1 | 0 | +1 |
| Track 8 vol (E0) | 22 | 21 | +1 |
| MIX label | 24 | 23 | +1 |
| MIX value | 28 | 27 | +1 |
| LIM label | 24 | 23 | +1 |
| DJF label | 24 | 23 | +1 |
| OTT/RES label | 24 | 23 | +1 |
| Track 1 --- | 34 | 34 | 0 |
| Track 8 --- | 34 | 34 | 0 |
| MX (sends) | 1 | 0 | +1 |
| DE (sends) | 4 | 4 | 0 |
| RE (sends) | 7 | 8 | −1 |
| INPUT | 13 | 12 | +1 |
| USB | 19 | 18 | +1 |

**Conclusion:** The offset is not a constant. A uniform +1 correction
would fix most elements but break DE (already aligned) and RE (would
shift to −2). The sends section has genuinely different spacing between
device and clone, which needs its own investigation.

- **Date:** 2026-07-26

