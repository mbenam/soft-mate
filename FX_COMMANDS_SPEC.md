# FX Commands Implementation Spec

This spec covers every M8 FX command: what exists today, what's missing, and the
exact behavioral contract for each. Engine-only (no UI wiring here).

---

## ⚠️ Verification status — read before implementing anything below

This document mixes three confidence levels now, and they must not be treated the same:

- **Tier 1 — verified / implemented.** The FX enum, the relative/absolute value model
  (Part A), the phrase engine for DEL/KIL/HOP, the table engine for VOL/PIT/HOP/TIC, and
  the file-I/O mapping (Part K, now round-trip-tested by `L12`/`L13`). These describe code
  that exists and passes tests. Trust them.

- **Tier 1.5 — manual-confirmed (2026-07-18), not yet implemented or tested.** We have the
  official M8 operation manual (`manual/m8_operation_manual_v20260421.pdf`) and cross-referenced
  every command below against its "Sequencer FX Commands" / "Mixer & Effects Commands" / "Instrument
  FX Commands" appendix. **Most of what was Tier 2 is now manual-confirmed at the semantic
  level** — what each command does, which nibble means what, the general shape of the math. This
  is real confirmation (an official, non-guessed source), but it is **not the same as a hardware
  capture** — the manual is occasionally imprecise on exact curves/formulas (see the per-command
  notes below, each one marked "Manual:" with the actual quoted text and "Still unconfirmed:"
  where a real gap remains). Treat a Tier 1.5 mark as "safe to implement from," not as "hardware
  round-tripped" — still worth a spot-check against a real device before calling it done, per
  bucket 1/2 of the "Next step" section below, but no longer a research checklist item.

- **Tier 2 — genuinely still unconfirmed.** A short, specific list, not "everything from Part D
  on" anymore: TPO's exact hex→BPM table (manual explicitly punts to on-screen help text, not
  documented statically), SCA/SCG's exact key-number mapping (manual confirms "X=key" but not
  which number is which key), ARC's exact mode-number encoding (manual confirms "X=mode" but not
  the 0/1/2/3 assignment), NTH's exact skip-count formula (manual explicitly punts to on-screen
  help text), and CHA's exact probability curve (the manual's own example, "CHA1F" ≈ "~10%
  chance", doesn't cleanly match a simple `X/15` linear model — see Part E). Also: the entire
  newly-discovered "Instrument FX Commands" family (FIN + the EA/AT/HO/DE/ET/LA/LF/LT envelope/LFO
  set, see the new Part D.5) has manual-level semantic descriptions but **no library FX byte
  assigned in Part K** — that mapping is not in the manual and needs either `m8-files-cxx` source
  inspection or a device capture of the raw bytes before it can round-trip at all.

**Project rule (AGENTS.md §4 / `status.md`): do not implement guessed hardware behavior.**
The REPITCH/BPM sampler modes were deliberately deferred for exactly this reason. The bar for
Tier 2 above is unchanged: confirm via capture or device before building. **Tier 1.5 has cleared
that bar for its stated scope** (semantic behavior) — implement from it, but don't assume it's
pixel/byte-perfect until spot-checked on real hardware. Until each command is actually
implemented, it is correctly *preserved* on load/save (as `FxCmd::UNKNOWN`) and stays inert.

