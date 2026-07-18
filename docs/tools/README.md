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
- **The two hardware-facing tools (`m8_nav`, `m8_capture`) each have their own independent serial
  port implementation** — they are not sharing a `SerialPort` class, despite speaking the same
  underlying M8 protocol. `m8_diffcheck` shares `m8_nav`'s implementation (both link `m8_device`).
- **Known real, currently-open reliability issues live in `M8_DEVICE_CONTROL_SPEC.md`**, not
  here — `m8_nav.md` and `m8_diffcheck.md` summarize the hardware-testing gotchas found as of
  2026-07-18, but that spec document is the living, authoritative source for current status
  (Tier 4.5 reliability hardening was in progress at the time these docs were written).
