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
- **The main mix EQ and the three effect EQs are not modeled at all.** There is no `master_eq`
  field, nothing EQ-shaped in `MixerSettings`, and the offsets table carries only `eq` (the
  instrument banks) and an optional `.m8i` equivalent. `EffectsSettings`' `delay_hp`/`delay_lp`/
  `reverb_hp`/`reverb_lp` are simple corner filters, not the 3-band EQ. Wherever those four EQs
  live in the file, we do not parse it — they survive today only because save-by-overlay
  preserves bytes it doesn't understand.

So building the mix EQ first means a control that either doesn't persist or persists to a
guessed offset. Instrument banks first; the other four wait on §6.

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

**Do not trust `EqModeType::eq_type()`.** It clamps anything above 5 to `Bell`
(`types.cpp:131`). If ALLPASS is type 6 — as §5 assumes — the accessor will silently report it
as a bell. Decode `mode.value & 0x7` ourselves. The raw byte *is* preserved on write
(`EqBand::write` writes `mode.value` untouched), so no data is lost either way; only the
interpretation is wrong.

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

**4b. ALLPASS — STILL OPEN.** No band anywhere in the tree uses a type other than 1, 2 or 4, so
there is no evidence for or against type 6. Implement it as 6 and treat it as an inference. The
risk is contained: the raw byte round-trips untouched either way, so a wrong guess mislabels a
band rather than corrupting a file.

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
2. [ ] **Load and save EQ banks.** `Song::eqs` ⇄ engine state, bank count from the song version.
       Byte-identical round-trip must still hold.
3. [ ] **Biquads.** Seven types, five stereo modes, coefficients on change only, reset on
       `LOAD_SONG`.
4. [ ] **Wire instrument EQ.** Per-voice, bank index from the instrument, applied in the voice
       chain.
5. [ ] **Curve glyphs.** Seven dash glyphs, plus the response evaluation.
6. [ ] **Editor screen.** Layout, navigation, the shortcuts listed above, entered from the
       Instrument and Instrument Pool screens.
7. [ ] **Bank assignment.** Make the existing `eq` field editable on both screens.
8. [ ] **Find where the main mix and effect EQs live in the file.** Write a known EQ on the
       device, save, and diff the bytes against a copy saved without it. Until this lands, those
       four EQs stay unbuilt — a control that doesn't persist is worse than no control.
9. [ ] **Fold findings into reference docs, then archive this spec.** Cannot be ticked while any
       step above is unticked (`AGENTS.md` §9).

---

## 8. Known limitations to carry forward

- ALLPASS as type 6 is an inference — no file in the tree uses any type but 1, 2 or 4. The raw
  byte round-trips untouched, so a wrong guess mislabels a band rather than corrupting a file.
- The main mix EQ and the three effect EQs are not built; their bytes are preserved blind, as
  they are today. Step 8 is what unblocks them.
- Curve resolution is 7 sub-positions per cell; steep slopes will read as dots.
- Q is stored as a plain byte and its mapping to an actual filter Q is not known. The values
  seen in real files (50, 90) and on the device (69) suggest a range, not a scale. Step 3 has
  to choose a mapping; pick something musical, document it as unverified, and expect to revise
  it if a capture ever contradicts it.