Suggested safe order: land the Tier-1 fixes (already done) and the Tier-1.5 mechanical
commands first (VOL/PIT/INS/OFF/TPO's state-write mechanism\*/TSP/SNG, the Part J mixer sends,
RET, REP/RTO, the pitch-mod family, RMX/GGR/NXT/TBX/THO); leave the still-genuinely-unconfirmed
Tier-2 list (TPO's hex table, SCA/SCG's key numbering, ARC's mode numbers, NTH's formula, CHA's
exact curve, the new instrument-FX family's byte assignments) gated behind a capture or a closer
manual read. \*TPO's *mechanism* (set BPM directly) is confirmed; only the hex→BPM *value
table* is not.

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
| TBL | yes | -- | yes (lib 0x06) | yes |
| GRV | yes | -- | yes (lib 0x07) | yes |
| TIC | -- | yes | yes (lib 0x08) | yes |

**File-I/O round-trip fixed (2026-07-17).** `libFxToEngine`/`engineFxToLib` previously
dropped every command byte `>= 0x06` to `NONE` on load and clobbered it to `0xFF` on save.
Now: `0x00..0x08` decode to VOL..TIC (TBL/GRV/TIC round-trip correctly and are UI-selectable),
and any byte `>= 0x09` decodes to `FxCmd::UNKNOWN` and is **preserved byte-for-byte** on save
(the phrase save loop leaves UNKNOWN slots as the re-parsed original bytes — invariant #8).
Regression tests: `L12` (TBL/GRV/TIC), `L13` (unmodeled preserved). See `SongIO.cpp`.

### What's missing

All commands below are absent from the codebase (they load as `FxCmd::UNKNOWN`, display as
`???`, are inert at tick time, and are preserved on save — they are NOT executed): ARP, ARC,
CHA, RND, RNL, RET, REP, RTO, RMX, NTH, PSL, PBN, PVB, PVX, SCA, SCG, SNG, SED, THO, TBX, TPO,
TSP, NXT, OFF, MTT, INS, GGR, and all mixer/FX send commands.

**Also missing, and not previously in this list at all (found 2026-07-18 via the manual's
"Instrument FX Commands" appendix — see the new Part D.5):** FIN (fine tune), EA1/EA2 (envelope
amount), AT1/AT2 (envelope attack), HO1/HO2 (envelope hold), DE1/DE2 (envelope decay), ET1/ET2
(envelope retrigger), LA1/LA2 (LFO trigger amount), LF1/LF2 (LFO frequency), LT1/LT2 (LFO
retrigger). This spec's command inventory was itself incomplete before this — these aren't just
unimplemented, they were undocumented here entirely.

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

**Manual**: "Sets/changes the instrument number as a FX command." — confirms the concept but is
terser than this spec's own description; the "takes effect immediately, unlike the I column"
distinction is this spec's own reasonable elaboration, not manual-sourced but consistent with
every other FX command's own documented immediacy.

**Behavior**: Changes the instrument for the current track mid-phrase without
a note column entry. Takes effect immediately (unlike the I column which only
applies on note-on).

**Implementation**:
- On parseFX: `m_trackInstrument[t] = val`. Update voice's instrument pointer
  via `m_voices[t].setInstrument()`.

---

### OFF XX — Note off (ADSR release)

**Value**: ticks until release triggers (0 = immediate).

**Manual**: "Stops the currently playing instrument after a given number of ticks (XX). If there
is an ADSR envelope configured in the Instrument Modulation view, OFF will trigger the release
stage of the envelope." — confirms exactly, including the important detail this spec already
had: it only triggers a *release* stage if an ADSR modulation is actually configured (otherwise,
implicitly, behaves like a hard stop — consistent with "stops the currently playing instrument").

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

**Manual**: "Sets the play position for the given track to a relative song row from it's current
position. If the given song position is not playable the command is ignored." — confirms this
spec's description exactly, essentially word for word.

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

**Manual**: "Set the song tempo in BPM. Refer to the help text at the bottom of the screen to
translate the hex value to decimal." — confirms the *mechanism* (directly sets tempo) but
explicitly withholds the exact hex→BPM table, deferring to on-device runtime help text.

**Still unconfirmed, checked and genuinely absent from the manual** (not just missed): searched
both the FX appendix and the Project View's own TEMPO field documentation (which describes
tap-tempo / direct BPM entry on-screen, not this FX command's byte encoding) — no static table
exists in the manual. **This needs an actual device session**: navigate to a TPO field, watch the
on-screen help text at a few different hex values, or just test whether `val` maps 1:1 to BPM
(0x00-0xFF -> 0-255 BPM, this spec's current guess) by comparing tick timing before/after a TPO
command on a real device.

**Behavior**: Sets the song tempo. Takes effect immediately.

**Implementation**:
- On parseFX: `m_state.bpm = val; m_state.bpm_frac = 0; recalcBPM();`.

---

### TSP XX — Global transpose

**Value**: signed transpose (0x80 = 0, 0x00 = -128, 0xFF = +127).

**Manual**: "Transposes the entire song - Identical to the transpose value in the Project View."
— confirms the effect (global, all tracks) but doesn't confirm the exact signed-byte encoding
below (`0x80=0` center-point) — a reasonable, low-risk guess consistent with how other signed
bytes in this spec work, not manual-sourced for this specific command.

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

## Part B.5 — Instrument FX commands (newly discovered 2026-07-18, not previously in this spec)

**This entire section is new.** It was not in this document before the manual cross-reference
pass — the command inventory itself was incomplete, not just under-implemented. Source: manual
p.75, "Instrument FX Commands." The manual's own framing: "Almost all parameters for a given
instrument type have an FX command associated to it. Check the FX command help view with the
desired instrument in use to see the full list. Note that highlighting an instrument parameter in
the instrument view and navigating back to the phrase or table will set the default FX command to
the selected parameter." — this implies the *full* list of instrument-parameter FX commands is
much larger than what's enumerated below and is **type-specific** (different per Wavsynth/
Macrosynth/Sampler/FMSynth/Hypersynth) — the manual's printed appendix only lists the commands
"common" across types. Treat the list below as a known-incomplete floor, not a ceiling — a real
device's on-screen "FX command help view" (`[EDIT]+[UP or DOWN]` on a command column, per the
manual) is the actual complete reference, not this document.

### FIN XX — Fine tune

**Manual**: "Offset the note pitch from -1 to +1 semitones." — a finer-grained sibling to PIT
(Part B), same relative-offset model (Part A) presumably applies, narrower range.

### EA1/EA2 XX — Envelope amount (offset)

**Manual**: "Offset the Envelope amount." Two variants (EA1, EA2) — one per modulation slot,
consistent with the Instrument Modulation view's "M8 has 4 configurable modulation slots per
instrument" (manual, Instrument Modulation View section) — though only 2 of the 4 slots appear
to have dedicated FX commands per this list; the other 2 may use a different command or may not
be FX-addressable at all. Needs the on-device help view (see above) to confirm slot coverage.

### AT1/AT2 XX — Envelope attack (offset)

**Manual**: "Offset the Envelope attack time."

### HO1/HO2 XX — Envelope hold (offset)

**Manual**: "Offset the Envelope hold time."

### DE1/DE2 XX — Envelope decay (offset)

**Manual**: "Offset the Envelope decay time."

### ET1/ET2 XX — Envelope retrigger

**Manual**: "Retrigger the Envelope. Any value (XX) greater than '00' will retrigger the
envelope." — **not a relative offset** like the others above; a threshold-trigger command (this
one is a clean, complete, manual-sourced contract, no ambiguity).

