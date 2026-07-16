# M8-SDL3 — Hardware Capture Spec

Two C++ tools that produce clean single-note recordings off a real M8 headless, so
`m8_spectrum` (see `M8_AUDIO_ANALYSIS_SPEC.md`) can compare this engine's synths against
hardware. This is how you build Macrosyn / FM / etc. to parity.

- **`m8_makeprobe`** — generate probe `.m8s` files: one instrument, known parameters, one note.
- **`m8_capture`** — drive the headless over serial, record its USB audio, write a trimmed WAV.

Everything is C++. No Python. The point is that the agent runs the whole loop unaided:
`m8_makeprobe` → copy to SD → `m8_capture` → `m8_render --load` → `m8_spectrum` → adjust → repeat.

**Build these when you start Macrosyn, not before.** The analysis tools that pay off now are in
the other spec.

---

## Key idea — compare identical input

Do **not** enter parameters on the device by hand or by keystroke. Generate a `.m8s` with the
exact parameters you want, load *that same file* on hardware and through `m8_render`, and
compare. Same file, both sides — no parameter-entry drift, no "did I set timbre to 0x40 on
both." The `.m8s` is the single source of truth for the experiment.

This is possible because `m8-files-cxx` already writes `.m8s`, and `m8_render --load` already
plays one.

---

## Tool 1 — `m8_makeprobe`

Generates a minimal `.m8s`: one instrument at a known setting, one phrase with one note at a
known pitch, one chain, one song row. Links `m8_files_cpp`.

```
m8_makeprobe --type macrosynth --shape 0x00 --timbre 0x40 --color 0x80 \
             --note C-4 --out probe_macro_00_40_80.m8s

m8_makeprobe --sweep shape --type macrosynth --note C-4 --out-dir probes/
    # writes probe_shape_00.m8s, probe_shape_10.m8s, ... one per value across the range
```

### Requirements

- Build a `m8::Song` from scratch (version V4 or V4.1 — pick the one that round-trips; pre-4.0
  `write()` throws, per the persistence spec).
- One instrument at index 0 with the type and parameters given. Every other instrument empty.
- One phrase (0x00): row 0 = the note, instrument 0, full velocity. All other rows empty.
- One chain (0x00): slot 0 = phrase 0x00.
- Song row 0, track 0 = chain 0x00. Everything else empty.
- Set a known tempo (e.g. 120) and a groove so the note lands predictably.
- **Amp envelope**: mod slot 0 = AHD → VOLUME with a long-ish hold and decay, so the note
  sustains long enough to analyse (≥ 1.5 s). A percussive envelope gives you nothing to FFT.
- Write with `Song::write()`; fail loudly if it throws.

### `--sweep`

Given `--sweep <param>`, write one file per value of that parameter across its range (step of
16 is fine — 0x00, 0x10, ... 0xF0), naming each file by the value. This is how you build a
reference library covering a whole parameter axis in one command.

Supported sweep axes at minimum: `shape`, `timbre`, `color` (MacroSynth). Add others as the
synths need them.

### Acceptance

- A generated probe loads in `m8_clone` and plays a single sustained note.
- The same probe loads via `m8_render --load` and renders without error.
- Round-trips: `m8_makeprobe ... --out p.m8s` then load p.m8s in `m8-files-cxx` and confirm the
  instrument parameters read back exactly what was set.

---

## Tool 2 — `m8_capture`

Drives the headless over serial and records its audio. Links miniaudio (header-only).

> **`m8_capture` links miniaudio, not SDL.** It is a standalone diagnostic tool, not part of the
> product. Keeping it on its own audio backend means the engine links no audio API, `m8_clone`
> links SDL, `m8_capture` links miniaudio, and nothing else links either. The tool could be
> lifted out of the repo and still build. miniaudio is a single public-domain header
> (`third_party/miniaudio/miniaudio.h`); its capture path is more mature and its device
> enumeration simpler than SDL's.

```
m8_capture --port COM4 --audio "M8" --seconds 2.5 --out ref_macro_00.wav
m8_capture --port COM4 --audio "M8" --batch probes_loaded.txt --out-dir refs/
```

### What it does

Assumes the target `.m8s` is **already on the SD card and loaded on the device** (loading a file
off the SD card by remote keystroke is fiddly and the M8's browser layout is not worth
automating). The operator loads the probe on the device; `m8_capture` handles play + record +
trim.

1. Open the serial port. Send `E` (enable display) so the device is in the expected state — the
   framebuffer is not decoded, we just need the device responsive.
2. Open a miniaudio capture device matching `--audio` (substring match on the
   input device name). 48 kHz stereo float.
