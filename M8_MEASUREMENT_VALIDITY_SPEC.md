# M8 Measurement Validity Spec — Making the Parity Tests Able To Fail

Follow-on to `M8_PARAM_RANGE_SPEC.md`, which is 10/10 ticked. The range finding it
produced is correct — `0x7F` is the confirmed instrument volume ceiling on all five
types, and the silent-probe bug is genuinely fixed. This spec addresses a different
problem: **two of the steps that verified the fix used measurements that could not
have detected a failure.**

`hw_findings.md` §R9 reports ten measurements — five probe peaks and five golden
peaks, across a sampler and four distinct synthesis engines — all reading exactly
`0.999664`. That is 32757/32768, eleven counts below full scale. Six different audio
paths do not coincidentally produce identical peaks to six decimal places. The
signals are almost certainly pinned at the limiter, and when both sides of a
comparison are clipped, a ratio of `1.0000` is arithmetic, not evidence.

§R6 shows the same signature: peak `1.000` in all four windows for both
`hold = 0x80` and `hold = 0xFF`. A flat maximum across two seconds means no decay
was observed at all, which contradicts the earlier "~0.5 s blip" finding. A clipped
signal reads full scale until it drops below the ceiling, so a saturated measurement
produces exactly that table.

**This is the same defect as `verifyRoundTrip`** — a check that structurally cannot
fail — which is the reason the probe-authoring spec chain exists. The remedy is the
same: make the test capable of returning a negative result, then re-run it.

A secondary thread: `ParamRange.h` correctly marks `mixer_dry`, `master_volume`, and
`track_volume` as `confirmed = false`, but `hw_findings.md` §R5 lists all three as
"Valid value" against ranges nobody measured. The code is honest; the findings
document is not. This spec reconciles them.

> **Scope.** Hardware measurement and the analysis tooling. Steps marked `[HW]` need
> the device on `COM4`. No changes to `m8_nav`, the driver, or the UI parity work.

### Progress checklist

```
[x] V1  Saturation guard in the analysis tooling
[x] V2  [HW] Re-run amplitude parity with headroom
[ ] V3  [HW] Re-run the envelope measurement with headroom
[ ] V4  [HW] Measure the three unconfirmed mixer ceilings
[ ] V5  Update ParamRange.h with measured values
[ ] V6  Write the missing provenance section
[ ] V7  Correct §R5 and re-check the clone default from R8
[ ] V8  Add the invariant and update docs
```

Same discipline as the other specs: one commit per step prefixed with the step id,
tick the checklist in this file in the same commit, re-read a step before starting
it, stop when a Verify fails.

---

### Step V1 — Saturation guard in the analysis tooling

**Files:** `src/tools/main_spectrum.cpp`, `src/tools/main_analyze.cpp`

**Change.** `main_spectrum.cpp` already refuses to normalize a signal below
`kMinPeakThresh` — the correct instinct, applied at only one end of the range. Add
the upper counterpart:

```cpp
// A signal at or above this peak is very likely clipped at the limiter. Peak
// ratios between two clipped signals are 1.0 by construction and prove
// nothing, so refuse to report a parity ratio rather than reporting a
// meaningless one. See M8_MEASUREMENT_VALIDITY_SPEC.md V1.
static constexpr double kSaturationThresh = 0.995;
```

When either the reference or the test peak is at or above it: emit a clear warning
naming which side saturated and its peak, **do not emit a parity ratio**, and exit
nonzero.

Add the same check to `main_analyze.cpp` wherever it reports peak-based comparisons.

**Why this is first.** Every subsequent step in this spec is a measurement. Without
the guard, V2 and V3 can reproduce the same unfalsifiable result and be recorded as
passes a second time. The guard makes that failure mode impossible to repeat rather
than relying on whoever runs it to notice.

**Guardrail.** The guard **refuses**; it does not rescale and continue. Silently
compensating would hide the condition the guard exists to surface.

**Guardrail.** Do not set the threshold from the observed `0.999664`. Pick a value
below any legitimate measurement and above the clipping floor; `0.995` is a starting
point. If a genuine unsaturated measurement trips it, raise it and record why in the
commit — do not delete the check.