### LA1/LA2 XX — LFO amount (offset)

**Manual**: "Offset the LFO amount."

### LF1/LF2 XX — LFO frequency (offset)

**Manual**: "Offset the LFO frequency."

### LT1/LT2 XX — LFO retrigger

**Manual**: "Retrigger the LFO. The value (XX) sets the desired phase offset (start position) of
the LFO." — also a clean, complete contract: unlike ET (pure trigger), LT's value has meaning
(phase offset), not just a >0 threshold.

### Implementation status

**None of this family is implemented, and none has a library FX byte assigned in Part K** — the
manual doesn't document raw byte values (it's a user manual, not a file-format reference), so
assigning these library bytes needs either `m8-files-cxx` source inspection or a device capture
of the raw FX bytes when one of these commands is placed and saved. Do not guess byte offsets by
extrapolating Part K's existing table (e.g. assuming FIN follows GGR at `0x24`) — that pattern
isn't confirmed and these are architecturally different (per-instrument-type commands, not global
sequencer commands), so they may not even be sequential with the rest of the enum in the actual
file format.

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

**Manual** (2026-07-18 cross-reference): "Hop/jump to a specific table position (0X). This
command hops all columns when used inside a table." — confirms the "hops all columns" behavior
exactly matching this spec's description.

**Behavior**: Like HOP but affects all columns. Used when the table has
independent column tick rates (TIC per column).

**Implementation**: Same as HOP but applies to all three column tick counters,
not just the current column.

---

## Part D — Arpeggio

> **Tier 1.5 (manual-confirmed) begins here**, through Part J, except where a section says
> otherwise. Source: `manual/m8_operation_manual_v20260421.pdf`, "Sequencer FX Commands" /
> "Mixer & Effects Commands" appendix (pp. 69-74), cross-referenced 2026-07-18. Implement from
> these contracts; spot-check against real hardware before calling any of it fully done (see
> "Next step beyond this spec"). Do not skip straight past a section's own "Still unconfirmed"
> note if it has one.

### ARP XY — Arpeggio

**Value**: X = first interval (semitones), Y = second interval (semitones).

**Manual**: "Produces a rapid 3 note arpeggio. The currently playing note is the base note with
X representing the first note interval in semitones, and Y as the second. Ex: 'ARP37' on note
C-4 plays C-4, D#4 (+3), and G-4 (+7)." — **matches this spec's prior hypothesis exactly.**
Confirmed, including the semitone interpretation and 3-note cycle.

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

**Value**: X = mode, Y = speed in ticks.

