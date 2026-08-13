# EQ — Implementation Spec

A 3-band parametric EQ, its editor screen, and the bank system instruments use to share
settings. Decided 2026-08-13 from the M8 manual's EQ section, a photo of the EQ Editor view,
and the `.m8s` format as modeled by `m8-files-cxx`.

Deferred out of `archive/MIXER_SPEC.md`, which left an unwired "EQ" label on the mixer.

---

## 1. Order of work, and why it is not the obvious one

The M8 has EQ in five places: 128 instrument banks, the main mix, and one on each of ModFX,
Delay and Reverb. The first instinct is to do the **main mix** first — it is a single instance
and the mixer already has the label. That is wrong, and the file format is why:

- **Instrument EQ banks are fully modeled.** `Song::eqs` is a flat array of `Equ`, read at
  `Offsets::eq`, and each instrument's existing `eq` byte is an index into it. We already load,
  display and preserve that byte on both the Instrument and Instrument Pool screens.
- **The main mix EQ and the three effect EQs are not modeled by the library.** There is no
  `master_eq` field, nothing EQ-shaped in `MixerSettings`, and the offsets table carries only
  `eq` (the instrument banks) and an optional `.m8i` equivalent. `EffectsSettings`'
  `delay_hp`/`delay_lp`/`reverb_hp`/`reverb_lp` are simple corner filters, not the 3-band EQ.

That was the original reason to start with instrument banks: building the mix EQ first would
have meant a control that either didn't persist or persisted to a guessed offset.

**§4c has since located all four**, so they are no longer blocked — they sit immediately after
the instrument bank array and we can read and write them directly. The order stands anyway,
because it was never only about persistence: the editor screen, the curve and the biquads get
built once regardless, and the instrument path is the one whose bank-index plumbing already
exists. The other four become wiring once those parts work.

---

## 2. What the editor is

One screen editing one bank, reached from wherever that bank is referenced. Header shows which
bank ("EQ BANK 00"). Three bands, each with five parameters. The manual is explicit that
LOW/MID/HIGH are only default labels — the bands are functionally identical, so nothing stops
all three being high shelves.

| Parameter | Meaning |
|---|---|
| GAIN | Boost or cut in dB. Signed, two decimals on screen (`-05.00`). |
| FREQ | Centre or corner frequency in Hz (`1547`). |
| Q | Bandwidth. High is narrow, low is wide. |
| TYPE | LOWCUT, LOWSHELF, BELL, BANDPASS, HI.SHELF, HI.CUT, ALLPASS |
| MODE | STEREO, MID, SIDE, LEFT, RIGHT |

MODE is the one worth calling out: mid/side means you can EQ the centre of a mix without
touching the sides. It needs an M/S encode-decode around the filter, which is a few lines.

---

## 3. The file format, as established

Per band, 6 bytes; per bank, 18 (`Equ::V4_SIZE`):

```
mode_type   freq_fin   freq   level_fin   level   q
```

**Bank count is version-dependent and must not be hardcoded.** `V4_OFFSETS.instrument_eq_count`
is **32**; `V4_1_OFFSETS` is **128**. The manual's "128 assignable banks" describes current
firmware only. Read the count from the song's version, as the library does.

**The mode byte packs two fields.** `type = value & 0x7`, `mode = (value >> 5) & 0x7`. Bits 3-4
are unused as far as we can tell.

**Do not use `EqModeType::eq_type()`.** It clamps anything above 5 to `Bell`
(`types.cpp:131`), and ALLPASS is type 6 — confirmed on hardware, §4b — so an ALLPASS band read
through that accessor comes back as a bell. Decode `mode.value & 0x7` ourselves. The raw byte
*is* preserved on write (`EqBand::write` writes `mode.value` untouched), so the file is never
damaged; only the interpretation would be.

---

## 4. The encoding — settled 2026-08-13

Decoded with `tools/eq_dump.py` against every `.m8s` and `.m8i` in the tree. Reproduce with:

```
python tools/eq_dump.py third_party/m8-files-cxx/examples/songs/*.m8s
python tools/eq_dump.py third_party/m8-files-cxx/examples/instruments/*.m8i
```

**4a. Coarse/fine combination — CONFIRMED.** Both pairs are a 16-bit value, coarse byte high,
fine byte low: `(coarse << 8) | fine`. Frequency is Hz directly; gain is a **signed** value in
**hundredths of a dB**.

