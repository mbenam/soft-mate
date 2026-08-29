# CLI Tools Reference

Every standalone command-line tool built alongside the m8-sdl3 clone, documented for future
agents picking up this project. Each tool has its own detailed doc — this page is the index and
the map of how they fit together.

All of these are separate executables built from `src/tools/main_*.cpp`, each with a narrow,
single-purpose role (per the project's audio-API-separation invariant — see `status.md` — most
link only `m8_engine`, `m8_files_cpp`, or `m8_device`, never more than one, and never SDL unless
it's the main app). None of them require the full `m8_clone` app to build or run.

## Index

| Tool | Doc | One-line summary |
|---|---|---|
| [`m8_render`](m8_render.md) | offline rendering | Drives the real engine with no audio device; writes WAV + a sample-accurate event-log CSV. The ground-truth oracle for "what should this sound like." |
| [`m8_analyze`](m8_analyze.md) | audio health gate | Reads a WAV, computes objective metrics (peak/RMS/crest/DC/clipping), exits non-zero on hard-check failure. The CI-checkable pass/fail gate. |
| [`m8_spectrum`](m8_spectrum.md) | A/B timbre comparison | Compares two WAVs' spectra (fundamental, harmonics, centroid, log-spectral distance) — typically a hardware capture vs. a render. Reporting only, never fails. |
| [`m8_makeprobe`](m8_makeprobe.md) | fixture generator | Writes minimal, deterministic `.m8s` files: one instrument, one note, isolatable for hardware/offline comparison. |
| [`m8_makesong`](m8_makesong.md) | song regenerator | Regenerates the committed "opening" (Night Drive) demo song + its drum WAVs from the in-code demo. |
| [`m8_composesong`](m8_composesong.md) | song authoring | Composes the "SUNRISE" startup song from scratch in C++ — the app's actual boot song. |
| [`m8_capture`](m8_capture.md) | hardware audio capture | Drives a real M8 over serial (play/keyjazz) while recording its USB audio output via miniaudio. |
| [`m8_nav`](m8_nav.md) | hardware device driver | Decodes the M8's serial display protocol; navigates screens, edits fields, loads files, runs `.m8script` scripts — closed-loop, verified against the real framebuffer. The most complex tool here, under active reliability work. |
| [`m8_diffcheck`](m8_diffcheck.md) | device-vs-golden diff | Runs a `.m8script` on the real device and diffs the resulting screen against a stored text reference. |
| [`m8_watchcapture`](m8_watchcapture.md) | guarded measurement | Records USB audio while decoding the screen every 10 ms, and aborts on transport inversion or field drift instead of publishing a number that has to be retracted. The only tool that sees and hears at once. |
| [`m8_sweep`](m8_sweep.md) | unattended measurement | Set a field, play a note, capture, measure, next value. Verifies each value by read-back, restores the field, exits non-zero if it could not. Built to be scheduled, not watched. |
| [`m8_crawl`](m8_crawl.md) | field-map ground truth | Walks a screen's cursor chain until it closes and records every real stop plus the navigation graph, then diffs it against the field maps. `--check` runs offline. |
| [`m8_editwatch`](m8_editwatch.md) | #34 reproduction rig | Replays an edit walk one press at a time while sampling the cursor between presses, to find out whether it moves. Mutates the device; restores and verifies. |
| [`m8_livecheck`](m8_livecheck.md) | live-read diagnostic | Proves on hardware that a settle-gated read cannot complete while the transport runs, and that `LiveReader` can. Presses one key (`PLAY`), navigates nothing, prints one JSON object. |
| [`m8drv`](m8drv.md) | unattended driver (Python) | Holds **one** `m8_nav --serve` connection open and supervises it: per-command timeouts, kill/restart/re-home recovery, and up-front refusal of fields that provably can't be driven. Not a second driver — the supervision layer that lets an agent drive the device with no human hand on it. |
| [`extract_manual_wavetables`](extract_manual_wavetables.md) | data extraction (Python) | Digitises the 61 WavSynth wave tables out of the manual's Wave Table Index — the plots there are vector polylines, so the index is a machine-readable source for the data itself. Emits the engine's generated wave table bank. |
| [`compare_capture`](compare_capture.md) | wave table verification (Python) | Folds an `m8_capture` recording down to one averaged cycle and reports which of a wave table's 64 frames it matches. How the extraction above was verified against real hardware. |
| [`fm_probe`](fm_probe.md) | FM screen driver (Python) | Maps the FMSYNTH instrument screen by pressing a value key and reading back which number moved — its cursor coordinates are identical at every stop along a row — then builds the patch on the device, captures it guarded, and measures the fundamental with three estimators. §UI-30. |
| [`level`](level.md) | master-chain level rig (Python) | Sweeps a named field, guards INSTRUMENT and MIXER around every capture, and measures steady-state peak/RMS/crest/ripple on a note-relative window. How LIM and OTT were measured. §UI-31. |
| [`step_cell`](level.md) | unnamed-cell stepper (Python) | Steps a hex byte the field map cannot name, reached by key path from a field it can, verified from the decoded row. The mixer's `OTT` has no map entry at all. |
| [`rt60`](rt60.md) | reverb decay rig (Python) | Sets reverb `DECAY` by stepping and re-reading (`m8drv set` cannot address a paired `A:B` row), captures a tail with `m8_capture --note-ms`, and reports three decay estimates so they can disagree in public. Guards three screens before and after, not one. §UI-29. |
| [`hw_measure`](hw_measure.md) | verified capture (Python) | Sets instrument fields, verifies each by read-back, captures, then verifies **again** afterwards — and refuses to keep a WAV whose state drifted. Exists because a `set` on one field silently changed another and produced a data point that looked real ([#34](../../specs/M8_DRIVER_BUGS.md)). A measurement is worth only the state it was taken in. |

**Not covered by a doc here** (out of this index's scope, documented elsewhere):
- `m8_clone` — the actual application (SDL3 UI + engine + headless `.m8script` runner). See
  `ARCHITECTURE.md` and `status.md`.
- `m8_tests` — the Catch2 test binary (`tests/test_*.cpp`, one executable, tag-filtered via
  `m8_tests.exe "[tagname]"`). Self-documenting via Catch2's own `--list-tests`/`--list-tags`.

## How they fit together

**Offline render→analyze loop** (no hardware needed):
```
m8_makeprobe / m8_makesong / m8_composesong  →  .m8s file
                    ↓
              m8_render  →  .wav + _events.csv
                    ↓
              m8_analyze  →  PASS/FAIL + metrics
```

**Hardware parity loop** (needs a real M8 over serial + USB audio):
```
m8_makeprobe --type sampler --sample-path ...  →  probe.m8s
        (copy probe.m8s + its sample WAV to the SD card — see m8_nav's --load-file
         Gotchas: there's no automated file-transfer, only automated *loading* of an
         already-present file)
                    ↓
        m8_nav --load-file <name>  →  loads it on the device
                    ↓
        m8_capture --keyjazz <note> --out ref.wav  →  real hardware recording
                    ↓
        m8_render --load probe.m8s --note ... --out mine  →  offline "should sound like this"
                    ↓
        m8_spectrum --ref ref.wav --test mine.wav  →  where do they diverge
        m8_analyze ref.wav  →  is the *capture itself* healthy (not silent, not clipped)
```

**Wave table digitisation loop** (the manual is the data source; hardware is the check):
```
manual/wavesynth.pdf
        ↓
extract_manual_wavetables  →  src/engine/data/WavetableBank.cpp  (compiled into m8_engine)
                           →  wavetables.bin  (--bin, for the comparator only)
        ↓
m8drv batch  →  set the device to one wave table, everything else neutral
        ↓
m8_capture --keyjazz 36  →  cap.wav
        ↓
compare_capture cap.wav <TABLE> --scan <byte>  →  which frame is this, really
```

**Device-driver testing** (no audio involved, screen-state only):
```
m8_nav --script foo.m8script          →  run a script against the real device
m8_diffcheck --script foo.m8script --golden ref.txt  →  same, diffed against a stored screen
```

## Cross-cutting things every tool here shares

- **Exit code 0 = success is not universal.** Some tools (`m8_spectrum`) never fail regardless of
  result quality — they're reporting tools, not gates. Read each tool's own Exit Codes section;
  don't assume.
- **`--out`/`--out-dir` conventions are inconsistent across tools** — some default to a fixed
  name (`render`, `probe.m8s`), some require the flag, some silently do nothing if omitted
  (`m8_capture`, still exits 0). Check each tool's own flag table.
- **None of these tools validate their own numeric CLI args beyond `strtol`/`atof`** — a
  malformed number silently becomes `0`, not a parse error. This is consistent across all of
  them; don't expect strict validation anywhere in this set.
- **There are two independent serial port implementations, not one per tool.** `m8_capture` has
  its own Win32 `SerialPort`; everything else — `m8_nav`, `m8_diffcheck`, `m8_livecheck`,
  `m8_watchcapture` — shares `m8_device`'s. They speak the same underlying M8 protocol from two
  separate codebases. Note the consequence for `m8_watchcapture`: it links `m8_device` for the
  display and `m8_audiocap` for the audio, so it is the one tool that reads the screen and
  records the sound through the same process — see its doc for why that had to be one process.
- **Known real, currently-open reliability issues live in `M8_DEVICE_CONTROL_SPEC.md`**, not
  here — `m8_nav.md` and `m8_diffcheck.md` summarize the hardware-testing gotchas found as of
  2026-07-18, but that spec document is the living, authoritative source for current status
  (Tier 4.5 reliability hardening was in progress at the time these docs were written).

## Documentation verification script

The script [`tools/check_doc_flags.py`](../../tools/check_doc_flags.py) automatically extracts `--flag` string literals from `src/tools/main_*.cpp` files and verifies that every CLI flag is documented in its corresponding `docs/tools/m8_*.md` document.

Run standalone:
```powershell
python tools/check_doc_flags.py
```

Run via test suite:
```powershell
.\build\Release\m8_tests.exe "[doc]" --reporter compact
```