**Manual**: "ARP Configuration. Set the APR mode (X) and speed in ticks (Y)." — confirms X=mode,
Y=speed-in-ticks (matches this spec's Y interpretation exactly), but does **not** enumerate the
mode values.

**Still unconfirmed**: the exact 0/1/2/3 = up/down/up-down/random assignment below is this
spec's own guess, not manual-sourced — the manual names no modes at all. Needs a device
capture (author ARC00 vs ARC01 vs ARC02 vs ARC03 on an arpeggiating note, listen/capture the
direction) before trusting the numbering.

**Behavior**: Configures the arpeggio cycling mode and rate.

**Implementation**:
- Add to `EngineState`: `int arpMode[8] = {0}; int arpSpeed[8] = {1}`.
- On parseFX: `arpMode[t] = X; arpSpeed[t] = max(Y, 1)`.
- ARP speed divides the tick counter: `if (tickCount % arpSpeed == 0) cycle`.
- Mode values (0 = up, 1 = down, 2 = up-down, 3 = random) are **unconfirmed** — see above.

---

## Part E — Probability and randomness

### CHA XY — Chance

**Value**: X = left probability (0=never, F=always), Y = right probability.

**Manual**: "In a phrase: Individually set the probability to the left (X) or right (Y) side of
the command. The value range is from 0 (never) to F (always). Ex: 'CHA1F' will give the note a
~10% chance of triggering, and all other FX commands 100%. In a table: Set the probability for
everything to the left that 'CHA' is on from 00 (never) to FF (always)." — confirms the general
shape (0=never, F=always, independent left/right) and the phrase-vs-table split this spec
already had (per-slot in phrase, single value in table).

**Still unconfirmed**: the manual's own worked example doesn't cleanly confirm `X/15` as the
exact curve. `CHA1F`'s left digit is X=1; `1/15 ≈ 6.7%`, which the manual calls "~10%" — either
the manual is rounding loosely, or the true curve isn't linear `X/15` (e.g. could be `(X+1)/16 =
12.5%`, also only roughly "~10%"). This is exactly the kind of soft edge a device capture would
settle: author `CHA0F`/`CHA1F`/`CHA2F`/etc., render 100+ reps each, measure the actual trigger
rate, fit the curve. Don't trust `X/15` as byte-exact until that's done — but it's the right
shape to implement from in the meantime (worst case it's off by a few percentage points, not
structurally wrong).

**Implementation**:
- On parseFX: for each FX slot to the left of CHA in the same row, roll a
  random number. If roll > (X/15.0), clear that FX slot to NONE for this tick.
- Use `xorshift32` seeded per-row (deterministic per the LFO RANDOM pattern).

### RND XY — Randomize previous FX

**Value**: X = left range, Y = right range (0=none, F=full range).

**Manual**: "Randomizes the previously active FX command in a phrase/table. Independently control
the range's left (X) and right (Y) values from 0 (no randomness) to F (full range)." — confirms
this spec's model: targets the *previously active* command, independent left/right ranges,
0=none to F=full. No numeric range formula given beyond "full range" at F — the `[-X..+X]`
symmetric-range implementation below is this spec's own reasonable interpretation, not manual-
sourced, but low-risk (the failure mode of a slightly-wrong range is cosmetic, not structural).

**Behavior**: Randomizes the previously active FX command. The previous FX
command's value is offset by a random amount within the specified range.

**Implementation**:
- On parseFX: find the most recent non-NONE FX slot before RND in the same
  row. Generate random offset in [-X..+X] (left nibble) and [-Y..+Y] (right
  nibble). Apply to that FX slot's value.

### RNL XY — Randomize left command

**Value**: X = left range, Y = right range.

**Manual**: "Randomizes the FX command to the left in a phrase/table. Independently control the
range's left (X) and right (Y) values from 0 (no randomness) to F (full range). In the first
column RNL will randomize the note and instrument number in phrase, **or note and velocity in
table**." — confirms this spec's phrase-mode description, and adds a detail this spec was
missing entirely: the **table-mode first-column variant randomizes note+velocity, not
note+instrument** (phrase and table differ here). Add that distinction to the implementation.

**Behavior**: Like RND but specifically randomizes the FX command to the left.
In the first column: phrase mode randomizes note and instrument number; **table mode
randomizes note and velocity** (manual-confirmed distinction, was previously undocumented here).

### NTH XY — Nth trigger

**Value**: X = skip-left count, Y = skip-right count.

**Manual**: "NTH is a conditional trigger based on loop count. Skips either everything to the
left (X) or right (Y) side of the FX command in phrase, or everything to the left (XX) in table.
Skipping is determined by the value and how many times the phrase or table has looped. **Refer to
the help text at the bottom of the screen when editing the value.**" — confirms the general
mechanism (loop-count-based conditional skip, phrase uses X/Y independently, table uses XX as one
value) but explicitly punts the exact skip formula to on-device runtime help text.

**Still unconfirmed**: the exact `loopCount % (X+1) != 0` formula below is this spec's own guess
— the manual is explicit that it doesn't document this statically. This is one of the clearer
"needs a device session" items: navigate to an NTH field on a real M8 and read what the on-screen
help text actually says for a few different X values, or capture loop behavior directly.

**Implementation**:
- Add to `EngineState`: `int loopCount[8] = {0}`. Increment when phrase row
  wraps from 15 to 0.
- On parseFX: if `(loopCount[t] % (X+1)) != 0`, clear left FX slots. If
  `(loopCount[t] % (Y+1)) != 0`, clear right FX slots.
- **Unconfirmed** — see above.

### SED XX — Random seed

**Value**: seed byte.

**Manual**: "Set the random seed (XX) for the current track. This will reset all random values to
a specific state." — confirms the concept (deterministic reset) exactly as this spec already had
it.

**Note on scope**: the *exact* seed-expansion algorithm (how a single byte becomes internal RNG
state) is not something a manual would ever document, and — unlike CHA/NTH — it doesn't need
hardware confirmation either: SED's whole job is "make this track's randomness reproducible,"
and our engine's own RNG is an independent implementation from the M8's, so *our* hash choice
only needs to be internally deterministic, not bit-identical to the M8's. Not a Tier-2 item in
the same sense as the others above.

**Behavior**: Sets the random seed for the current track. Resets all random
values to a specific state.

**Implementation**:
- On parseFX: `m_rngState[t] = val * 2654435761u` (or similar seed
  expansion). All subsequent RND/CHA/RNL operations on this track use this
  seed.

---

## Part F — Repeat and retrigger commands

### RET XY — Retrig

**Value**: X = volume ramp direction/amount, Y = tick count. **This command had zero behavioral
spec anywhere in this document before 2026-07-18**, despite being referenced in Part K's file-I/O
mapping (`0x0E`) and test F11 — a real gap, not an oversight in this pass.

**Manual**: "Retrigger the current row with volume ramping at a given number of ticks. If Y is
not zero, Y sets the number of ticks to retrig while X changes the volume. An X value of 0 to 7
decreases and 8 to F increases the volume on each retrig. If Y is zero, RET is in single retrig
mode where X sets the number of ticks to wait." — this is a complete, precise, manual-sourced
contract: two distinct modes selected by whether Y is zero.

**Behavior**:
- **Y != 0 (repeating retrig mode)**: the row's note retriggers every Y ticks. Each retrig,
  volume ramps: X in `0x0-0x7` decreases volume (larger X within that range = presumably a
  bigger step, exact per-step magnitude within 0-7 not manual-specified — a genuine remaining
  gap, mild severity since the direction is confirmed), X in `0x8-0xF` increases volume.
- **Y == 0 (single retrig mode)**: one retrig only, after X ticks (X here is a tick count, not a
  volume direction — the manual explicitly separates these two interpretations of X by which
  mode is active).

**Still unconfirmed**: the exact volume-step-per-nibble-value magnitude within the 0-7/8-F
ranges (is it linear, is X=0 vs X=3 a bigger step than X=8 vs X=B — same open question as CHA's
curve). Confirmed: the mode-select-by-Y==0 branching and the direction split at the 0x8 boundary.

**Implementation**:
- Add to `EngineState`: `int retTicks[8] = {-1}; int retVolStep[8] = {0}; bool retSingleMode[8] =
  {false}; int retTickCounter[8] = {0}`.
- On parseFX: if `Y == 0`, set single-retrig mode with wait = X ticks. Else, set repeating mode
  with period = Y ticks and a volume step derived from X (sign by the 0x8 boundary; magnitude
  unconfirmed, start with a linear guess and revisit once measured).
- Each tick: if repeating mode and `tickCounter % Y == 0`, retrigger the note and apply the
  volume step (accumulating, similar to REP below). If single mode, retrigger once after X ticks
  then deactivate.
- Retriggering means a fresh `noteOn()` call at the row's note, not a new row — the row itself
  doesn't advance.

### REP XX — Repeat

**Value**: increment per step (0 = stop repeat).

**Manual**: "Repeat the last FX command, incrementing by a given amount per step (XX). REP will
continue to be active until a new command stops it. It does not need to be present on every
successive row. On new instrument triggers, the value will maintain it's position and continue
to repeat. To stop 'REP' either put a new command or 'REP00' in the FX column. **Note: A
double-caret '^^' will appear next to a FX lane when a REP is currently active from a previous
phrase.**" — confirms this spec's model exactly, and adds two details worth capturing:
1. REP explicitly **survives across rows without needing to be re-placed** (this spec's
   `repActive` flag design already handles this correctly — no change needed, just confirming).
2. REP explicitly **persists across new instrument triggers** ("maintain its position and
   continue to repeat") — this is a real behavioral detail worth double-checking against this
   spec's own Part A rule that note-on resets *relative* parameter offsets; REP is apparently an
   exception to that reset, per the manual's own wording. Flag this specifically when
   implementing VOL/PIT + REP interaction.
3. The `^^` UI indicator (display-only, no engine implication, but useful if/when UI wiring for
   REP happens later).

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
- **Do NOT clear `repActive` on note-on/instrument-trigger** — the manual is explicit that REP
  survives it. This differs from VOL/PIT's own note-on reset (Part B) — don't copy that reset
  logic here by reflex.

### RTO XX — Repeat to

**Value**: target value to stop at.

**Manual**: "Use after REP (Repeat) to set the min or max value that repeat should stop at." —
confirms this spec's description exactly, word for word in substance.

**Behavior**: Sets the min or max value that REP stops at. Used with REP to
create bounded repeat cycles.

---

## Part G — Pitch modulation

> All four commands in this part are manual-confirmed at the semantic level (see each). None
> have a fully-specified numeric curve (rate-per-tick, exact bend magnitude scaling) — treat
> the implementation sketches' specific formulas as reasonable-but-unconfirmed, same caveat as
> CHA/RET above, low severity (wrong magnitude, not wrong direction/shape).

### PSL XX — Pitch slide (portamento)

**Value**: slide time in ticks.

**Manual**: "Enables portamento for the currently playing instrument. The value (XX) is in
ticks." — confirms exactly.

**Behavior**: Enables portamento for the currently playing instrument. The
pitch slides from the previous note to the current note over XX ticks.

**Implementation**:
- Add to `SynthVoice`: `float m_portaTarget = 0.0f; int m_portaTime = 0; int m_portaTick = 0; float m_portaStart = 0.0f`.
- On note-on: `m_portaStart = m_frequency; m_portaTarget = newFreq; m_portaTime = val; m_portaTick = 0`.
- Each tick: `m_portaTick++`. In renderSample: `freq = lerp(m_portaStart, m_portaTarget, min(m_portaTick / m_portaTime, 1.0))`.

### PBN XX — Pitch bend

**Value**: 0x00-0x7F = bend up, 0x80-0xFF = bend down. Magnitude = |val - 0x80|.

**Manual**: "Enables a continuous pitch slide up (00-7F) or down (80-FF) by the given amount." —
confirms the direction split exactly (0x00-0x7F up, 0x80-0xFF down) matching this spec's prior
hypothesis precisely.

**Behavior**: Continuous pitch slide. The bend amount is applied per-tick as a
semitone offset that accumulates.

**Implementation**:
- Add to `EngineState`: `int pitchBend[8] = {0}`.
- On parseFX: `pitchBend[t] = (int8_t)val`.
- Each tick: accumulate pitch bend into the voice's pitch offset. The rate is
  proportional to the bend value.

### PVB XY — Vibrato

**Value**: X = speed, Y = depth.

**Manual**: "Apply vibrato to the currently playing instrument. Speed is set by X, and depth by
Y." — confirms exactly.

**Behavior**: Applies vibrato (LFO on pitch) to the currently playing note.
Speed X sets the LFO frequency; depth Y sets the modulation depth.

**Implementation**:
- Add to `SynthVoice`: `float m_vibPhase = 0.0f; int m_vibSpeed = 0; int m_vibDepth = 0`.
- On parseFX: set speed/depth. In renderSample: generate sine LFO at speed,
  apply `depth * sin(phase)` semitones to pitch.

### PVX XY — Extreme vibrato

**Value**: Same as PVB but with higher depth and rate range.

**Manual**: "Same as PVB (Vibrato) above with a high and more extreme depth and rate." — confirms
exactly, word for word in substance.

**Behavior**: Same as PVB with extended range.

---

## Part H — Track/scale commands

### SCA XY — Track scale

**Value**: X = key, Y = scale number (0-15).

**Manual**: "Sets the key signature (X) and scale number (Y) to use for the given track." —
confirms X=key, Y=scale-number, matching this spec's structure, but does **not** state the
key-to-number mapping.

**Still unconfirmed**: the `0=B, 1=C, ... 11=A#` numbering below is this spec's own guess. Checked
both the FX-command appendix entry and the manual's separate Scale View section (which explains
the *concept* of scales/keys in detail but never gives a numbered key table) — genuinely absent
from the manual, not just missed on a first pass. This one really does need a device capture:
author `SCA00`/`SCA10`/`SCA20`/etc. on a track and read which key the SCALE screen or note
quantization shows.

