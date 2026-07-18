# m8_makesong

**Source:** [`src/tools/main_makesong.cpp`](../../src/tools/main_makesong.cpp) (215 lines)
**Build target:** `m8_makesong` (CMakeLists.txt)
**Category:** song-authoring tool (special-purpose, not a generic CLI utility)

## What it does

Regenerates the committed **"opening" demo song** (`songs/opening.m8s`, internally "Night
Drive") as a real, reloadable `.m8s` file, plus its four drum sample WAVs
(`songs/samples/{kick,snare,hat,clap}.wav`).

The app's original in-code demo (`Engine::loadDemoSong()`) synthesizes its own drum hits at
startup and never touches disk. `m8_makesong` exists to turn that in-memory demo into committed,
versioned data: it (1) regenerates the four drum sounds using the **exact same DSP** the engine
uses internally (copied verbatim from `Engine.h`'s `setupDemoSamples`, so the WAVs are
sample-identical to what the engine would have synthesized), writes them to disk, (2) builds the
demo song in a real `Engine` instance, repoints the sampler instruments at the new WAV files
(M8-absolute paths like `/samples/kick.wav`), and (3) exports the whole state via
`io::saveNewSong()`.

This is why `songs/opening.m8s` exists as a file in the repo instead of the app just calling
`loadDemoSong()` at startup — it lets the demo be loaded through the normal `.m8s` file-loading
path (same code path a real user song takes), which is a stronger test of the load/save pipeline
than a special-cased in-code fallback.

It is **not** a general "make me a song" tool — it hardcodes one specific song (the Night Drive
demo). For a from-scratch composed song, see [`m8_composesong`](m8_composesong.md).

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--out <path>` | `songs/opening.m8s` | Where to write the song file. |
| `--samples <dir>` | `songs/samples` | Directory to write the four drum WAVs into. |
| `--template <path>` | `third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s` | The empty-song template `io::saveNewSong` overlays onto (see [SongIO's write_over mechanism](../../ARCHITECTURE.md) — pre-4.0 files can't be saved in place, so a valid empty V4 template is required as the base). |

No other flags; no help text is printed for unknown args (they're silently ignored since the
parser only recognizes the three above).

## What it generates

**Drum WAVs** (`genDrum()`), 48kHz mono 16-bit PCM, procedurally synthesized:
- `kick.wav` — 0.28s, pitch-swept sine (55Hz + 160Hz·exp(-32t)) with a short click transient, soft-clipped via `tanh`.
- `snare.wav` — 0.22s, tonal body (190Hz sine, fast decay) blended with a noise "crack" component.
- `hat.wav` — 0.09s, differenced white noise (a crude high-pass via first-difference) with fast decay.
- `clap.wav` — 0.30s, noise with a 3-burst envelope (0/11/22ms) mimicking multiple clap transients, plus a longer tail.

**Song** (`songs/opening.m8s`): the full demo arrangement (8 instruments — 4 samplers, 4
MacroSynth) exported via `io::saveNewSong()`, with sampler paths repointed from the in-memory
demo names (`demo_kick.wav` etc.) to the new committed WAV paths.

**Groove-slot fixup:** the `.m8s` format doesn't persist which groove slot is globally
*active* — reloading a song always resets `project.groove` to slot 0. If the demo's active groove
(its swing feel) lived in a different slot, `m8_makesong` copies that groove's data into slot 0
before saving, so the swing survives the round trip.

## Round-trip verification

After writing, it reloads the file (`io::loadSong`) and diffs a curated set of fields against
the in-memory source: `bpm`, `mixer.out_vol`, per-track `mixer.track_vol`, `effects.rev_size`,
`effects.del_feedback`, `project.groove`, and per-instrument type-specific fields
(MacroSynth: cutoff/amp; HyperSynth: cutoff/amp/swarm; Sampler: amp/rev) plus all 4 mod slots
per instrument (dest/type/p1/p2/p3, skipping slots inactive on both sides). Prints one `DIFF`
line per mismatch and a final `round-trip OK` / `round-trip: N field(s) differ` summary. **This
check is advisory only** — a nonzero mismatch count does not set a nonzero exit code (only
write/reload *failures* do); read the printed diffs.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (drums written, song written, reload succeeded — even if round-trip diffs were printed) |
| 1 | A drum WAV failed to write, `saveNewSong` failed, or the reload/verification load failed |

## Example

```
cmake --build build --config Release --target m8_makesong
build\Release\m8_makesong.exe
build\Release\m8_render.exe --load songs/opening.m8s --sample-root songs --song --seconds 8 --out opening
```

Custom output location:
```
build\Release\m8_makesong.exe --out songs/opening_v2.m8s --samples songs/samples_v2
```

## Gotchas

- Regenerating overwrites `songs/opening.m8s` and the four sample WAVs in place — if you've hand-edited either, running this again discards those edits.
- The drum DSP here is a **copy** of `Engine.h`'s `setupDemoSamples`, not a shared function. If the engine's demo-drum synthesis ever changes, this file's `genDrum()` must be updated to match by hand, or the committed WAVs will silently drift from what `loadDemoSong()` would produce in-memory.
- `--template` must point at a valid pre-existing empty V4+ song; `saveNewSong`'s overlay mechanism can't originate a file from nothing.
