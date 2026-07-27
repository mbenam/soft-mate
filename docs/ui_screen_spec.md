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
- mixer VU meters and level bars

Meters, if they ever happen, are custom glyphs appended to the font table in
`src/ui/font.h` (`struct Font { char letter; char code[7][6]; }`). They are
characters, not graphics, and they are last in line.

MIXER parity is parked — see `hw_findings.md` §UI-4. Do not open it.

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

- No layout exists for FMSYNTH, WAVSYNTH or HYPERSYN. Only
  `InstrumentSamplerLayout.h` and `InstrumentMacrosynLayout.h` exist.
- `Engine.h` has no `HypersynState` parameter struct, though `SynthVoice.cpp`
  has a HYPERSYN branch.
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