**Behavior**: Sets the key signature and scale for the given track. Notes are
quantized to the scale.

**Implementation**:
- Add to `EngineState`: `int trackScale[8] = {-1}; int trackKey[8] = {0}`.
- On parseFX: `trackScale[t] = Y; trackKey[t] = X`.
- In tickTrack: when computing freq from midi note, quantize to scale.
- Key numbering (`0=B` etc.) is **unconfirmed** — see above.

### SCG XY — Global scale

**Value**: X = key, Y = scale number.

**Manual**: "Sets the key signature (X) and scale number (Y) to use for the song." — confirms
exactly, same structure as SCA, same key-numbering caveat applies.

**Behavior**: Sets the key/scale for all tracks.

**Implementation**:
- On parseFX: `m_state.project.scale = Y; m_state.project.transpose = X` (or
  a dedicated scale fields).

---

## Part I — Remix and global commands

### RMX XY — Remix

**Value**: X = track mask, Y = phrase row position.

**Manual**: "Set the phrase play-head position (Y) of tracks to the left of the current track.
Select which tracks are affected using the first digit X." — confirms exactly, including which
digit is which (X=mask, Y=position) matching this spec precisely.

**Behavior**: Sets the phrase playhead position (Y) of tracks to the left of
the current track. Selects which tracks are affected using X.