**Verify.** Re-run the existing §R9 comparison unchanged. It must now fail with a
saturation warning rather than reporting `1.0000`. That failure is the proof the
guard works — if the old comparison still passes, the guard is not wired into the
path R9 used.

---

### Step V2 `[HW]` — Re-run amplitude parity with headroom

**Files:** append `## V2` to `docs/tools/hw_findings.md`

**Change.** Repeat §R9's five-type comparison with the signal held well below
clipping.

1. Confirm the P0 recording level before starting.
2. Generate probes at a reduced volume — start at `0x40` — and capture.
3. **Check the resulting peak lands in roughly `0.3`–`0.7`.** If it is still near
   full scale, the base volume is not what is saturating: the AHD→VOLUME mod at
   `amount = 0xFF` is driving it back to maximum. In that case reduce the mod amount
   as well and iterate until the peak sits in the target band. Record which
   combination produced it.
4. Author matching goldens at the same reduced settings, or reduce the existing
   goldens' volume on-device to match. Same-session A/B only.
5. Record per type: probe peak, golden peak, ratio.

**The assertion that makes this test meaningful:** the five instrument types must
**not** all produce identical peaks. A sampler and four different synthesis engines
at the same nominal volume should differ measurably. If they still match to six
decimals at `0.5` peak, something other than the limiter is flattening the
measurement and V2 has found a second bug — stop and report it rather than recording
a pass.

**Why.** This is the test §R9 was meant to be. A `1.0000` ratio at headroom, with
per-type peaks that differ from each other, is real evidence of amplitude parity.
The same ratio at full scale is not.

**Verify.** Five ratios recorded, every peak below `kSaturationThresh`, and the
per-type peaks distinguishable from one another.

---

### Step V3 `[HW]` — Re-run the envelope measurement with headroom

**Files:** append `## V3` to `docs/tools/hw_findings.md`,
`src/tools/main_makeprobe.cpp` if the finding changes the value

**Change.** Repeat §R6 at the V2 headroom settings, and answer the question the
authoring spec's P5 actually asked: **does the note sustain, or is it a blip?**

Sample the amplitude envelope across the full capture window at finer granularity
than R6's four 500 ms buckets — 50 ms buckets or a plotted envelope. Do this for both
`hold = 0x80` (the device golden's value) and `hold = 0xFF` (what `main_makeprobe`
writes).

Record the actual decay shape, not just per-window peaks. Four buckets all reading
`1.000` cannot distinguish "sustains for two seconds" from "clipped for two seconds."

**Why.** Three documents now disagree about this envelope: `main_makeprobe.cpp`'s
comment claims ~10.6 s, `M8_HARDWARE_TEST_SPEC.md` says ~0.5 s blip, and §R6 says flat
for 2 s. At most one is right, and the one measurement taken was saturated. If the
real behaviour is a short blip, capture-window alignment alone changes the measured
peak — which would put a confound underneath V2's numbers too.

**Guardrail.** If the measured envelope contradicts the comment at
`main_makeprobe.cpp:191`, fix the comment in the same commit. A wrong comment beside a
correct value is how the next person re-derives the wrong conclusion.

**Verify.** A fine-grained envelope table or plot committed for both hold values, and
an explicit statement of which of the three existing claims was correct.

---

### Step V4 `[HW]` — Measure the three unconfirmed mixer ceilings

**Files:** append `## V4` to `docs/tools/hw_findings.md`

**Change.** `ParamRange.h` carries `mixer.dry`, `mixer.master_volume`, and
`mixer.track_volume` with a `0xE0` maximum and `confirmed = false`. The note reads
"Observed nominal master volume 0xE0" — meaning `0xE0` is a value seen in use, not a
ceiling anyone measured.

Measure all three the way R1 measured instrument volume: on the device, turn each
parameter up until it stops increasing, save the project, and read back the stored
byte. Record UI maximum, stored byte, and whether they match, per parameter.

**Why.** The entire bug this spec chain chased was a value written past an unmeasured
ceiling. `master_volume = 0xE0` and `track_volume = 0xE0` are currently written on
exactly that basis. If the mixer shares the `0x7F` ceiling, three more fields are
silently misread and V2's parity numbers were taken through a broken mixer.

**Guardrail.** Turn the parameter to its maximum and read back. Do not infer a
ceiling from the highest value you happen to observe in a golden — that is the
reasoning that produced the `0xE0` entries in the first place.

