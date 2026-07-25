# M8 Parameter Range Spec — Validating Written Values Against Hardware Limits

Every value `m8_makeprobe` writes into a `.m8s` must be inside the range the real M8
accepts. Today at least one was not: instrument volume was written as `0xE0` when the
hardware ceiling is `0x7F`, and the device treated it as `0x00`. That single
out-of-range byte is the probable cause of the silent-and-near-silent generated
probes.

This spec generalizes that fix. It is **not** about the sampler, and it is not about
volume. It is about establishing what every field's legal range is, checking every
value we write against it, and failing loudly instead of silently producing a file
the device misreads.

> **Companion specs.** `M8_DRIVER_SPEC.md` (serial/device work) and
> `M8_PROBE_AUTHORING_SPEC.md` (probe authoring and audio comparison). This spec is
> offline: no serial port except where marked `[SD]` or `[HW]`.

---

## Why the current fix is not enough

`main_makeprobe.cpp` now contains, five times over:

```cpp
int instVol = (instType == "sampler" && volume == 0xE0) ? 0x00 : volume;
```

Three problems:

1. **Four of the five copies are dead code.** The line sits at 215, 236, 250, 275,
   and 296, inside an `if / else if` chain on `instType`. `instType == "sampler"` can
   only ever be true at 236. MacroSynth, WavSynth, FMSynth, and HyperSynth still
   write `0xE0`. The duplication makes it *look* like all five are handled.
2. **It is gated on the exact value `0xE0`.** `--volume 0xC0` still writes an
   out-of-range value with no warning.
3. **It encodes a symptom, not a rule.** If there is a hard ceiling, that belongs at
   the boundary as one validated clamp — not a magic-value special case.

## The open question this spec answers first

`docs/tools/hw_findings.md` §P3 records the cause as instrument-type-specific: "for
MacroSynth `0xE0` is nominal default volume, but on hardware Sampler instruments
`0x00` represents 0 dB / full volume." The newer finding is a global range ceiling of
`0x7F`.

**These contradict each other and predict different amounts of remaining breakage.**
Under the P3 theory only the sampler ever needed changing and the tree is correct.
Under the range theory four instrument types are still wrong. Step R1 settles it, and
nothing else in this spec should be built until it does.

### Progress checklist

```
[x] R1  [SD] Capture the four missing device goldens
[x] R2  Extract the observed ranges into a table
[x] R3  ParamRange.h — single source of truth
[x] R4  Replace the five pasted checks with one validated clamp
[x] R5  Audit every remaining literal makeprobe writes
[x] R6  Reconcile ahd.hold against the device golden
[x] R7  Range-check the mixer fields
[ ] R8  Fix the clone's out-of-range defaults
[ ] R9  [HW] Re-verify amplitude parity across all five types
[ ] R10 Record the rule and retire the contradiction
```

Same working discipline as the other specs: one commit per step prefixed with the
step id, tick the checklist in this file in the same commit, re-read a step before
starting it, stop when a Verify fails.

---

### Step R1 `[SD]` — Capture the four missing device goldens

**Files:** `tests/fixtures/device_golden/`, append `## R1` to
`docs/tools/hw_findings.md`

`tests/fixtures/device_golden/` currently holds `Sampler.m8s` and nothing else. P1 in
the authoring spec called for one golden per instrument type; four are missing, which
is why the range question can't be answered from the repo.

**Change.** Author on the device, by hand, and save: `MacroSynth.m8s`,
`WavSynth.m8s`, `FMSynth.m8s`, `HyperSynth.m8s`. For each, set the volume parameter
**to its maximum via the device UI** — hold EDIT and turn it up until it stops
increasing — then save.

Record per type: the maximum volume value the UI would accept, the value actually
stored in the file, and whether they match.

**Why.** Turning the parameter to its ceiling on the device and reading back what got
stored is the direct measurement. It answers "what is the max" without inference, for
every type at once.

**Guardrail.** Author by hand. Do not load a generated probe and re-save it — that
launders our bytes through the device and destroys the comparison.

**Verify.** Five goldens in `tests/fixtures/device_golden/`, each confirmed audible
when reloaded on the device.

---

### Step R2 — Extract the observed ranges

**Files:** `tools/m8s_diff.py` (extend), append `## R2` to `hw_findings.md`

**Change.** Add a mode to `m8s_diff.py` that dumps the named fields of a `.m8s` rather
than diffing two. Run it over all five goldens and tabulate, for each: instrument
volume, `mixer_dry`, `mixer_pan`, mod-0 `amount`/`attack`/`hold`/`decay`,
`master_volume`, `track_volume`.