**Implementation**:
- On parseFX: for each track `t2 < currentTrack` where `(X >> t2) & 1` is
  set: `m_state.playPhraseRow[t2] = Y`.

### GGR XX — Global groove

**Value**: groove index.

**Manual**: "Sets the groove number (XX) for all tracks." — confirms exactly.

**Behavior**: Sets the groove for all tracks (overrides per-track groove).

**Implementation**:
- On parseFX: `m_state.project.groove = val; for (int i=0; i<8; ++i) m_state.trackGroove[i] = -1`.

### NXT XX — Trigger instrument on next track

**Value**: instrument index.

**Manual**: "Plays an Instrument (XX) using the currently playing note and velocity on the track
to the right of the active track. Does not work on track 8." — confirms exactly, including the
track-8 edge case this spec already had.

**Behavior**: Plays instrument XX using the current note and velocity on the
track to the right. Does not work on track 8.

**Implementation**:
- On note-on: if `t < 7`, trigger `m_voices[t+1]` with the current note
  frequency and volume.

### TBX XX — Auxiliary table

**Value**: table index (0x00 = stop auxiliary table).

**Manual**: "Assign a table (XX) to play along with the current track (in parallel with any
Instrument tables). A '00' value will stop the auxiliary table. **If a TIC FX command is present
in the same row as a TBX, it will affect the auxiliary table's tick rate.**" — confirms this
spec's model, and adds a detail this spec was missing: a TIC on the *same row* as TBX targets the
**auxiliary** table's tick rate specifically, not the instrument's own table. Add that
disambiguation to the implementation (TIC's target — main table vs. aux table — depends on
whether a TBX shares its row).

