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

## P5 — Resolve the AHD envelope contradiction

### Envelope Hold Duration Analysis
- **Tempo Baseline:** 120 BPM with default 6 ticks per step => 1 tick = 41.67 ms (1000 samples at 48 kHz).
- **Hold Calculation:** `hold = 0xFF` (255 ticks) yields ~10.6 seconds of sustained peak volume.
- **Resolution:** Updated `m8_makeprobe` to set `ahd.hold = 0xFF` (max hold), guaranteeing sustained full-scale amplitude over the entire capture/analysis window (1.5 - 5.0 s) with no early decay blip.

