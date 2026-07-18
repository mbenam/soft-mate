# m8_composesong

**Source:** [`src/tools/main_composesong.cpp`](../../src/tools/main_composesong.cpp) (191 lines)
**Build target:** `m8_composesong` (CMakeLists.txt)
**Category:** song-authoring tool (special-purpose, not a generic CLI utility)

## What it does

Authors the app's **startup song**, "SUNRISE," and writes it to `songs/sunrise.m8s`. Unlike
[`m8_makesong`](m8_makesong.md) (which re-exports the pre-existing in-code demo), this tool
composes an entirely new arrangement from scratch, directly in C++, using the engine's data
structures (`seq.phrases`, `seq.chains`, `seq.song`) as the authoring surface.

SUNRISE is deliberately distinct from the older "Night Drive" demo:
- 128 BPM, four-on-the-floor house feel, straight timing (no swing/groove).
- A-minor chord progression **Am → F → C → G**, driven entirely by **chain transpose** (see
  below) — a bright, uplifting 4-bar loop.
- Sampler drums (reusing the committed `/samples/*.wav` kit from `m8_makesong`) + MacroSynth saw
  for bass/pad/arp/lead (reusing the demo's tuned envelope/filter patches, just retriggered with
  new notes).
- A 16-bar structure with a build: drums+bass+pad from bar 1, clap+arp+snare enter at bar 5,
  lead enters at bar 9.

It reuses the demo's *instrument patches* (the tuned envelopes and filter settings — loaded via
`eng.loadDemoSong()` as a starting point) but replaces every note, chain, and song-row with new
material, and clears the sequencer first (`seq.clear()`) so nothing of the old arrangement
survives.

The app loads this `.m8s` at startup (`songs/sunrise.m8s` — see `status.md`'s "Startup / demo
songs" section) — nothing about the song's content lives in the app binary itself.

## Musical/data design (useful context for editing it)

- **Chord progression via chain transpose, not new phrases.** Each lane (kick, clap, hat, bass,
  pad, arp, lead, snare) has exactly one chain, one phrase pattern, reused across all 4 bars of
  the progression. The *pitch* changes come from `CH(lane, slot, phrase, transpose)` — the
  per-slot transpose value (`prog[4] = {0, -4, +3, -2}`, i.e. A→F→C→G in semitones) applied at
  the chain level.
- **Drums ignore the progression.** Percussion instruments have `transp = 0` (set explicitly:
  `for (int i : {0,1,2,7}) st.instruments[i].sampler.transp = 0;`), so chain transpose has no
  effect on them even though they share the same chain-transpose mechanism structurally.
- **The build is a song-level effect, not a chain-level one.** Every chain is the same length (4
  bars: one full progression cycle), so all lanes always advance in lockstep — there's no drag
  between lanes of different lengths. The "build" (drums-only → +clap/arp/snare → +lead) is
  expressed purely as which song-row cells are populated vs. left empty (`0xFF` = "this lane
  rests this section").
- **Lead is fixed-pitch, not transposed** (two alternating phrases, A-minor-pentatonic, chosen to
  stay consonant against every chord in the progression without needing per-chord variants).

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--out <path>` | `songs/sunrise.m8s` | Where to write the song file. |
| `--template <path>` | `third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s` | Empty-song template `io::saveNewSong` overlays onto. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (song written, reload succeeded) |
| 1 | `saveNewSong` failed, or the verification reload failed |

Missing samples after reload (`rt.missing`) print a `WARN` but do **not** fail the run — check
the printed list if you see it (it usually means `/samples/*.wav` don't exist yet; run
`m8_makesong` first, since `m8_composesong` reuses that kit rather than generating its own).

## Example

```
cmake --build build --config Release --target m8_composesong
build\Release\m8_composesong.exe
build\Release\m8_render.exe --load songs/sunrise.m8s --sample-root songs --song --seconds 30 --out sunrise
```

## Gotchas

- **Depends on the drum kit existing.** SUNRISE's sampler drum instruments point at
  `/samples/kick.wav` etc. — the same files `m8_makesong` writes. If you've never run
  `m8_makesong` (or deleted `songs/samples/`), the reload step will report missing samples (a
  warning, not a hard failure, but the resulting song will be silent on those lanes until the
  samples exist).
- Regenerating overwrites `songs/sunrise.m8s` in place — hand edits to that file are lost.
- The instrument list (indices 0-7: KICK, SNARE, HAT, BASS, PAD, ARP, LEAD, CLAP) and the tuned
  synth patches come from `eng.loadDemoSong()`, the same seed `m8_makesong` uses. If that demo
  patch set changes, SUNRISE's timbres change too, even though its arrangement code is untouched.
- Unlike `m8_makesong`, there is **no field-by-field round-trip diff** printed here — only a
  reload success/failure check and a missing-samples warning.