The proof is `FMDUBSTABEQ_4_1.m8i`, an instrument someone saved with a deliberately non-default
EQ:

```
LOW   LOWSHELF  100 Hz   +6.00 dB  Q 50    raw 01 64 00 58 02 32
MID   BELL     1000 Hz   +9.00 dB  Q 90    raw 02 E8 03 84 03 5A
HIGH  HI.SHELF 5000 Hz  -11.00 dB  Q 50    raw 04 88 13 B4 FB 32
```

Frequencies land exactly on 100 / 1000 / 5000. Gains decode to +6, +9 and −11 dB — musical
values at hundredths; the tenths reading would make them +60, +90 and −110 dB, which no EQ has.
The negative one also confirms two's complement: `0xFBB4` = −1100.

**Q is a plain byte**, displayed as-is (50 here, 90 on the bell; the device screenshot shows 69).

**The factory defaults**, identical across every song file in the tree, are worth keeping for
the EDIT+OPTION reset:

| Band | Type | Freq | Gain | Q | Mode |
|---|---|---|---|---|---|
| LOW | LOWSHELF (1) | 100 Hz | 0.00 dB | 50 | STEREO |
| MID | BELL (2) | 1000 Hz | 0.00 dB | 50 | STEREO |
| HIGH | HI.SHELF (4) | 5000 Hz | 0.00 dB | 50 | STEREO |

**4b. ALLPASS — CONFIRMED on hardware 2026-08-13.** Read directly off a real M8 (firmware
6.5.0) by cycling the TYPE field with EDIT+RIGHT and noting each value. Seven types, in this
order, so ALLPASS is index 6 as assumed:

```
0 LOWCUT   1 LOWSHELF   2 BELL   3 BANDPASS   4 HI.SHELF   5 HI.CUT   6 ALLPASS
```

MODE was read the same way and matches the manual exactly: five entries,
`0 STEREO  1 MID  2 SIDE  3 LEFT  4 RIGHT`.

Note this makes `EqModeType::eq_type()` actively wrong rather than merely suspect — it clamps 6
to Bell, so an ALLPASS band read through that accessor becomes a bell. Decode the raw byte.

**4c. Where the main mix and effect EQs live — FOUND 2026-08-13.** Established by saving the
same project twice on a real M8, identical but for the mix EQ, and diffing (§7 step 8's method,
executed early because the hardware was available). The two files differ in 17 bytes: one
18-byte EQ block, plus the project name and what appears to be a save counter.

**Four more EQ blocks sit immediately after the instrument bank array**, in the same 18-byte
format:

```
mix_eq_offset = Offsets::eq + instrument_eq_count * 18
```

which is `0x1AF9E` on a 32-bank V4 file and `0x1B65E` on a 128-bank one. Verified on V4EMPTY
(4.0.1), V4-1EMPTY (4.2.0) and a hardware save (6.5.0) — in every one, exactly 72 bytes of EQ
follow the banks.

| Block | Offset | Purpose | Default bands |
|---|---|---|---|
| 0 | `+0` | **Main mix** — confirmed by the diff | LOWSHELF 100 / BELL 1k / HI.SHELF 5k |
| 1 | `+18` | ModFX *(inferred)* | LOWCUT 100 / BELL 1k / HI.SHELF 5k |
| 2 | `+36` | Delay *(inferred)* | LOWCUT 500 / BELL 1k / HI.CUT 10k |
| 3 | `+54` | Reverb *(inferred)* | LOWCUT 200 / BELL 1k / HI.CUT 8.8k |

Block 0 is fact — it is the one that changed. The other three are an inference from their
defaults and from the order the Effects screen lists them: a send rolled off at 10 kHz and cut
below 500 Hz reads as a delay, one rolled off at 8.8 kHz as a reverb. Confirm the same way if
it matters — set a distinctive EQ on the delay from Effects Settings, save, diff.

The confirming values also re-prove §4a on a different EQ instance entirely: `+12.00 dB / 137 Hz
/ Q 3` came back as `01 89 00 B0 04 03`, where `0x0089` is 137 and `0x04B0` is 1200.

**Version reach.** The hardware save reports **6.5.0** and decodes cleanly at the V4.1 offsets,
so this layout has been stable from 4.1 through at least 6.5. It also carries **32 bytes past
the four EQ blocks** that 4.x files do not — fields added since, which we do not parse. They are
safe: `BinaryWriter` only ever grows its buffer and `finish()` returns the whole thing, so
anything past what the library writes survives a save untouched.

