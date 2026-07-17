# FX Commands Implementation Spec

This spec covers every M8 FX command: what exists today, what's missing, and the
exact behavioral contract for each. Engine-only (no UI wiring here).

---

## Current state

### Enum (`SeqTypes.h`)

```
FxCmd: NONE=0, VOL=1, PIT=2, DEL=3, REV=4, HOP=5, KIL=6, TBL=7, GRV=8, TIC=9
```

### What works today

| Cmd | Phrase engine | Table engine | File I/O | UI selectable |
|-----|:---:|:---:|:---:|:---:|
| VOL | -- | yes | yes (lib 0x00) | yes |
| PIT | -- | yes | yes (lib 0x01) | yes |
| DEL | yes | -- | yes (lib 0x02) | yes |
| REV | -- (stub) | -- | yes (lib 0x03) | yes |
| HOP | yes | yes | yes (lib 0x04) | yes |
| KIL | yes | -- | yes (lib 0x05) | yes |
| TBL | yes | -- | -- (dropped) | -- (max=6) |
| GRV | yes | -- | -- (dropped) | -- (max=6) |
| TIC | -- | yes | -- (dropped) | -- (max=6) |

### What's missing

All commands below are absent from the codebase: ARP, ARC, CHA, RND, RNL, RET,
REP, RTO, RMX, NTH, PSL, PBN, PVB, PVX, SCA, SCG, SNG, SED, THO, TBX, TPO,
TSP, NXT, OFF, MTT, INS, GGR, and all mixer/FX send commands.

---

## Part A — Relative vs Absolute values

Every FX command value (the `val` byte) is interpreted in one of two ways:

**Absolute**: val 0x10 sets the parameter to 0x10, period.

**Relative** (default for instrument-parameter commands): val 01-7F adds to the
current parameter value; val FF-80 subtracts. The parameter wraps at 0x00/0xFF.
Relative offsets accumulate until a note-on (new instrument trigger or RET),
which resets all parameters to the instrument's stored values.

Relative value computation:

```
offset = (val < 0x80) ? val : val - 256     // signed: 01..7F = +1..+127, 80..FF = -128..-1
result = clamp(param + offset, 0, 255)
```

### Phrase vs Table context

