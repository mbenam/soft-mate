# m8_composesong

**Source:** [`src/tools/main_composesong.cpp`](../../src/tools/main_composesong.cpp) (520 lines)
**Build target:** `m8_composesong` (CMakeLists.txt)
**Category:** song-authoring tool (special-purpose, not a generic CLI utility)

## What it does

Authors the app's **startup songs** and writes them to `songs/`. Unlike
[`m8_makesong`](m8_makesong.md) (which re-exports the pre-existing in-code demo), this tool
composes entire arrangements from scratch, directly in C++, using the engine's data
structures (`seq.phrases`, `seq.chains`, `seq.song`) as the authoring surface.

Two songs live here, picked with `--song`:

| `--song` | song | writes | notes |
|---|---|---|---|
| `neondusk` *(default)* | **NEON DUSK**, 112 BPM, Dm - Gm - Bb - Am | `songs/neondusk.m8s` | The current startup song. |
| `sunrise` | **SUNRISE**, 128 BPM, Am - F - C - G | `songs/sunrise.m8s` | The previous startup song, kept reproducible because tests load it by name. |

### NEON DUSK

Written once FMSynth, WavSynth and HyperSynth existed, and built to put all of them
on screen at boot — SUNRISE predates them and is entirely MacroSynth and samplers:

| # | name | engine | notes |
|---|---|---|---|
| 00 | KICK | Sampler | `/samples/kick.wav`, LP + SIN limiting |
| 01 | SNARE | Sampler | `/samples/snare.wav`, high-passed clear of the kick |
| 02 | HAT | **WavSynth** | shape 8 NOISE — a hat with no sample behind it |
| 03 | BASS | **FMSynth** | algo 0, ops A/B silent so it reduces to 2-op: C modulates carrier D |
| 04 | PAD | **HyperSynth** | fifth-stack chord bank, swarm + width |
| 05 | ARP | **WavSynth** | shape 16 WT-OSC:LIQUID, MULT/WARP/SCAN for movement |
| 06 | LEAD | **MacroSynth** | shape 28 PLUCKED |
| 07 | CLAP | Sampler | `/samples/clap.wav`, once a bar on the 4 |

Four song rows of four bars each, and the build is expressed as empty song cells rather
than extra chains. It opens on **pad and arp alone** for four bars, which is the clearest
way to hear that the pad is a HyperSynth and not a saw; kick/hat/bass enter at row 1, the
lead and backbeat at row 2, and the lead drops out at row 3 before the loop.

**Chord quality is the trap** in a transpose-driven progression: the pad's chord bank is a
fixed set of intervals, so a minor voicing transposed onto a major chord fights it. The pad
and arp are therefore quality-neutral — roots, fifths and octaves, no third anywhere — and
the lead is D-minor pentatonic with `TRANSP OFF` so it holds still while the harmony moves.
The four transposes `{0, -7, -4, -5}` all descend, which keeps the bass in its register
instead of climbing away over the cycle.

### SUNRISE

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

The app loads the startup `.m8s` from disk (`songs/neondusk.m8s` — see `status.md`'s "Startup /
demo songs" section) — nothing about either song's content lives in the app binary itself.

> **Regenerating `sunrise.m8s` does not currently reproduce the committed file byte for byte.**
> The engine has drifted since it was last written (instrument `AMP`/`VOLUME` handling, among
> other things), so the bytes differ even from the pre-`--song` version of this tool. The
> committed file is what the tests load; leave it alone unless you mean to re-baseline it.

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
| `--song <name>` | `neondusk` | Which song to author: `neondusk` or `sunrise`. Any other value exits 2. |
| `--out <path>` | `songs/<song>.m8s` | Where to write the song file. |
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