**Incidental confirmations.** The bank offset and version-dependent count are right — V4EMPTY
(4.0.1) decodes cleanly as 32 banks and V4-1EMPTY (4.2.0) as 128, both at `0x1AD5E`. Note that
"V4.1" files in this tree actually report **4.2.0**, so the `at_least(4, 1)` test is what
matters, not an equality check. TEST-FILE.m8s is 3.0.4 and has no EQ block at all, which is the
pre-4.0 path. For `.m8i`, the EQ sits at `0x165` at the very end of the file — an instrument
saved without one simply stops short (357 bytes against 375), which is why the library guards
on length before reading.

---

## 5. DSP

Three biquads in series per EQ instance. Standard RBJ cookbook forms cover six of the seven
types; ALLPASS is the seventh and is also a cookbook form.

- Coefficients recompute only when a parameter changes, not per sample.
- MODE wraps the filter: STEREO runs both channels through matched filters; LEFT/RIGHT filter
  one channel and pass the other; MID/SIDE encode to M/S (`m = (l+r)/2`, `s = (l-r)/2`), filter
  one of them, decode back.
- State resets where every other stateful DSP does — on `LOAD_SONG`, alongside the effects and
  master bus (`ARCHITECTURE.md` invariant 11), or the offline renderer diverges from the app.
- Nothing allocates on the audio thread. Filter state lives in members, sized at construction.
- **Instrument EQ runs per voice**, so 8 voices × 3 biquads. The bank index is read per note-on,
  not per sample.

---

## 6. Screen

