# M8 Tracker Clone — Status

Last reviewed: 2026-07-13

## Project Goal & Architecture Spec

Build a functional software clone of the **Dirtywave M8 Tracker**: the tracker workflow,
the 2D view navigation system, the custom UI layout, and the audio engine.

Target architecture ("holy trinity" — see *Design Intent* vs *Reality* below):

1. **Separation of concerns.** DSP/engine math fully decoupled from the SDL UI layer.
   Engine is a black box with a single `render(buffer, frames)` entry point.
2. **Lock-free SPSC command ring.** The UI thread never mutates engine state directly.
3. **Sample-accurate sequencer clock.** Sequencer driven from the audio callback, not
   an OS timer.



## Tech Stack

- **Language**: C++20
- **Framework**: SDL3 (windowing, input, audio streams)
- **DSP Library**: DaisySP (LGPL modules)
- **Build**: CMake + FetchContent
- **Target OS**: Windows (PC first; Android/handheld ports later)

---

## Implemented and working

### UI layer

- **Renderer & typography.** Custom 5x7 font on a strict 8x8 bounding-box grid,
  320x240 at a 40x30 character grid.
- **View navigation.** The 2D view hierarchy with `Shift + Arrow` movement, plus the
  dynamic minimap (main row `S C P I T`, with vertical column isolation).
- **`ViewManager`.** Centralised screen routing via `m8::ui::ViewType`, replacing the
  earlier inline `if/else` chain in `main.cpp`. Modal push/pop for the file browser.
- **Dictionary-based layout system.** Screens declare fields in a map plus a `NavGraph`
  of Up/Down/Left/Right neighbours, supporting irregular (non-grid) layouts.
- **Screens rendered.** Song, Chain, Phrase, Instrument (Sampler + Macrosyn layouts),
  Table, Project, Groove, Scale, Mods, Instrument Pool, Mixer, Effects, File Browser.
- **Edit mode.** `X` + arrows increments/decrements hex values in the tracker grids.
- **File browser.** `std::filesystem` listing of `.wav` files with directory traversal.

### Engine layer

- **`CommandRing`.** A working SPSC lock-free ring (`CommandRing<EngineCommand, 1024>`).
- **`Engine::render`.** Stereo output, 8 tracks summed, per-track volume, equal-power
  pan, per-instrument dry/chorus/delay/reverb sends, master bus with `daisysp::Chorus`,
  two `DelayLine`s, `ReverbSc`, and a `tanh` soft clip.
- **`SynthVoice`.** One monophonic voice per track (matching hardware). POLYBLEP saw
  oscillator, ADSR, `daisysp::Svf` filter with LP/HP/BP selection and cutoff/res read
  from the instrument. Linear-interpolated sample playback with a phase accumulator.
- **`Sequencer`.** Song → chain → phrase → step walk across 8 tracks, with chain
  transpose, a demo song, and sub-step groove ticking (default 6 ticks per 16th).
- **FX commands.** `HOP`, `DEL`, `KIL` parsed and executed inline.
- **Parameter dispatch.** `ParamID` enum + `applyParameterUpdate` covering project,
  mixer, effects, instrument, sampler, macrosyn, modulator and scale parameters.
- **WAV decoding.** `dr_wav` integrated; files decode to float on load.
- **Pass 1/2/3/4 remediations landed.** Zero-allocation lock-free engine logic. Synchronized Playhead tracking across UI modes. `PlayMode` bound checks. Robust instrument decoupling logic. Full offline test suite (B-series) integrated and passing cleanly under ASan, UBSan, and TSan.
- **Pass 5 audio fixes.** Pitch modulation scaled correctly (semitones, not octaves). DC blocker on master bus, delay and reverb feedback paths. Demo mixer rebalanced for headroom (peak 0.855, no clipping). NOTE_OFF spam eliminated (3800 → 271 events).

---

## Known Caveats

- **GC ring overflow leaks one buffer.** `m_gcRing.push(old)` in `LOAD_SAMPLE` ignores its
  return value. The audio thread cannot free, and blocking is not an option, so leaking on
  a full 64-slot ring is the correct trade. The UI drains it every frame; overflow would
  require 64 sample loads inside one video frame. Leave it, but add a comment saying so
  explicitly, so a future reader doesn't "fix" it into a free on the audio thread.

---

## Spec'd but NOT implemented

These are described in earlier design notes and/or have UI surface, but the engine does
not read them. Treat as future work.

- **Macrosyn engine.** No Braids, no oscillator model at all. `shape`, `timbre`,
  `color`, `redux` are stored and ignored — every instrument renders the same POLYBLEP
  saw (or a sample). Wavsynth, FM and Hypersynth do not exist.
- **Per-instrument amp envelope.** `SynthVoice`'s ADSR is hardcoded
  (10 ms / 100 ms / 0.5 / 200 ms) and never pulled from the instrument.
- **Modulation matrix.** Partially wired: mod slots can reach VOLUME / PITCH / CUTOFF,
  but envelope times are a raw `p/255` seconds mapping, LFOs are free-running, and the
  AHD/DRUM/TRIG/TRACKING types are not distinguished.
- **Tables.** `TableScreen` edits `tables[]`; the engine never reads them. `TBL_TIC`
  is inert.
- **Sampler features.** `start`, `loop_st`, `length`, `slice`, `play` (FWD/REV) are
  stored and unused. No looping, no start offset, no reverse, no slicing.
- **Scales.** Key, tune and per-note enable/offset are stored; note lookup ignores them.
- **Project transpose, degrade, EQ, limiter, DJF, input/USB mixer channels.** Stored,
  unused.
- **Project persistence.** No save/load of songs; the demo song is generated in code.

---

## Next steps

1. Begin feature work on the sampler playback modes (looping, slicing, start offset).
2. Implement Tables.
3. Implement Macrosyn oscillator.