**Behavior**: Assigns a table to play in parallel with any instrument tables.
The auxiliary table runs at its own tick rate.

**Implementation**:
- Add to `TableState`: `int auxTable = -1; int auxRow = 0; int auxTickCount = 0`.
- On parseFX: `m_tableState[t].auxTable = (val < NUM_TABLES) ? val : -1`.
- In doTick: tick the auxiliary table independently.
- If a TIC command shares the row with TBX, route it to `auxTickCount`'s rate instead of the
  main table's tick rate (manual-confirmed detail, not in the original implementation sketch).

---

## Part J — Mixer/FX send commands

**Manual-confirmed (2026-07-18)**: every command below is documented in the manual's "Mixer &
Effects Commands" appendix, each as a one-line "sets the value in the Mixer/Send Effects settings
view" description — confirming the whole table's shape (direct, absolute writes to the named
screen-visible fields) exactly. The manual doesn't give per-field numeric scaling beyond "sets
the value," so the `val * (255/0xFF)` conversions below (which reduce to identity, since `val` is
already 0-255) are this spec's own — harmless either way, but worth noting they're not adding
real scaling. One addition the manual has that this spec's table doesn't break out: **EQM/EQI's
"XX" is an EQ *slot number* to assign, not a value written into an EQ parameter directly** — the
manual: "Assign the main song EQ (EQM) or the current instrument (EQI) to a EQ slot," confirming
this spec's "mixer EQ slot" / "instrument EQ slot" description matches.

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

### Instrument FX commands (Part B.5) — no mapping exists yet

FIN, EA1/EA2, AT1/AT2, HO1/HO2, DE1/DE2, ET1/ET2, LA1/LA2, LF1/LF2, LT1/LT2 have **no library
byte assigned above** — this table stops at `0x23` (GGR) and was built before this family was
discovered (2026-07-18 manual cross-reference, see Part B.5). Determining their actual library
bytes needs `m8-files-cxx` source inspection or a device capture, not extrapolation from this
table's existing sequence — don't assume they're `0x24` onward.

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