3. Send play (`C` + the START button mask), begin recording.
4. Record `--seconds`.
5. Send stop (release, then the transport-stop keystroke).
6. **Trim**: find the note onset (first sample above a threshold), drop everything before it
   minus a few ms of pre-roll. Optionally trim a fixed tail.
7. Write a 48 kHz 16-bit stereo WAV.

### Serial on Windows

No library needed. `CreateFile("\\\\.\\COM4", ...)`, `SetCommState` with a `DCB` (115200, 8N1),
`WriteFile` to send bytes. The button protocol is the same as the Python client:

```
E                enable display
C <u8 mask>      button state (START = bit 3)   -- see the button table in status.md / m8_client.py
```

Sending one `C 0x08` then `C 0x00` is a START tap. Keep the existing bit order.

### miniaudio capture

Single header. `#define MINIAUDIO_IMPLEMENTATION` in exactly one TU.

```cpp
ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
cfg.capture.format   = ma_format_f32;
cfg.capture.channels = 2;
cfg.sampleRate       = 48000;
cfg.pCaptureDeviceID = <id of the device whose name matches --audio>;
cfg.dataCallback     = onFrames;   // appends frames to a std::vector<float> in pUserData
ma_device device;
ma_device_init(nullptr, &cfg, &device);
ma_device_start(&device);
// ... wait --seconds while the callback fills the buffer ...
ma_device_stop(&device);
ma_device_uninit(&device);
```

Enumerate with `ma_context_get_devices` (capture list), match `--audio` as a substring of the
device name, and print the list if no match. The data callback must be trivial — append the
incoming frames to a preallocated buffer, nothing else; it runs on miniaudio's audio thread.

### `--batch`

`probes_loaded.txt` is a plain list of `name<TAB>label` lines. The tool prompts the operator to
load each named probe on the device, waits for a keypress, then captures to `refs/<label>.wav`.
Semi-automated: the operator does the SD-card load (one action per probe), the tool does play +
record + trim + save. Good enough — the loading is the only manual step and it is one button.

### Acceptance

- `m8_capture --port ... --audio M8 --seconds 2 --out t.wav` with a probe loaded produces a WAV
  containing one note, onset trimmed to near sample 0, ≥ 1.5 s of sustain.
- `m8_analyze t.wav` on the result: no clipping, no NaN, a clear sustained tone.

---

## The full parity loop

Once both tools exist, building a synth to parity is:

```
1. m8_makeprobe --sweep shape --type macrosynth --note C-4 --out-dir probes/
2. copy probes/*.m8s to the SD card
3. m8_capture --port COM4 --audio M8 --batch loaded.txt --out-dir refs/
       (operator loads each probe on the device; tool records refs/shape_XX.wav)
4. for each probe:  m8_render --load probe_shape_XX.m8s --note C-4 --out mine_XX
5. for each:        m8_spectrum --ref refs/shape_XX.wav --test mine_XX.wav --json diff_XX.json
6. read the log-spectral distances; fix the worst; re-render (step 4); repeat
```

Steps 4–6 are pure software — the agent loops them unaided. Steps 1–3 are done once per
reference library and only need the operator for the SD-card copy and the per-probe load.

### `m8_render --note` / isolation flag

For step 4, `m8_render` needs to render one instrument playing one note in isolation. Add:

```
m8_render --load probe.m8s --note C-4 --instrument 0 --seconds 2.5 --out mine
```

It already loads `.m8s`. `--note` + `--instrument` triggers a single sustained note on that
instrument (via a synthesised one-note sequence, or by just playing the probe's own song row 0,
which already contains exactly one note — simplest). Render, trim to the note like `m8_capture`
does, so `ref` and `test` are aligned the same way before `m8_spectrum` sees them.

---

## Order

1. `m8_makeprobe` — trivial, links the library you already vendored. Verify a probe plays.
2. `m8_render --note` isolation flag.
3. `m8_capture` — serial + miniaudio capture. The only real work is the miniaudio WASAPI path and getting
   the trim right.
4. Wire the loop; capture a first reference library for MacroSynth `shape`.

Then start Macrosyn with `m8_spectrum` as the yardstick.

CMake, per `AGENTS.md`:

```cmake
add_executable(m8_makeprobe src/tools/main_makeprobe.cpp)
target_link_libraries(m8_makeprobe PRIVATE m8_files_cpp)

add_executable(m8_capture src/tools/main_capture.cpp)   # miniaudio is header-only; no link target
# m8_capture depends on NOTHING from this project — not m8_engine, not SDL. Standalone.
target_include_directories(m8_capture PRIVATE third_party/miniaudio)
```

On Windows, miniaudio's WASAPI backend needs no extra libraries beyond what the platform SDK
already provides.
