# UI Screen Spec

Read this before doing any work on a clone UI screen. It records decisions that
have already been made, so they are not re-argued or quietly reversed.

## What the clone is for

soft-mate is an M8 tracker you can use without the hardware. Target order is
Windows first, then Linux, then Android.

**A screen is done when you can use it to create and play songs.** That is the
only definition of done. It is not "matches the device."

## The grid is 40x30, and that is deliberate

| | cols x rows | pitch |
|---|---|---|
| Clone | 40 x 30 | 8 x 8 |
| Device | 40 x 24 | 8 x 10 |

Both are 320x240 pixels. The row counts differ because the pitch differs.

Consequences, all intended:

- Device row R does not correspond to clone row R. Text sits at different rows.
- The constant row offset seen when diffing PHRASE against the device is
  arithmetic, not a defect. Do not fix it. Do not propose pitch as its cause.
- Column positions may differ too. That is acceptable.

## How to use device captures

The hardware is an answer key for **content**:

- which parameters and labels a screen has
- what a value means, and its range
- what the device does when a value changes

It is **not** an answer key for:

- geometry — see the grid section above
- colour — the two sides use different themes and different numbers of colours
- cell counts — the device emits a cell for every column including blanks, the
  clone only emits cells it wrote. Comparing totals is meaningless; compare
  glyphs.

Tools: `m8_nav --ui-capture`, `m8_diffcheck --diff-capture`,
`tools/side_by_side.py`. The side-by-side text rendering has caught more real
bugs than the cell-by-cell diff has. Prefer it.

## Out of scope

Do not build these, and do not report them as gaps:

- oscilloscope / waveform view
- currently-playing-notes display
- piano keyboard overlay on the minimap

**Mixer meters came off this list on 2026-08-12** and are now being built — see
`specs/MIXER_SPEC.md`. The approach this document always specified turned out to
be the right one and is what the spec follows: custom glyphs appended to the font
table in `src/ui/font.h` (`struct Font { char letter; char code[7][6]; }`), seven
partial-fill levels plus blank, stacked and coloured per cell. They are
characters, not graphics.

The other three stay out. The scope and the playing-note list were considered and
deferred in the same session, not forgotten: both need live audio data crossing
from the engine to the UI, which the meters are building anyway, so they get
cheaper once meters land. Revisit then.

MIXER *parity* stays parked — see `hw_findings.md` §UI-4. Rebuilding the mixer to
be usable is not the same thing as matching the device's geometry, and the
rebuild does not reopen §UI-3a/§UI-3b.

## Architecture: add to it, don't replace it

Every screen has a `*ScreenLayout.h` beside it holding `UI_GridCell` tables:

```cpp
struct UI_GridCell {
    std::string text;
    int col, row;
    std::string normal_color, selected_color;
    std::string role;
    bool has_cursor_box;
    int width;
};
```

returned from `GetStaticText()`, `GetDynamicTextDefaults()` and
`Get*InteractiveFields()`. The render function iterates the table and calls
`renderer.drawString()`. Input handlers live in the screen's `.cpp` and take a
reference to the data they mutate.

This is already a data-driven layout system. Add tables to it. Do not replace
the mechanism.

## Known gaps — real work

- ~~No layout exists for FMSYNTH, WAVSYNTH or HYPERSYN.~~ **CLOSED
  (verified 2026-08-18).** All three exist —
  `InstrumentFmsynthLayout.h`, `InstrumentWavsynthLayout.h`,
  `InstrumentHypersynLayout.h`.
- ~~HYPERSYN has no edit path at all.~~ **CLOSED (verified 2026-08-18).** The
  `HYP_*` cursor ids and their layout bindings are in
  `InstrumentHypersynLayout.h` and dispatched from `InstrumentScreen.cpp`.
  *(This entry twice described a gap that had already been filled — first
  claiming `Engine.h` had no `HyperState`, corrected 2026-08-12 to claim the
  ParamID/edit path was missing, which by then it was not either. Both readings
  were checked against the tree before closing it.)*
- **HYPERSYN draws an AMP control the device does not have.** Measured
  2026-08-18 (fw 6.5.2, COM3, `m8_nav --ui-capture`, `artifacts/inst_HYPERSYN.json`):
  on hardware the HyperSynth instrument screen's right-hand column starts at
  `LIM`. Row 13 carries `SHIFT` on the left and **nothing** at column 18, where
  every other instrument type puts `AMP`. The clone binds `C::AMP` at row 10
  (`InstrumentHypersynLayout.h:36`).

  This is a content difference, which per "How to use device captures" above is
  the thing the hardware *is* the answer key for — unlike geometry or colour. But
  it is not a one-line deletion: `HyperState::amp` is real, loads and saves
  through `SongIO`, and is applied in `SynthVoice`'s HyperSynth branch, so
  removing the control leaves a field that is set by files and unreachable in the
  UI. Two readings and they need different fixes: either the M8 applies amp to
  HyperSynth without exposing it (keep our control, note the divergence), or
  HyperSynth genuinely has no amp stage (our engine has an extra gain the device
  lacks, and every HyperSynth patch renders differently). Settle by capturing
  hardware audio with the file's amp byte at two values and comparing. Do not
  delete the control until that is measured — silence and a 6 dB level error look
  the same in a diff.

- `HandleTableInput` takes no data reference — TABLE moves a cursor but cannot
  edit anything.

## Retired — do not reopen

- **The PHRASE row offset.** Grid difference, see above.
- **Cross-side colour comparison.** The device has more distinct colours than
  the clone, so index-based labelling reports differences that do not exist.
- **Cell-count comparison.** Blank-cell asymmetry, see above.
- **§UI-3a's "device draws full-width cyan separator bars."** Those were blank
  cells mislabelled by the palette-index bug fixed in 83a7a5f. All cells in
  that capture share one background id. There are no bars.