In a phrase, VOL/PIT are relative instrument-parameter commands (offset the
current instrument's volume/pitch).

In a table, VOL multiplies the row volume; PIT adds signed semitones. These
are not relative to the instrument — they are additive to the table row.

---

## Part B — Phrase FX commands

All phrase FX are parsed in `tickTrack()` on `playTick == 0`. Three FX slots
per row, processed left to right. Later slots override earlier ones for
conflicting commands (e.g., two DELs: the last one wins).

### DEL XX — Note-on delay

**Value**: ticks to delay (0 = immediate, max = grooveLength - 1).

**Behavior**: When a row has a note + DEL, the note-on fires after `val` ticks
instead of at tick 0. The note still occupies the full groove window — the
delay just shifts when noteOn() is called. If DEL val >= grooveLength, it is
clamped.

**Edge case**: val=0 behaves identically to no DEL (note fires at tick 0).

**State**: `m_state.pendingDel[t]` (int, -1 = none).

**Reset**: cleared at the start of each row.

---

### KIL XX — Kill note

**Value**: ticks until note-off (0 = immediate kill, max = grooveLength - 1).

**Behavior**: `val` ticks after the row starts, `emitNoteOff(t)` fires. If val
>= grooveLength, clamped.

**State**: `m_state.pendingKil[t]` (int, -1 = none).

**Reset**: cleared at the start of each row.

---

### HOP XY — Row hop

**Value**: Y = target phrase row (0x00-0x0F).

**Behavior**: When the current groove window expires (playTick reaches
grooveLength), the phrase row jumps to Y instead of advancing to row+1.

**Edge case**: HOPFF on phrase stops the current track (sets playMode = NONE
for CHAIN mode, or triggers song row advance for SONG mode). This is the
existing "HOP to row 0xFF" behavior — 0xFF > 15 so it falls through to the
existing chain-end logic.

**State**: `m_state.nextHop[t]` (int, -1 = none).

**Reset**: cleared at the start of each row.

---

### TBL XX — Assign table

**Value**: table index (0x00-0xFF, only 0-255 valid).

**Behavior**: Assigns table `val` to the current track's instrument. Resets
table row to 0, tickCount to 0. Reads the instrument's tbl_tic for the tick
rate. Does not retrigger the note.

**State**: `m_tableState[t]`.

---

### GRV XX — Per-track groove

**Value**: groove index (0x00-0x1F = assign groove; 0x20+ = revert to project
groove).

**Behavior**: Overrides the groove for the current track. The groove index is
read at the start of each tick (per-track override takes priority over
project.groove). Resets playGrooveIndex to 0.

**State**: `m_state.trackGroove[t]` (-1 = project groove).

---

### VOL XX — Volume (phrase)

**Value**: relative offset (01-7F add, FF-80 subtract).

**Behavior**: Offsets the instrument's effective volume by the relative amount.
Applied as: `effVol += bipolarOffset`. Does not retrigger the note. Affects
the current note if one is playing, otherwise affects the next note.

**Implementation**:
- Add to `EngineState`: `float pendingVolOffset[8] = {0}`.
- On parseFX: accumulate `pendingVolOffset[t] += bipolarAmt(val)`.
- On note-on: apply `pendingVolOffset` to the voice's volume, then reset to 0.
- While note is sounding: apply `pendingVolOffset` each tick via
  `m_voices[t].setVolume()`.

**Reset**: on note-on (instrument trigger resets all parameters).

---

### PIT XX — Pitch (phrase)

**Value**: relative semitone offset (01-7F = +1..+127, FF-80 = -128..-1).

**Behavior**: Offsets the instrument's pitch by the relative amount in
semitones. Accumulates across rows until a note-on resets it.

**Implementation**:
- Add to `EngineState`: `int pendingPitchOffset[8] = {0}`.
- On parseFX: accumulate `pendingPitchOffset[t] += (int8_t)val`.
- On note-on: apply offset to `m_voices[t]` via a new `m_pitchTranspose`
  member (similar to `m_tableTranspose`), then reset to 0.
- In `renderSample()`: add `m_pitchTranspose` to semis (sampler path) or
  frequency multiplier (synth path).

**Reset**: on note-on.

---

### REV XX — Reverse

**Value**: 00 = no change, 01 = reverse playback.

**Behavior**: Toggles the sampler's play mode to REV for the current note.
Only affects INST_SAMPLER. Non-sampler instruments ignore this.

**Implementation**:
- Add to `EngineState`: `bool pendingReverse[8] = {false}`.
- On parseFX: set `pendingReverse[t] = (val != 0)`.
- On note-on: if pendingReverse, temporarily override
  `sampler.play = SamplePlayMode::REV`. On next note-on without REV, restore
  original play mode.

**Note**: This is a placeholder. The real M8 REV is more nuanced (it can be
relative to the current play mode). Start with this simple version.

---

### INS XX — Instrument change

**Value**: instrument index (0x00-0x7F).

**Behavior**: Changes the instrument for the current track mid-phrase without
a note column entry. Takes effect immediately (unlike the I column which only
applies on note-on).

**Implementation**:
- On parseFX: `m_trackInstrument[t] = val`. Update voice's instrument pointer
  via `m_voices[t].setInstrument()`.

---

### OFF XX — Note off (ADSR release)

**Value**: ticks until release triggers (0 = immediate).

**Behavior**: Like KIL, but instead of hard noteOff(), it triggers the ADSR
release stage. The voice continues sounding through its release envelope.

**Implementation**:
- Add to `EngineState`: `int pendingOff[8] = {-1}`.
- On parseFX: `pendingOff[t] = min(val, grooveLength - 1)`.
- On tick match: call `m_voices[t].noteOff()` (which already triggers ADSR
  release via `m_adsrEnv[i].gate(false)`).

---

### SNG XX — Song hop

**Value**: relative song row offset (00-7F = forward, 80-FF = backward).

**Behavior**: Sets the play position for the current track to a relative song
row from its current position. If the target is not playable, the command is
ignored.

**Implementation**:
- Add to `EngineState`: `int pendingSongHop[8] = {-1}`.
- On parseFX: compute target = `m_state.playSongRow[t] + (int8_t)val`. Clamp
  to 0..SONG_ROWS-1. If target row has a valid chain for this track, set
  `m_state.playSongRow[t] = target` and reset chainRow/phraseRow to 0.

---

### TPO XX — Tempo

**Value**: BPM value (0x00-0xFF maps to 0-255 BPM; the M8 uses a hex-to-dec
translation shown in the help text).

**Behavior**: Sets the song tempo. Takes effect immediately.

**Implementation**:
- On parseFX: `m_state.bpm = val; m_state.bpm_frac = 0; recalcBPM();`.

---

### TSP XX — Global transpose

**Value**: signed transpose (0x80 = 0, 0x00 = -128, 0xFF = +127).

**Behavior**: Sets the project-level transpose. Affects all tracks.

**Implementation**:
- On parseFX: `m_state.project.transpose = (int8_t)val`.

---

### MTT XX — Micro-time

**Value**: signed sub-tick offset (-128 to +127, in 1/8th tick units).

**Behavior**: Adjusts the current row's playback timing by a fractional tick
amount. Negative offsets delay the row; positive offsets advance it. Does not
work on the first phrase row. No effect in tables.

**Implementation**:
- This requires sub-tick timing in the tick accumulator. Add to EngineState:
  `int microTime[8] = {0}`.
- On parseFX: `microTime[t] = (int8_t)val`.
- In tickTrack: when advancing the phrase row, add `microTime[t] / 8.0` to
  the tick phase. This shifts when the next row's tick fires.

---

## Part C — Table FX commands

Table FX are parsed in `tickTable()`. Three FX slots per row. Affects the
table's own playback, not the phrase.

### VOL XX — Table volume

**Value**: volume level (0x00 = silent, 0x7F = full; values > 0x7F = full).

**Behavior**: Multiplies the table row's base volume. Applied as:
`tableVol *= (val <= 0x7F) ? val/127.0 : 1.0`.

---

### PIT XX — Table pitch

**Value**: signed semitone offset (int8_t).

**Behavior**: Adds to the table row's transpose value. Applied as:
`tableTransp += (float)(int8_t)val`.

---

### HOP XX — Table row hop

**Value**: target row (0x00-0x0F).

**Behavior**: Jumps to the target row immediately. The target row's modulation
is applied on the same tick. If val >= ROWS, ignored.

---

### TIC XX — Table tick rate override

**Value**: tick rate for this FX column (same encoding as tbl_tic).

**Behavior**: Overrides the tick rate for the FX column where TIC appears.
The effective rate for the table is determined by column 0 (or the TIC in
column 0 if present).

---

### DEL XX — Table delay

**Value**: ticks to delay table advancement.

**Behavior**: Delays the table playhead by val ticks. The row's modulation is
still applied immediately, but the row advance is delayed.

**Implementation**:
- Add to `TableState`: `int delayCount = -1`.
- On parseFX: `ts.delayCount = val`.
- In tickTable: if `delayCount > 0`, decrement instead of advancing row.

---

### THO XX — Table hop (all columns)

**Value**: target row (0x00-0x0F).

**Behavior**: Like HOP but affects all columns. Used when the table has
independent column tick rates (TIC per column).

**Implementation**: Same as HOP but applies to all three column tick counters,
not just the current column.

---

## Part D — Arpeggio

### ARP XY — Arpeggio

**Value**: X = first interval (semitones), Y = second interval (semitones).

**Behavior**: Produces a rapid 3-note arpeggio cycling through: base note,
base+X, base+Y. The cycle rate defaults to every tick (configurable via ARC).
The arpeggio is active until a new note triggers.

**Implementation**:
- Add to `EngineState`: `int arpInterval[3][8] = {{0}}; bool arpActive[8] = {false}; int arpPhase[8] = {0}`.
- On parseFX: `arpInterval[0][t] = 0; arpInterval[1][t] = X; arpInterval[2][t] = Y; arpActive[t] = true; arpPhase[t] = 0`.
- Each tick: if arpActive, cycle arpPhase (0,1,2,0,1,2...), compute freq =
  baseFreq * 2^(arpInterval[phase]/12), call `m_voices[t].setFrequency(freq)`.
- On note-on: reset arpPhase to 0, read new base frequency.

### ARC XY — Arpeggio config

**Value**: X = mode (0 = up, 1 = down, 2 = up-down, 3 = random), Y = speed
in ticks.

**Behavior**: Configures the arpeggio cycling mode and rate.

**Implementation**:
- Add to `EngineState`: `int arpMode[8] = {0}; int arpSpeed[8] = {1}`.
- On parseFX: `arpMode[t] = X; arpSpeed[t] = max(Y, 1)`.
- ARP speed divides the tick counter: `if (tickCount % arpSpeed == 0) cycle`.

---

## Part E — Probability and randomness

### CHA XY — Chance

**Value**: X = left probability (0=never, F=always), Y = right probability.

**Phrase behavior**: Individually sets the probability for FX commands to the
left (X) and right (Y) of CHA. Each FX slot on the same row is independently
gated: with probability X/15 for left slots and Y/15 for right slots.

**Table behavior**: Sets a single probability for everything to the left of CHA
on that row.

**Implementation**:
- On parseFX: for each FX slot to the left of CHA in the same row, roll a
  random number. If roll > (X/15.0), clear that FX slot to NONE for this tick.
- Use `xorshift32` seeded per-row (deterministic per the LFO RANDOM pattern).

### RND XY — Randomize previous FX

**Value**: X = left range, Y = right range (0=none, F=full range).

**Behavior**: Randomizes the previously active FX command. The previous FX
command's value is offset by a random amount within the specified range.

**Implementation**:
- On parseFX: find the most recent non-NONE FX slot before RND in the same
  row. Generate random offset in [-X..+X] (left nibble) and [-Y..+Y] (right
  nibble). Apply to that FX slot's value.

### RNL XY — Randomize left command

**Value**: X = left range, Y = right range.

**Behavior**: Like RND but specifically randomizes the FX command to the left.
In the first column, randomizes note and instrument number.

### NTH XY — Nth trigger

**Value**: X = skip-left count, Y = skip-right count.

**Behavior**: Conditional trigger based on loop count. Skips everything to the
left (X) or right (Y) of the NTH command based on how many times the phrase
or table has looped.

**Implementation**:
- Add to `EngineState`: `int loopCount[8] = {0}`. Increment when phrase row
  wraps from 15 to 0.
- On parseFX: if `(loopCount[t] % (X+1)) != 0`, clear left FX slots. If
  `(loopCount[t] % (Y+1)) != 0`, clear right FX slots.

### SED XX — Random seed

**Value**: seed byte.

**Behavior**: Sets the random seed for the current track. Resets all random
values to a specific state.

**Implementation**:
- On parseFX: `m_rngState[t] = val * 2654435761u` (or similar seed
  expansion). All subsequent RND/CHA/RNL operations on this track use this
  seed.

---

## Part F — Repeat commands

### REP XX — Repeat

**Value**: increment per step (0 = stop repeat).

**Behavior**: Repeats the last FX command, incrementing its value by XX each
step. Active until a new command or REP00 stops it. Persists across note
triggers (the value maintains its position).

**Implementation**:
- Add to `EngineState`:
  ```
  FxCmd repCmd[3][8] = {{NONE}};  // per-slot, per-track
  uint8_t repVal[3][8] = {0};
  uint8_t repInc[3][8] = {0};
  bool repActive[3][8] = {false};
  ```
- On parseFX (REP): `repInc[slot][t] = val; repActive[slot][t] = (val != 0)`.
- On each tick: if repActive, apply `repVal[slot][t] += repInc[slot][t]` to
  the target FX command.
- On new command in the same slot: `repActive[slot][t] = false`.

### RTO XX — Repeat to

**Value**: target value to stop at.

**Behavior**: Sets the min or max value that REP stops at. Used with REP to
create bounded repeat cycles.

---

## Part G — Pitch modulation

### PSL XX — Pitch slide (portamento)

**Value**: slide time in ticks.

**Behavior**: Enables portamento for the currently playing instrument. The
pitch slides from the previous note to the current note over XX ticks.

**Implementation**:
- Add to `SynthVoice`: `float m_portaTarget = 0.0f; int m_portaTime = 0; int m_portaTick = 0; float m_portaStart = 0.0f`.
- On note-on: `m_portaStart = m_frequency; m_portaTarget = newFreq; m_portaTime = val; m_portaTick = 0`.
- Each tick: `m_portaTick++`. In renderSample: `freq = lerp(m_portaStart, m_portaTarget, min(m_portaTick / m_portaTime, 1.0))`.

### PBN XX — Pitch bend

**Value**: 0x00-0x7F = bend up, 0x80-0xFF = bend down. Magnitude = |val - 0x80|.

**Behavior**: Continuous pitch slide. The bend amount is applied per-tick as a
semitone offset that accumulates.

**Implementation**:
- Add to `EngineState`: `int pitchBend[8] = {0}`.
- On parseFX: `pitchBend[t] = (int8_t)val`.
- Each tick: accumulate pitch bend into the voice's pitch offset. The rate is
  proportional to the bend value.

### PVB XY — Vibrato

**Value**: X = speed, Y = depth.

**Behavior**: Applies vibrato (LFO on pitch) to the currently playing note.
Speed X sets the LFO frequency; depth Y sets the modulation depth.

**Implementation**:
- Add to `SynthVoice`: `float m_vibPhase = 0.0f; int m_vibSpeed = 0; int m_vibDepth = 0`.
- On parseFX: set speed/depth. In renderSample: generate sine LFO at speed,
  apply `depth * sin(phase)` semitones to pitch.

### PVX XY — Extreme vibrato

**Value**: Same as PVB but with higher depth and rate range.

**Behavior**: Same as PVB with extended range.

---

## Part H — Track/scale commands

### SCA XY — Track scale

**Value**: X = key (0=B, 1=C, ... 11=A#), Y = scale number (0-15).

**Behavior**: Sets the key signature and scale for the given track. Notes are
quantized to the scale.

**Implementation**:
- Add to `EngineState`: `int trackScale[8] = {-1}; int trackKey[8] = {0}`.
- On parseFX: `trackScale[t] = Y; trackKey[t] = X`.
- In tickTrack: when computing freq from midi note, quantize to scale.

### SCG XY — Global scale

**Value**: X = key, Y = scale number.

**Behavior**: Sets the key/scale for all tracks.

**Implementation**:
- On parseFX: `m_state.project.scale = Y; m_state.project.transpose = X` (or
  a dedicated scale fields).

---

## Part I — Remix and global commands

### RMX XY — Remix

**Value**: X = track mask, Y = phrase row position.

**Behavior**: Sets the phrase playhead position (Y) of tracks to the left of
the current track. Selects which tracks are affected using X.

**Implementation**:
- On parseFX: for each track `t2 < currentTrack` where `(X >> t2) & 1` is
  set: `m_state.playPhraseRow[t2] = Y`.

### GGR XX — Global groove

**Value**: groove index.

**Behavior**: Sets the groove for all tracks (overrides per-track groove).

**Implementation**:
- On parseFX: `m_state.project.groove = val; for (int i=0; i<8; ++i) m_state.trackGroove[i] = -1`.

### NXT XX — Trigger instrument on next track

**Value**: instrument index.

**Behavior**: Plays instrument XX using the current note and velocity on the
track to the right. Does not work on track 8.

**Implementation**:
- On note-on: if `t < 7`, trigger `m_voices[t+1]` with the current note
  frequency and volume.

### TBX XX — Auxiliary table

**Value**: table index (0x00 = stop auxiliary table).

**Behavior**: Assigns a table to play in parallel with any instrument tables.
The auxiliary table runs at its own tick rate.

**Implementation**:
- Add to `TableState`: `int auxTable = -1; int auxRow = 0; int auxTickCount = 0`.
- On parseFX: `m_tableState[t].auxTable = (val < NUM_TABLES) ? val : -1`.
- In doTick: tick the auxiliary table independently.

---

## Part J — Mixer/FX send commands

These commands directly modify `EngineState.mixer` and `EngineState.effects`.

| Cmd | Target field | Value mapping |
|-----|-------------|---------------|
| VMV XX | `mixer.out_vol` | val * (255/0xFF) |
| VMX XX | `mixer.cho_vol` | val * (255/0xFF) |
| VDE XX | `mixer.del_vol` | val * (255/0xFF) |
| VRE XX | `mixer.rev_vol` | val * (255/0xFF) |
| VT1 XX | `mixer.track_vol[0]` | val * (255/0xFF) |
| VT2 XX | `mixer.track_vol[1]` | val * (255/0xFF) |
| ... | ... | ... |
| VT8 XX | `mixer.track_vol[7]` | val * (255/0xFF) |
| XMT XX | `effects.cho_mod_type` | val |
| XMM XX | `effects.cho_mod_depth` | val |
| XMF XX | `effects.cho_mod_freq` | val |
| XMW XX | `effects.cho_width` | val |
| XMR XX | `effects.cho_reverb` | val |
| XDT XY | `effects.del_time_l`=X\*16, `del_time_r`=Y\*16 | split byte |
| XDF XX | `effects.del_feedback` | val |
| XDW XX | `effects.del_width` | val |
| XDR XX | `effects.del_reverb` | val |
| XRS XX | `effects.rev_size` | val |
| XRD XX | `effects.rev_decay` | val |
| XRM XX | `effects.rev_mod_depth` | val |
| XRF XX | `effects.rev_mod_freq` | val |
| XRW XX | `effects.rev_width` | val |
| XRZ XX | `effects.rev_freeze` | val > 0 |
| EQM XX | mixer EQ slot | val |
| EQI XX | instrument EQ slot | val |
| DJC XX | `mixer.djf_freq` | val |
| DJR XX | `mixer.djf_res` | val |
| DJT XX | `mixer.djf_typ` | val |
| IVO XX | `mixer.in_vol` | val |
| IMX XX | `mixer.in_cho` | val |
| IDE XX | `mixer.in_del` | val |
| IRV XX | `mixer.in_rev` | val |
| IV2 XX | line input 2 volume | val |
| IM2 XX | line input 2 ModFX | val |
| ID2 XX | line input 2 delay | val |
| IR2 XX | line input 2 reverb | val |
| USB XX | `mixer.usb_vol` | val |

All mixer/FX commands are **absolute** (not relative).

**Implementation**: In `tickTrack` parseFX, after the existing command dispatch,
add a fallthrough for these commands that directly writes to the mixer/effects
struct.

---

## Part K — File I/O

### libFxToEngine mapping

```
Library  Engine
0xFF     NONE
0x00     VOL
0x01     PIT
0x02     DEL
0x03     REV
0x04     HOP
0x05     KIL
0x06     TBL
0x07     GRV
0x08     TIC
0x09     ARP
0x0A     ARC
0x0B     CHA
0x0C     RND
0x0D     RNL
0x0E     RET
0x0F     REP
0x10     RTO
0x11     RMX
0x12     NTH
0x13     PSL
0x14     PBN
0x15     PVB
0x16     PVX
0x17     SCA
0x18     SCG
0x19     SNG
0x1A     SED
0x1B     THO
0x1C     TBX
0x1D     TPO
0x1E     TSP
0x1F     NXT
0x20     OFF
0x21     MTT
0x22     INS
0x23     GGR
```

### engineFxToLib mapping

Reverse of the above. `v == 0` (NONE) maps to `0xFF`. All others map to
`libIndex = engineValue - 1` for the first 6, then sequential for the rest.

### Mixer/FX send commands

These use a separate command namespace in the .m8s file format (they are
channel-type commands, not phrase FX). For now, store them as-is in the FX
column with the appropriate library index.

---

## Part L — Tests

### Test naming convention

Tests are tagged `[fx]` and named `F{N}.{M}` where N is the command group
and M is the sub-test number.

### Test infrastructure

All tests use `OfflineHost` with:
- `INST_MACROSYN` (no sample needed)
- `dry=0xC0, pan=0x80` (reasonable output)
- `tbl_tic=0x01` (fast table ticks when needed)
- Render 2000-5000 frames per test

### Command tests

#### F1: VOL at phrase level
```
Setup: phrase row 0 = note C4 + VOL 0x40 (relative)
Expected: volume is offset from instrument default
Assert: peak differs from no-VOL baseline
```

#### F2: PIT at phrase level
```
Setup: phrase row 0 = note C4 + PIT 0x0C (+12 semitones)
Expected: pitch is one octave higher
Assert: sum(abs(audio)) differs from no-PIT baseline
```

#### F3: Relative value accumulation
```
Setup: row 0 = VOL 0x10, row 1 = VOL 0x10
Expected: row 0 volume = base + 0x10, row 1 volume = base + 0x20
Assert: audio peaks differ between rows
```

#### F4: Relative value subtraction
```
Setup: instrument volume default high, row 0 = VOL 0xFF (= -1)
Expected: volume decreases by 1
Assert: peak is slightly lower than baseline
```

#### F5: INS instrument change
```
Setup: row 0 = note C4 + INS 0x01 (instrument 1, different filter)
Expected: voice switches instrument mid-phrase
Assert: timbre changes (spectral difference)
```

#### F6: OFF note off with release
```
Setup: instrument with ADSR (rel > 0), row 0 = note C4 + OFF 0x02
Expected: note-off triggers after 2 ticks, voice enters release
Assert: audio decays over release time, not instantly silent
```

#### F7: TPO tempo change
```
Setup: row 0 = TPO 0x78 (120 BPM), row 4 = TPO 0xF0 (240 BPM)
Expected: tick rate doubles after row 4
Assert: time between ROW_ADVANCE events halves
```

#### F8: SNG song hop
```
Setup: SONG mode, row 0 = SNG +02, row 2 = note C4
Expected: song jumps to row 2, note plays
Assert: NOTE_ON event at expected sample time
```

#### F9: ARP arpeggio
```
Setup: row 0 = note C4 + ARP 0x37 (intervals +3, +7)
Expected: rapid cycling through C4, D#4, G4
Assert: frequency changes detectable in audio (3 distinct pitches)
```

#### F10: CHA chance
```
Setup: row 0 = note C4 + CHA 0x0F (1/16 probability)
Render 100 repetitions
Expected: ~6 note-on events out of 100
Assert: note count within 2-sigma of expected
```

#### F11: RET retrigger
```
Setup: row 0 = note C4 + RET 0x13 (retrig every 3 ticks, vol -1)
Expected: note retriggers 3 times with decreasing volume
Assert: multiple NOTE_ON events, peaks decreasing
```

#### F12: REP repeat
```
Setup: row 0 = VOL 0x10, row 1 = REP 0x05
Expected: row 2 gets VOL 0x15, row 3 gets VOL 0x1A
Assert: volume increases stepwise
```

#### F13: PSL portamento
```
Setup: row 0 = C4, row 1 = E4 + PSL 0x06
Expected: pitch slides from C4 to E4 over 6 ticks
Assert: smooth frequency transition (no discontinuity)
```

#### F14: PBN pitch bend
```
Setup: row 0 = C4 + PBN 0x40 (bend up)
Expected: pitch rises continuously
Assert: frequency increases monotonically
```

#### F15: PVB vibrato
```
Setup: row 0 = C4 + PVB 0x44 (speed 4, depth 4)
Expected: periodic pitch modulation
Assert: frequency oscillates around base
```

#### F16: RT safety — all commands
```
Setup: every FX command type active simultaneously
Assert: g_allocCount == 0 after 5000 frames
```

### Table-specific tests

#### F17: Table DEL delay
```
Setup: table row 0 = DEL 0x02, row 0 = vol 0 (silent)
Expected: table advancement delayed by 2 ticks
Assert: silence starts 2 ticks later than without DEL
```

#### F18: Table THO hop-all-columns
```
Setup: table with independent column rates (TIC per column)
Row 0: THO 0x05
Expected: all columns jump to row 5
Assert: modulation matches row 5's values
```

#### F19: TBX auxiliary table
```
Setup: TBX 0x01 on phrase, table 1 has vol 0 (silent)
Expected: auxiliary table plays alongside main table
Assert: output is silent (auxiliary table's vol 0 applies)
```

### File I/O tests

#### F20: Round-trip TBL/GRV/TIC
```
Setup: phrase with TBL, GRV, TIC FX commands
Save to .m8s, reload
Assert: FX commands preserved byte-identical
```

#### F21: Round-trip all new commands
```
Setup: phrase with one of each new FX command
Save to .m8s, reload
Assert: all FX commands preserved
```

---

## Implementation order

1. **Plumbing**: Extend FxCmd enum, fix UI cycle range (0-38), fix FxName(),
   fix libFxToEngine/engineFxToLib
2. **Phrase VOL/PIT**: Most-used commands, highest value
3. **INS + OFF**: Simple, high value
4. **TPO + TSP + SNG**: Simple state writes
5. **ARP + ARC**: Complex but self-contained
6. **RET**: Retrig with volume ramp
7. **REP + RTO**: Repeat system
8. **PSL + PBN + PVB + PVX**: Pitch modulation family
9. **CHA + RND + RNL + NTH + SED**: Probability/randomness
10. **RMX + GGR + NXT + TBX + THO**: Misc track commands
11. **SCA + SCG**: Scale quantize
12. **MTT**: Micro-time (sub-tick)
13. **Mixer/FX send commands**: Direct struct writes
14. **REV**: Reverse playback (placeholder)
15. **File I/O**: Update libFxToEngine for all new commands
