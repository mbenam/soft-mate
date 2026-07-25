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