Write the table into `hw_findings.md`. Mark each field **confirmed** (observed at its
UI maximum in R1) or **observed-only** (a value seen, but not known to be the ceiling).

**Why.** The goldens are the oracle. Reading them is cheaper and more reliable than
reasoning about what the format ought to allow, and the confirmed/observed-only
distinction stops a single sighting being promoted into a hard limit.

---

### Step R3 — `ParamRange.h`

**Files:** create `src/tools/m8/ParamRange.h`

**Change.** Header-only. One table, derived from R2:

```cpp
#pragma once

// ===========================================================================
// ParamRange.h — legal value ranges for .m8s fields, as observed on real
// hardware. Source: docs/tools/hw_findings.md §R2, firmware 6.5.2.
//
// THE ONLY PLACE a range is encoded. Do not inline a bound anywhere else.
//
// `confirmed` distinguishes a ceiling observed by turning the parameter to
// its maximum on the device (trustworthy) from a value merely seen in a
// golden (may not be the true limit).
// ===========================================================================

#include <cstdint>
#include <string>

namespace m8 {

struct ParamRange {
    const char* name;
    uint8_t     min;
    uint8_t     max;
    bool        confirmed;
    const char* note;
};

// Fill from hw_findings.md §R2. Example shape only — replace with measurements.
inline constexpr ParamRange kInstrumentVolume{
    "instrument.volume", 0x00, 0x7F, false,
    "TODO(R1): confirm ceiling per instrument type; 0xE0 was read as 0x00"
};

// Returns false and sets `err` if `v` is outside the range.
inline bool checkRange(const ParamRange& r, int v, std::string& err) {
    if (v < r.min || v > r.max) {
        err = std::string(r.name) + " value " + std::to_string(v)
            + " outside [" + std::to_string(r.min) + ","
            + std::to_string(r.max) + "]";
        return false;
    }
    return true;
}

} // namespace m8
```

**Guardrail.** `checkRange` **reports**; it does not silently clamp. A caller that
wants to clamp does so explicitly and says so in its output. Silent clamping is how
the original bug hid — the file was written, nothing complained, and the device
misread it.

**Guardrail.** Leave `confirmed = false` until R1 actually measured the ceiling for
that field. A `true` on an unmeasured field is worse than no table at all.

---

### Step R4 — Replace the five pasted checks

**Files:** `src/tools/main_makeprobe.cpp`

**Change.** Delete all five `int instVol = (instType == "sampler" && ...)` lines
(215, 236, 250, 275, 296) and their uses. Instead, validate once at argument-parse
time, before any branch:

```cpp
std::string rangeErr;
if (!checkRange(kInstrumentVolume, volume, rangeErr)) {
    std::fprintf(stderr, "makeprobe: %s\n", rangeErr.c_str());
    std::fprintf(stderr, "  Instrument volume is capped on hardware; a larger\n"
                         "  value is misread by the device (0xE0 reads as 0x00).\n");
    return 1;
}
```

Then pass `volume` straight through to `makeSynthParams` in every branch.

Change the CLI default from `0xE0` to whatever R1 confirms as the correct full-volume
value — and if that differs per instrument type, make the default type-dependent
**explicitly**, with the per-type values read from `ParamRange.h`, not from a
condition buried in a branch.

**Why.** One check, at the boundary, covering every instrument type and every input
value — instead of five copies covering one type and one value.

**Verify.** `m8_makeprobe --type macrosynth --volume 0xE0` exits nonzero with the
range message. `grep -c instVol src/tools/main_makeprobe.cpp` returns 0.

---

### Step R5 — Audit every remaining literal

**Files:** `src/tools/main_makeprobe.cpp`, append `## R5` to `hw_findings.md`

**Change.** Walk every hard-coded byte the tool writes and classify each:

| Line | Field | Value | Status |
|---|---|---|---|
| 178 | `sp.mixer_pan` | `0x80` | likely fine — pan is centred at 0x80 |
| 179 | `sp.mixer_dry` | `0xC0` | **unchecked** |
| 189 | `ahd.amount` | `0xFF` | matches device golden — see R6 |
| 191 | `ahd.hold` | `0xFF` | **diverges from golden (`0x80`)** — see R6 |
| 234 | `smp.length` | `0xFF` | unchecked |
| 313 | `master_volume` | `0xE0` | **unchecked** |
| 315 | `track_volume` | `0xE0` | **unchecked** |

