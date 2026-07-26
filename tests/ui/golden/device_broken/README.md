# Broken Device UI Corpus

These 79 files were captured by `captureFromGrid()` with a col/row swap bug
(UI-1). The bug has been fixed; re-capture is required.

## The Bug

`ScreenGrid::cells` is keyed `(y, x)`, but `captureFromGrid` read it as
`(x, y)`. The two lines:

```
uc.col = pos.first / cap.pitchX;   // pos.first is Y, not X
uc.row = pos.second / cap.pitchY;  // pos.second is X, not Y
```

swapped the axes and divided each by the other axis's pitch.

## Why Transposing or Rescaling Cannot Salvage the Data

The division by the wrong pitch causes distinct source columns to collide
onto the same stored index. The M8 draws glyphs at x = 0, 8, 16, 24, ...
(i.e. x = 8n). The broken code computes `col = y / 10`, which for the MIXER
screen's 750 non-blank cells maps many different (x, y) positions onto the
same col value. The original positions are irrecoverable because multiple
source pixels map to the same output index — information is lost, not merely
permuted.

A post-hoc transpose would swap col and row but could not un-collide the
lost columns. Rescaling cannot help either: the mapping is many-to-one, not
a uniform scale error.

## Status

- **Task 1:** Bug fixed in `UiCapture.cpp` (corrected col/row extraction).
- **Task 2:** Regression test added in `test_device_decode.cpp`.
- **Task 5:** Re-capture required from real hardware.
