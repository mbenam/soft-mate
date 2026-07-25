# M8 Hardware Test & Probe Authoring Findings

Documenting live hardware measurements, USB capture level baselines, device golden files, and diff findings.

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
- **Root Cause:** `m8_makeprobe` passed `volume = 0xE0` (224) to `SynthParams`. For MacroSynth `0xE0` is nominal default volume, but on hardware Sampler instruments `0x00` represents 0 dB / full volume (`0xE0` caused severe attenuation).
- **Fix:** Updated `main_makeprobe.cpp` to default `sp.volume = 0x00` (0 dB) for Sampler instruments.

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