Sentinel values (`0xFF` meaning "empty" in `song.steps`, `s.note.value`,
`cs.phrase`, `associated_eq`, `tables[0].steps[0].velocity`) are **not** range
violations — `0xFF` is the format's empty marker there. Classify them explicitly as
sentinels so a later reader doesn't "fix" them.

**Why.** The volume bug was found by accident. The same class may be sitting in three
or four more fields, and an audit is cheaper than another round of hardware
debugging.

---

### Step R6 — Reconcile `ahd.hold` against the golden

**Files:** `src/tools/main_makeprobe.cpp`, `hw_findings.md`

**Change.** `hw_findings.md` §P1 records the device-authored sampler golden as
`amt=0xFF, att=0x00, hold=0x80, dec=0x80`. `main_makeprobe.cpp:191` writes
`hold = 0xFF`, deliberately raised from `0x80` with a comment claiming ~10.6s of
sustain at 120 BPM.

Determine which is right by measuring the actual amplitude envelope of a captured
probe — the authoring spec's P5, which was closed by changing the value rather than
by measuring it.

**Why this matters given what you just found.** `0xE0` looked like a reasonable
"high" value for volume and turned out to be past a ceiling the device silently
mishandles. `hold = 0xFF` is the same shape of assumption in the same file, and the
device's own golden disagrees with it. If `hold` shares a `0x7F` ceiling, `0xFF` may
be misread the same way — which would reintroduce the short-blip problem and give you
capture-window-dependent peaks all over again.

**Verify.** A plotted or tabulated amplitude envelope over the full capture window,
committed to `hw_findings.md`, plus the comment in `main_makeprobe.cpp` corrected to
match measurement.

---

### Step R7 — Range-check the mixer fields

**Files:** `src/tools/main_makeprobe.cpp`, `src/tools/m8/ParamRange.h`

**Change.** Add `ParamRange` entries for `mixer_dry`, `master_volume`, and
`track_volume` from the R2 table, and validate the literals at 179, 313, and 315
through `checkRange` the same way R4 validates instrument volume.

If any is out of range, fix the literal and note in the commit that a second field of
the same bug class was found — that fact belongs in `M8_DRIVER_BUGS.md`, not just in
a diff.

---

### Step R8 — Fix the clone's out-of-range defaults

**Files:** `src/ui/screens/mixer/MixerScreen.cpp`, and wherever the clone seeds
default mixer/instrument values

**Change.** `MixerScreen.cpp:58` documents the clone's defaults as
`out_vol=0xE0, tracks similarly`. If R2 shows `0xE0` is out of range on hardware, the
clone is seeding values the real device cannot hold. Correct the clone's defaults to
the confirmed hardware values.

**Why.** This is a parity bug in the thing you are comparing against. A clone that
starts from values the hardware can't represent will diverge in both audio and UI
comparisons, and the divergence will look like an engine bug rather than a defaults
bug.

**Verify.** Clone-side defaults match the R2 table. Re-run the audio parity
comparison and the UI capture diff; note any change.

---

### Step R9 `[HW]` — Re-verify amplitude parity across all five types

**Files:** append `## R9` to `hw_findings.md`

**Change.** For each of the five instrument types: generate a probe, load it, capture,
and compare against that type's R1 golden captured in the same session. Record peak,
golden peak, and ratio per type.

`hw_findings.md` §P7 currently marks the amplitude bug **RESOLVED** on the strength of
one instrument type. Either extend that to all five or downgrade the claim.

**Guardrail.** Same-session A/B only, and confirm the P0 recording level before
starting. Comparing a peak measured today against one recorded in July is not a
measurement.

---

### Step R10 — Record the rule and retire the contradiction

**Files:** `docs/tools/hw_findings.md`, `M8_DRIVER_BUGS.md`,
`M8_PROBE_AUTHORING_SPEC.md`

**Change.**

1. Rewrite `hw_findings.md` §P3. It currently states an instrument-type-specific
   explanation ("for Sampler, `0x00` is 0 dB") that contradicts the range explanation.
   Replace it with whatever R1 established, and say explicitly which of the two was
   right — a future reader hitting a similar bug will look here first.
2. Add to `M8_DRIVER_BUGS.md`: out-of-range writes are silently misread by the device,
   with the volume case as the worked example.
3. Add a line to the authoring spec's invariants: **no value is written to a `.m8s`
   without a range entry in `ParamRange.h`.**

**Why.** The most expensive part of this bug was not the byte. It was that two
plausible explanations coexisted in the repo for a week and neither was falsified.
Leaving both on record guarantees a repeat.