Reached from the Instrument screen's EQ field and the Instrument Pool's EQ column. OPTION exits
back to where you came from (the manual says "returns to the Mixer View"; for us it returns to
the caller, since the mixer isn't the only door). Layout follows the device: the response curve
across the top, the parameter table below it in three columns.

**The curve is drawn with characters, not pixels.** Seven new glyphs, each a short dash at one
of the seven pixel rows in a cell, giving 7 sub-positions per row of graph — a 12-row graph has
84 vertical steps. For each column, evaluate the combined response in dB, map to a position,
draw one character. `drawLinePixel` is deliberately *not* used: it is a bare SDL call that
touches nothing in the shadow grid (`Renderer.cpp:183`), so anything drawn with it is invisible
to every dump, golden and assertion — the same blind spot that stops `playhead.m8script` from
proving the playhead moved. The curve is the point of this screen; it should not be the one
thing nothing can check.

Consequences to accept:
- Steep sections read as dots rather than a connected line. The device's own scope is a dotted
  trace, so this should look at home. If it doesn't, add a few two- and three-row segment
  glyphs before reaching for line drawing.
- Grid lines (50/100/200/500/1K/2K/5K/10K, and the 0 dB line) are also characters, and the
  curve will sometimes want the same cell. One character per cell: decide the cell's content
  first — curve wins over grid — then draw once. Same single-pass approach the mixer bars use,
  and what keeps `assert_no_overlap` meaningful.

Shortcuts from the manual, in scope: EDIT+arrows to edit (large steps vertical, small steps
horizontal), EDIT+OPTION to reset a parameter to default, EDIT double-tap to mute/unmute the
EQ, SHIFT+OPTION and SHIFT+EDIT to copy and paste a bank.

Out of scope: the touchscreen and MIDI-CC assignment shortcuts, which belong to a MIDI Mappings
view we don't have.

---

## 7. Steps

1. [x] **Settle the encoding offline.** Done 2026-08-13 — see §4. Frequency, gain and Q are
       confirmed; ALLPASS remains an inference. `tools/eq_dump.py` reproduces it.
2. [x] **Load and save EQ banks.** Done 2026-08-13. `Song::eqs` ⇄ `EngineState::eqs[128]`,
       taking however many the song carries. The packed type/mode byte is kept alongside the
       decoded fields and used as the base when writing back, so bits 3-4 survive untouched.
       Tests L16-L19 (`[io]`), the last of which diffs the EQ block byte-for-byte.
3. [x] **Biquads.** Done 2026-08-13 — `src/engine/EqFilter.h`. RBJ cookbook forms for all
       seven types, all five stereo modes, coefficients recomputed only when the bank changes,
       and a true bypass when every band is flat. Tests `EQ1`-`EQ10` (`[eq]`) measure the
       response rather than trusting it. The `LOAD_SONG` reset lands with step 4, when there is
       an instance to reset.
4. [x] **Wire instrument EQ.** Done 2026-08-13. One `EqProcessor` per *track* rather than
       per voice or per bank -- a track plays one instrument at a time, so eight cover all 128
       banks. Reconfigured once per `render()` call from `Instrument::getEq()`; applied to the
       track's dry stereo pair, the only point where a track has stereo at all. Reset on
       `LOAD_SONG` with the rest of the DSP. Tests EQ11-EQ13.
5. [x] **Curve glyphs.** Done 2026-08-13. Seven dashes at `0x08`-`0x0E` in `font.h`,
       mapped to `a`-`g` by `dumpScreenText` (0x0A raw would have split every dump into extra
       lines). Response comes from `EqBiquad::magnitudeAt` / `EqProcessor::responseDbAt`,
       evaluated analytically so drawing costs no audio.
6. [x] **Editor screen.** Done 2026-08-13 — `src/ui/screens/eq/`. Curve over a log axis
       (20 Hz-20 kHz, +/-18 dB), 5x3 parameter table, OPTION exits. Entered by an X tap on the
       Instrument screen's EQ field, and from the Instrument Pool's EQ column (step 7). Editing
       goes through new `EQ_*` ParamIDs so the mirror and the engine receive the same mutation.
7. [x] **Bank assignment.** Done 2026-08-13. Both screens already edited the `eq` byte but
       clamped it to 0-255 and read it with a sampler/macrosyn pick that ignored the other
       three instrument types; now they use `getEq()` and clamp to `EngineState::eqBankCount`,
       which `SongIO` sets from the song's own bank count -- assigning bank 100 on a 32-bank V4
       file would otherwise vanish on save. The pool's EQ column opens the editor rather than
       the instrument. Covered by IP6 and `tests/ui/eq_editor.m8script`, which drives the whole
       route through the app.
8. [ ] **Wire the main mix and effect EQs.** Their location is no longer unknown — see §4c;
       they are four 18-byte blocks at `Offsets::eq + instrument_eq_count * 18`. What remains is
       reading them into engine state, applying them (mix EQ into the master chain where the
       chain diagram already reserves a slot, the other three on their send returns), and making
       the mixer's EQ label a cursor stop that opens the editor. Confirm the ModFX/Delay/Reverb
       ordering first with one more device diff if it matters.
9. [ ] **Fold findings into reference docs, then archive this spec.** Cannot be ticked while any
       step above is unticked (`AGENTS.md` §9).

---

## 8. Known limitations to carry forward

- Which of the three effect EQ blocks is ModFX, Delay and Reverb is an inference from their
  default frequencies and the Effects screen's ordering (§4c). Block 0, the main mix, is
  confirmed. One more device save-and-diff would settle the rest.
- The main mix EQ and the three effect EQs are located but not yet built; their bytes are
  preserved untouched meanwhile.
- Files from firmware 6.5.0 carry 32 bytes past the four EQ blocks that 4.x files do not. We
  do not parse them. They survive a save because the writer only grows its buffer, but nothing
  tells us what they are.
- Curve resolution is 7 sub-positions per cell; steep slopes will read as dots.
- **Q maps logarithmically over 0-99** — measured 2026-08-13 from the device's own curve
  display, a bell at 1 kHz with +12 dB gain screenshotted at Q = 01, 10, 25, 50 and 99. The
  field caps at 99, so the byte never exceeds it. At 01 the curve is a flat +12 dB across the
  whole band (a bell so wide it boosts everything); at 25 a broad arch still lifted at both
  ends; at 50 a clean hill back to 0 dB by the edges; at 99 a narrow spike back to 0 dB by
  roughly 500 Hz and 2 kHz. That is about two decades of Q across the range, fitting:

  ```
  Q = 10 ^ ((byte - 50) / 50)      byte 0 -> 0.1,  50 -> 1.0,  99 -> ~9.8
  ```

  The default byte of 50 landing exactly on Q = 1.0 is what makes this credible rather than
  merely fitted. **Still an approximation** — read off screenshots by eye, not measured from
  audio. Validate it by drawing our own curve at those same five values and comparing shapes
  against the captures; refine with a swept measurement if it ever matters.