> Steps 1 and the file-I/O half of step 15 are **done** (2026-07-17): the enum carries an
> `UNKNOWN` passthrough, `FxName()` renders TBL/GRV/TIC + `???`, the UI cycle reaches TIC,
> and `libFxToEngine`/`engineFxToLib` round-trip (tests `L12`/`L13`). Steps 2–4 are Tier-1-ish
> (well-understood semantics, TPO's hex→BPM table aside — see its Part B entry).
>
> **Updated 2026-07-18**: after the manual cross-reference pass, most of steps 5, 6, 7, 8, and
> 10 are now **Tier 1.5 (manual-confirmed)** — safe to implement from the contracts in Parts
> D/F/G/I, not a hard hardware-capture gate anymore. The remaining genuine gates are narrower:
> ARC's mode-number encoding (step 5), RET's exact volume-step magnitude (step 6), CHA's exact
> probability curve (step 9), SCA/SCG's key numbering (step 11), and TPO's hex table (step 4) —
> see each command's own "Still unconfirmed" note for what specifically to check before trusting
> the implementation byte-exact. NTH (step 9) and MTT (step 12) remain effectively ungated by
> Tier 1 vs Tier 2 distinctions but each has its own residual gap (see Parts E and B). **Step
> 16 (new, Part B.5) — the instrument-FX family — is a different kind of blocker: no library FX
> byte is assigned at all, so it can't even round-trip yet, independent of behavioral confidence.**

1. **Plumbing** *(done)*: FxCmd enum + `UNKNOWN` passthrough, FxName(), UI cycle range,
   libFxToEngine/engineFxToLib round-trip.
2. **Phrase VOL/PIT**: Most-used commands, highest value
3. **INS + OFF**: Simple, high value
4. **TPO + TSP + SNG**: Simple state writes (TPO's hex→BPM table still needs device confirmation)
5. **ARP + ARC**: Complex but self-contained (ARP fully confirmed; ARC's mode numbers don't)
6. **RET**: Retrig with volume ramp (mode-select + direction confirmed; step magnitude doesn't)
7. **REP + RTO**: Repeat system (fully manual-confirmed, including the note-on-persistence detail)
8. **PSL + PBN + PVB + PVX**: Pitch modulation family (fully manual-confirmed)
9. **CHA + RND + RNL + NTH + SED**: Probability/randomness (RND/RNL/SED confirmed; CHA's curve
   and NTH's formula don't)
10. **RMX + GGR + NXT + TBX + THO**: Misc track commands (fully manual-confirmed, including the
    TBX+TIC row-sharing interaction and RNL's table-mode note+velocity distinction)
11. **SCA + SCG**: Scale quantize (X/Y roles confirmed; key numbering doesn't)
12. **MTT**: Micro-time (sub-tick) — manual-confirmed conceptually, no numeric formula given
13. **Mixer/FX send commands**: Direct struct writes (fully manual-confirmed)
14. **REV**: Reverse playback (placeholder) — not covered by the manual cross-reference pass
15. **File I/O**: Update libFxToEngine for all new commands
16. **Instrument FX commands** (Part B.5, new): FIN + EA/AT/HO/DE/ET/LA/LF/LT — needs a library
    byte mapping worked out (source inspection or capture) before anything else in this step is
    possible; the manual gives semantic contracts but not the file-format encoding.

---

## Next step beyond this spec (not yet actionable — recorded so it isn't lost)

**Update 2026-07-18**: the manual cross-reference pass (see the Tier 1.5 marks throughout this
doc) confirmed most of what this section originally assumed would need hardware capture. The
device-verification scope is now much smaller and much more specific than "everything from Part
D on" — it's the short list of genuine residual gaps this pass actually found, plus one net-new
item (the instrument-FX family's byte mapping). Still not actionable until implementation lands;
this section still exists so the (now-shrunk) plan isn't lost between now and then.

**The genuinely still-open device-verification list, by command:**

| Command | What's unconfirmed | Verification shape |
|---|---|---|
| TPO | exact hex→BPM table (manual explicitly punts to on-device help text; checked the Project View section too — genuinely absent, not just missed) | screen-readable (watch the on-screen help text at a few values, or measure tick timing) |
| SCA / SCG | exact key-number mapping (`0=B` etc. — checked both the FX appendix and the Scale View section, genuinely absent) | screen-readable (author a few SCA values, read the resulting key) |
| ARC | exact mode-number encoding (0/1/2/3 = up/down/up-down/random) — manual confirms "X=mode" but never enumerates the modes | audio-observable (listen/measure arpeggio direction per mode value) |
| NTH | exact skip-count formula (manual explicitly punts to on-device help text) | screen-readable, likely (read the on-device help text directly) |
| CHA | exact probability curve — manual's own worked example (`CHA1F` ≈ "~10%") doesn't cleanly match a simple `X/15` linear model | probabilistic (N-trial capture, fit the curve) |
| RET | exact volume-step magnitude per nibble value within the 0-7/8-F ranges (direction and mode-select-by-Y are confirmed) | audio-observable (measure peak per retrig step) |
| Instrument FX family (FIN, EA/AT/HO/DE/ET/LA/LF/LT) | no library FX byte assigned at all yet — not a behavior question, a file-format question | needs `m8-files-cxx` source inspection OR a device capture of the raw saved bytes |

Everything else that was originally slated for hardware verification (ARP, PSL, PBN, PVB/PVX,
REP/RTO, RMX, GGR, NXT, TBX, THO, SCA/SCG's non-key-numbering behavior, the whole Part J
mixer/send family, INS, OFF, TPO's mechanism, TSP, SNG) is now Tier 1.5 — manual-confirmed at
the semantic level, safe to implement from directly. A device spot-check after implementing is
still good practice (per the verification banner), but it's confirmation, not discovery.

**Revised verification shapes, now much smaller in scope:**

1. **Screen-verifiable, no audio capture needed.** TPO, SCA/SCG, NTH likely — small table,
   reuses `M8_DEVICE_CONTROL_SPEC.md`'s Recipe 1 mechanism (`set`, `goto`, `assert_field`).
2. **Audio-observable, single-shot.** ARC (mode direction), RET (volume-step magnitude) —
   `m8_capture` + `m8_spectrum`/`m8_analyze`, bespoke per command.
3. **Probabilistic.** CHA only now (RND/RNL/NTH/SED dropped off this bucket — RND/RNL's ranges
   are manual-confirmed as "0=none to F=full" without needing a precise curve fit the way CHA's
   trigger probability does; SED needs no external confirmation at all, see Part E).
4. **File-format investigation, not hardware behavior verification.** The instrument-FX family's
   byte mapping — a different kind of work entirely (reverse-engineering `m8-files-cxx` or a
   saved file's bytes), doesn't fit the capture-based buckets above.

**Decide then, not now, whether this becomes its own spec or a recipe family added to
`M8_DEVICE_CONTROL_SPEC.md`'s Tier 5** — with the list this short, folding it into Tier 5 as a
small recipe (or two, given bucket 4's different nature) looks like an even clearer fit than it
did before this pass; revisit once there's an actual implementation to test against.