---

### Step V5 — Update `ParamRange.h` with measured values

**Files:** `src/tools/m8/ParamRange.h`

**Change.** For each parameter V4 measured: set the true `max`, set
`confirmed = true`, and replace the note with the measurement provenance
(`"UI max confirmed on fw 6.5.2, see hw_findings.md §V4"`).

Leave `confirmed = false` on anything V4 did not measure — `mixer.pan` and the four
mod fields are not in V4's scope and must stay unconfirmed.

**Guardrail.** If a measured ceiling is lower than a value currently written by
`m8_makeprobe`, that is a live bug of the original class. Fix the literal and add a
`checkRange` call for it in the same commit, the way R4 did for instrument volume.

**Verify.** `m8_makeprobe` with a now-out-of-range mixer value exits nonzero with the
range message.

---

### Step V6 — Write the missing provenance section

**Files:** `docs/tools/hw_findings.md`

**Change.** `M8_PARAM_RANGE_SPEC.md` step R2 required an observed-range table in
`hw_findings.md`, marking each field **confirmed** or **observed-only**. No `## R2`
section exists — the ranges went straight into `ParamRange.h` and §R5's table.

Add a `## R2 (retroactive)` section with the full table, one row per field, each
marked confirmed or observed-only with a pointer to the section that established it
(§R1 for instrument volume, §V4 for the mixer fields, "not measured" for the rest).

**Why.** The absence of this record is what let §R5 present unmeasured ranges as
validated. A single table with an explicit confirmed column makes that mistake
visible instead of plausible.

---

### Step V7 — Correct §R5 and re-check the clone default

**Files:** `docs/tools/hw_findings.md`, `src/engine/Engine.h`

**Change.** Two corrections:

1. **§R5's table.** It lists `mixer_dry 0xC0` as "Valid value (range [0x00, 0xE0])"
   and `master_volume` / `track_volume` `0xE0` as "Valid value." Change each to the
   V4-measured status. Where V4 shows a value is in range, cite §V4; where it is out
   of range, mark it as a bug and reference the V5 fix.
2. **R8's clone default.** Commit `dcba9a8` changed `track_vol` from `0xF3` to `0xE0`
   in `Engine.h` — from one unverified value to another, since `0xE0` was
   `confirmed = false` at the time. Re-check it against V4's measurement and correct
   if needed.

**Why.** R8 fixed a real out-of-range default and landed on a number that was itself
unmeasured. That is not wrong so much as unfinished, and the clone is the thing every
parity comparison is measured against — a wrong default there shows up as an engine
bug in both the audio and UI comparisons.

---

### Step V8 — Add the invariant and update docs

**Files:** `M8_PROBE_AUTHORING_SPEC.md`, `docs/tools/m8_analyze.md`,
`docs/tools/m8_spectrum.md`, `M8_DRIVER_BUGS.md`

**Change.**

1. Add to the authoring spec's invariants: **a parity measurement taken at or near
   full scale is not evidence.** Both sides clip, the ratio is 1.0 by construction,
   and the test cannot fail. Cite §R9 as the worked example.
2. Document `kSaturationThresh` and the refusal behaviour in the two tool docs, so
   somebody hitting the error understands it is the tool working rather than broken.
3. Add to `M8_DRIVER_BUGS.md`: the two bug classes this chain produced — writing past
   an unmeasured ceiling, and verifying with a test that cannot fail — with the
   volume bug and §R9 as the respective examples.

**Why.** The `0x7F` finding will be remembered because it was dramatic. The reason it
took three investigations to find — checks that agreed with themselves — is the more
transferable lesson and the one most likely to be lost.

---

## What this spec deliberately does not do

- **Does not re-open the `0x7F` finding.** R1 measured it correctly on all five types
  and §P3 was properly retired. That work stands.
- **Does not rescale or compensate saturated measurements.** V1 refuses and exits
  nonzero. A measurement taken under a condition that invalidates it should be
  re-taken, not adjusted.
- **Does not measure `mixer.pan` or the mod fields.** They stay `confirmed = false`
  until someone measures them. An unmeasured range honestly labelled is fine; an
  unmeasured range labelled valid is what this spec is cleaning up.
