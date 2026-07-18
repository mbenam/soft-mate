# m8_render

**Source:** [`src/tools/main_render.cpp`](../../src/tools/main_render.cpp) (482 lines)
**Build target:** `m8_render` (CMakeLists.txt)
**Category:** offline audio rendering (the render half of the render→analyze→spectrum closed
loop; the ground-truth "what should this sound like" oracle for the whole project)
**Links:** `m8_engine` only. No SDL, no audio device, no serial.

## What it does

Drives the real `Engine` (the same engine the live app uses) with no audio device attached,
pumping `engine.render()` in fixed-size chunks, and writes the result to disk as a stereo 16-bit
WAV plus a **CSV event log** of every `NOTE_ON`/`NOTE_OFF`/`TICK`/`ROW` event with its exact
sample-accurate timestamp. The event log is the actual point of the tool: it turns "does this
sound right" from a listening exercise into something [`m8_analyze`](m8_analyze.md) and
[`m8_spectrum`](m8_spectrum.md) can check against ground truth mechanically.

It can render:
- **The in-code demo song** (`loadDemoSong()`) — no `--load` needed.
- **A real `.m8s` file** (`--load`), with samples resolved via `--sample-root`.
- **The whole song**, **one chain**, or **one phrase looped** (`--song`/`--chain N`/`--phrase N`).
- **One track soloed** (`--solo N`, mutes the other 7 via a queued `MIX_TRK_VOL` command — never
  by mutating engine state directly, since that would race with the queued `LOAD_SONG` command
  when `--load` is also used).
- **One instrument in isolation** (`--note`+`--instrument`) — intended for probe files (see
  [`m8_makeprobe`](m8_makeprobe.md)): solos track 0 and plays song row 0, on the assumption the
  probe puts its one test note there.
- **A full batch** (`--batch`, or no args at all): the whole song, all 8 solo tracks, and every
  non-empty phrase in the song (looped 8s each) — one big self-documenting dump of everything the
  song contains, useful when you don't know what's actually in a file yet.

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--seconds <n>` | `40.0` | Render duration in seconds (single-render modes only; batch phrase renders are hardcoded to 8s each, solo/song renders use this value). |
| `--out <name>` | `render` | Output basename (single-render mode only). Writes `<name>.wav` and `<name>_events.csv`. |
| `--load <path>` | *(none — uses the demo song)* | Load a real `.m8s` file instead of the in-code demo. Setting this also turns off batch-by-default (`argc==1`). |
| `--sample-root <dir>` | *(empty — samples resolved relative to CWD)* | Root directory sampler paths (M8-absolute, e.g. `/samples/kick.wav`) are resolved under. |
| `--song` | *(default mode)* | Render mode = SONG, from song row 0. |
| `--chain <id>` | — | Render mode = CHAIN; `<id>` is the chain number (accepts `0x` hex). |
| `--phrase <id>` | — | Render mode = PHRASE; `<id>` is the phrase number (accepts `0x` hex). |
| `--track <n>` | `0` | Which track column to use as context for chain/phrase mode. |
| `--row <n>` | `0` | Starting row within song mode. |
| `--solo <n>` | *(none — all tracks audible)* | Mute every track except `n`. Forces mode back to SONG. |
| `--note <name>` | — | Paired with `--instrument`; see isolation mode below. Value itself isn't validated/used beyond being non-empty (the note actually played comes from the probe song's own row 0 content). |
| `--instrument <n>` | `-1` | Paired with `--note`: solo track 0, play song row 0 — the layout `m8_makeprobe` produces. |
| `--batch` | `true` if no args at all, else `false` | Force batch mode (full song + all 8 solos + every non-empty phrase). |

Any unrecognized flag prints `unknown arg: <flag>` and exits 1.

## Output files

**WAV** — 16-bit PCM, stereo, interleaved, sample rate = `kSampleRate` (the engine's fixed
render rate). Samples are hard-clamped to `[-1, 1]` before quantizing (silent clipping, not
reported as an error by this tool — that's `m8_analyze`'s job).

**Events CSV** (`<out>_events.csv`) — one row per engine event:
```
sample_time,seconds,type,track,song_row,chain_row,phrase_row,instrument,frequency,volume
```
`type` is one of `NOTE_ON`, `NOTE_OFF`, `TICK`, `ROW`. This is the authoritative "what actually
happened and when" — use it to verify a render's content independent of listening to the audio
(e.g. "did track 3 get a NOTE_ON at all," "what was the exact sample where phrase 0x0C started").

**Track info** (stdout only, when `--load` is used) — before rendering, dumps every non-empty
instrument slot: index, name, type (`SAMPLER`/`MACROSYN`/`HYPERSYN`/`MIDI`/`NONE (unimplemented —
silent)`), and sample path for samplers. Useful for sanity-checking what a `.m8s` file actually
contains before spending render time on it. Note the `INST_NONE` special case: a genuinely-empty
slot (default name `------------`) is skipped, but a slot loaded from a file with an
**unimplemented type** (FM/Hyper/Wav/MIDIOut/External at the time this was written) is *also*
`INST_NONE` internally but keeps the file's real name — this printout is how you tell "nothing
here" apart from "something here the engine can't play" instead of both looking identical.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Unknown CLI arg, or `--load` failed (bad file path or load error — printed to stderr) |

Missing sample files (referenced by the song but not found under `--sample-root`) print a
warning to stderr but do **not** fail the render — the instrument just plays silently for that
slot. Check the "samples: N loaded, M missing" line.

## Examples

```
# The in-code demo, whole batch (song + 8 solos + every phrase)
m8_render

# The demo song, 40s
m8_render --song --seconds 40

# One phrase looped on one track
m8_render --phrase 0x0C --track 3

# Song with only track 3 audible
m8_render --solo 3

# A real song file, samples resolved from the SD-card mirror
m8_render --load songs/sunrise.m8s --sample-root songs --song --seconds 30 --out sunrise

# Full batch render of a real song
m8_render --load songs/sunrise.m8s --sample-root songs --batch

# Isolate one instrument (probe-file convention: note on song row 0)
m8_render --load probe.m8s --note C-4 --instrument 0 --seconds 2.5 --out mine
```

## Gotchas

- **No args = batch mode of the demo song.** If you meant to render just the song, pass `--song`
  explicitly — an empty invocation renders everything.
- **`--load` disables batch-by-default** (only the `argc==1` check sets it), so `m8_render --load
  x.m8s` alone renders in single mode with the *default* mode/target (SONG, row 0), not a batch —
  pass `--batch` explicitly if you want the full dump of a loaded file.
- **Solo muting is queued, not immediate**, specifically so it survives a `--load`'s `LOAD_SONG`
  command clobbering the whole mixer state. If you're extending this tool, don't mutate
  `engine state` directly for anything that needs to outlive a `LOAD_SONG` — queue a command
  instead, since the ring is FIFO and `LOAD_SONG` always runs first.
- **The "isolation" mode (`--note`+`--instrument`) doesn't actually select an instrument or
  transpose to the given note** — it hardcodes solo-track-0 + song-row-0 and trusts that whatever
  content is there (as authored by `m8_makeprobe`) is the thing you want to hear. Passing a
  `--note`/`--instrument` combo against a non-probe file won't do what the flag names imply.
- **Clipping is silent here.** This tool clamps and writes; it never reports clipped-sample
  counts itself (unlike `summarise()`'s own peak/rms/clipped printout, which *does* report it to
  stdout — but nothing fails the exit code). Run `m8_analyze` on the output if you need a hard
  pass/fail gate on audio health.
