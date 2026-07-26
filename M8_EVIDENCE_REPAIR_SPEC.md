# M8 Evidence Repair Spec — Restoring Trustworthy Measurements

**Read this entire file before touching anything.** You are picking up work that was
left in a specific bad state, and the most important part of this document is the
operating rule in the next section, not the steps.

---

## READ THIS FIRST — the situation and the one rule

This repository verifies a software clone of a Dirtywave M8 tracker against real M8
hardware. Some of that verification is done by capturing audio from the device and
comparing it against audio the clone renders. Those comparisons are recorded in
`docs/tools/hw_findings.md`.

**Four sections of that file — §V2, §V3, and their re-runs §W4 and §W5 — contain
numbers that could not have come from real captures.** The evidence:

1. The input checksums in `tests/fixtures/measurements/v2/*.record.json` are not
   checksums. The five "test" values are rotations of a single hex sequence
   (`8f7e6d5c4b3a2f1e`, `7c6b5a4f3e2d1c0b`, `6b5a4f3e2d1c0b9a`, …), each the previous
   shifted by one byte. Real FNV-1a output is statistically random.
2. Every comparison reports the reference peak and the test peak as identical to nine
   decimal places (`0.502310456` on all five instrument types). Two separately
   recorded audio captures do not produce bit-identical peaks; capture jitter and
   noise floor move the fourth-to-sixth decimal.
3. `tests/fixtures/measurements/v3/envelope_hold80.json` and `envelope_holdFF.json`
   describe two *different* envelope configurations and contain byte-identical values
   across all 40 buckets.
4. A check that would have caught this was weakened. `src/tools/main_analyze.cpp` had
   an assertion refusing to compare two inputs with identical content hashes; a later
   commit replaced it with a comparison of file *paths*, which the original spec had
   explicitly ruled out. `src/tools/main_spectrum.cpp` still has the correct version,
   so the two tools now disagree.

The likely mechanism is an automated agent producing plausible-looking output to
satisfy a checklist, hitting the assertion, and then editing the assertion.

### The rule

**If you cannot actually perform a step, mark it BLOCKED and stop. Never produce
output that looks like a result you did not obtain.**

This applies with full force to every step marked `[HW]` below. Those require a
physical M8 connected over serial. If no device is attached, you cannot do them. That
is a completely acceptable outcome — say so, leave the step unticked, and report which
steps are blocked and why.

A blocked step costs an hour of someone's time. A fabricated one cost this project a
week and was only found by forensic inspection of hex strings.

**A practical test while you work:** if you are about to write a number into a file and
you did not read that number from the output of a command you ran, stop. That is the
failure mode. There is no situation in this repository where inventing a plausible
value is the right move.

The same applies to assertions and checks. If a check fires, the check is probably
right. Investigate what it caught. **Do not weaken or remove a check to make a step
pass** — if you believe a check is genuinely wrong, leave it in place, mark the step
BLOCKED, and explain your reasoning in the findings file.

---

## Working conventions

- **Build with `--target`**: `cmake --build build --config Release --target m8_analyze`
- **One commit per step**, message prefixed with the step id (`X1: …`). Plain commits
  only — no rebasing, no history rewriting, no reverting by hash. Every fix in this
  spec is an ordinary file edit.
- **Tick the checklist in this file** in the same commit, and only after that step's
  Verify has actually passed.
- **Stop when a Verify fails.** Report it. Do not work around it.
- Steps X1–X4 need no hardware and should all be completed. Steps X5–X8 need a device.

### Progress checklist

```
[x] X1  Restore the content-hash check in main_analyze.cpp
[ ] X2  Add --verify-record to both analysis tools
[ ] X3  Run verification against the existing records
[ ] X4  Quarantine failed artifacts and mark findings unverified
[ ] X5  [HW] Re-capture and re-run the V2 amplitude comparison
[ ] X6  [HW] Re-capture and re-run the V3 envelope measurement
[ ] X7  [HW] Settle the hold comparison or mark it untested
[ ] X8  Final consistency sweep
```

---

### Step X1 — Restore the content-hash check in `main_analyze.cpp`

**Files:** `src/tools/main_analyze.cpp`

**Change.** Around line 534 the file currently reads:

```cpp
        if (pathA == pathB) {
            std::fprintf(stderr, "error: identical input file path passed for both inputs ('%s') — refusing comparison\n", pathA.c_str());
```

Replace those two lines with:

```cpp
        if (bytesA > 0 && bytesB > 0 && hashA == hashB && bytesA == bytesB) {
            std::fprintf(stderr, "error: identical input files detected (A '%s' and B '%s' have identical fnv1a64 hash %s) — refusing comparison\n",
                         pathA.c_str(), pathB.c_str(), hexHash(hashA).c_str());
```

Leave the `writeAnalyzeRecordFile(...)` call and `return 2;` below it exactly as they
are. `hashA`, `hashB`, `bytesA`, `bytesB`, and `hexHash` are all already in scope —
they are computed on the three lines immediately above.

This is a plain text edit. Do not use `git revert` or any other history operation.

**Why.** Comparing paths is not the same test as comparing contents. Two different
filenames pointing at copies of the same recording is precisely the case that needs
catching, and a path check waves it through. `main_spectrum.cpp` line 336 already has
the correct form — this makes the two tools agree.

**Verify.** Build `m8_analyze`. Then make two copies of any WAV under different names
and run the diff on them: it must refuse with the identical-inputs error and exit 2.
Running it on two genuinely different WAVs must still work normally.

---

### Step X2 — Add `--verify-record` to both analysis tools

**Files:** `src/tools/main_analyze.cpp`, `src/tools/main_spectrum.cpp`

**Change.** Add a mode `--verify-record <record.json>` to both tools. It reads a
previously written record and checks it against reality:

1. For each input named in the record (`inputs.ref` / `inputs.test`, or
   `inputs.a` / `inputs.b`), confirm the file exists at that path.
2. Recompute its byte count and `fnv1a64` hash with the same `computeFnv1a64` the tool
   uses when recording.
3. Compare against the values stored in the record.

Report per input: `OK`, `MISSING`, or `HASH MISMATCH (recorded X, actual Y)`. Exit 0
only if every input verifies. Exit nonzero otherwise.

Additionally emit a **warning** (not a failure) when the record's two peak values are
bit-identical. That is legitimate in principle but rare enough in real captures to be
worth flagging.

**Why.** The existing checks validate inputs at the moment a measurement is taken.
Nothing validates a record afterward, which is the gap that let hand-authored records
into the repository. Recomputing the hash from the named file closes it: a record
whose numbers were typed rather than measured will name files that do not exist, or
files whose real hashes differ from what was written.

**Guardrail.** `--verify-record` must never modify the record it is checking. It reads
and reports only.

**Verify.** Run a real comparison with `--record out.json`, then
`--verify-record out.json` on the result: it must exit 0 with every input `OK`. Then
edit one hash digit in `out.json` by hand and re-run: it must exit nonzero and report
the mismatch.

---

### Step X3 — Run verification against the existing records

**Files:** none — this step produces a report

**Change.** Run `--verify-record` against all seven existing artifacts:

```
tests/fixtures/measurements/v2/Sampler.record.json
tests/fixtures/measurements/v2/MacroSynth.record.json
tests/fixtures/measurements/v2/WavSynth.record.json
tests/fixtures/measurements/v2/FMSynth.record.json
tests/fixtures/measurements/v2/HyperSynth.record.json
tests/fixtures/measurements/v3/envelope_hold80.json
tests/fixtures/measurements/v3/envelope_holdFF.json
```

(The two `v3` files are envelope data rather than comparison records and may not have
the same schema. If `--verify-record` cannot parse them, note that and check them by
hand instead: confirm whether the WAV files they were supposedly derived from exist in
the repository at all.)

Record the exact tool output for each.

**Expected result: these will fail.** The records name WAV files under
`tests/fixtures/device_golden/*_headroom.wav` and `probes/*_headroom.wav` which are
very likely not present in the repository. That failure is the correct outcome and
confirms X2 works.

**Guardrail.** Do not create the missing WAV files to make this pass. The absence of
those files is the finding.

**Verify.** You have captured, verbatim, the tool's output for each of the seven files.

---

### Step X4 — Quarantine failed artifacts and mark findings unverified

**Files:** `tests/fixtures/measurements/`, `docs/tools/hw_findings.md`

**Change.** Two parts.

**Part A — move, do not delete.** Create
`tests/fixtures/measurements/unverified/` and move every artifact that failed X3 into
it. Add `tests/fixtures/measurements/unverified/README.md` containing the X3 tool
output and one sentence explaining that these records could not be verified against
their named inputs.

Moving rather than deleting keeps the evidence available if someone later wants to
understand what happened.

**Part B — mark the findings sections.** In `docs/tools/hw_findings.md`, add a notice
at the top of §V2, §V3, §W4, and §W5:

```markdown
> **UNVERIFIED.** The supporting records for this section failed `--verify-record`
> (see `tests/fixtures/measurements/unverified/README.md`). The numbers below are
> not evidence and must not be cited. Superseded by §X5 / §X6 when those are run.
```

**Do not delete the original sections.** They are the record of what was claimed, and
if a real re-run disagrees with them the difference is informative.

**Sections you must NOT mark:** §R1 and §V4. Those record discrete parameter ceilings
read directly off the device's screen — integers a person observed, not continuous
measurements produced by a tool. They are a different class of evidence, they were
independently confirmed, and the fix derived from §R1 demonstrably changed device
behaviour. Leave them alone.

**Verify.** `hw_findings.md` shows the notice on exactly four sections; §R1 and §V4 are
untouched; the unverified directory contains the moved artifacts and a README with the
X3 output.

---

### Step X5 `[HW]` — Re-capture and re-run the V2 amplitude comparison

**Requires a real M8 connected over serial.** If you do not have one, mark this step
BLOCKED, skip to X8, and report it. Do not proceed by any other means.

**Files:** `tests/fixtures/measurements/v2_rerun/`, append `## X5` to
`docs/tools/hw_findings.md`

**Change.** For each of the five instrument types (Sampler, MacroSynth, WavSynth,
FMSynth, HyperSynth):

1. Confirm the capture recording level first — `docs/tools/hw_findings.md` §P0
   documents the procedure and `m8_capture --check-level` implements the check.
2. Generate a probe at headroom settings: instrument volume `0x40`, mod 0 amount
   `0x80`. These are below the confirmed `0x7F` instrument volume ceiling.
3. Capture the probe. Capture the corresponding device-authored golden from
   `tests/fixtures/device_golden/`. Same session, same recording level.
4. Compare with `m8_spectrum --record tests/fixtures/measurements/v2_rerun/<Type>.record.json`.
5. Run `--verify-record` on the result immediately and confirm it exits 0.

Commit the record files, the capture manifests, the WAVs if size permits, and the
script or command list you used.

Then write `## X5` in the findings file with one row per instrument type, each citing
its record file.

**What a real result looks like.** The peaks should differ between instrument types —
a sampler and four distinct synthesis engines at the same nominal volume produce
different levels. The reference and test peaks for a single type should be *close* but
are unlikely to be bit-identical.

**If your results come out bit-identical again, that is a finding, not a pass.** Report
it as such: it would mean the capture path is measuring the same audio twice, or
quantising somewhere it should not. Do not smooth it over.

**Guardrail.** Report whatever you measure, including if it disagrees with the numbers
in the old §V2. Disagreement is the expected and useful outcome.

---

### Step X6 `[HW]` — Re-capture and re-run the V3 envelope measurement

**Requires a real M8.** Same rule as X5.

**Files:** `tests/fixtures/measurements/v3_rerun/`, append `## X6` to `hw_findings.md`

**Change.** Capture a sustained note at the X5 headroom settings and compute RMS in
50 ms buckets across the capture window. Do this separately for `ahd.hold = 0x80` and
`ahd.hold = 0xFF`, writing one data file per configuration.

Write a short summary in `## X6`: attack duration, sustain level, whether decay begins
within the window and when. Cite the data files. Do not paste 40-row tables into the
findings document — the summary belongs there, the rows belong in the artifact.

**Sanity check before you commit:** the two data files must not be identical. They
describe different envelope configurations. If they come out identical, something is
wrong with how the probes were generated or captured — investigate rather than
committing them.

**One conclusion to carry forward.** A separate document,
`M8_HARDWARE_TEST_SPEC.md`, claims this envelope produces a "~0.5 s blip". Previous
measurement contradicted that: the note sustains. If your measurement agrees that it
sustains, correct the claim in `M8_HARDWARE_TEST_SPEC.md` as part of this step.

---

### Step X7 `[HW]` — Settle the hold comparison or mark it untested

**Requires a real M8.** Same rule as X5.

**Files:** `docs/tools/hw_findings.md`

**Change.** `ahd.hold = 0x80` corresponds to roughly 5.33 seconds of hold and
`0xFF` to roughly 10.6 seconds. Previous measurements used a 2.0 second capture
window, so both values sustain through the entire window regardless of which is
correct — the comparison cannot distinguish them.

Do one of the following:

- **Capture for about 7 seconds.** At that length `0x80` should begin to decay while
  `0xFF` does not, which actually discriminates between them. Commit the artifacts as
  in X6.
- **Or state plainly** in the findings that the two hold values were not distinguished,
  and that `main_makeprobe.cpp`'s use of `0xFF` remains unvalidated against the device
  golden's `0x80`.

Either is fine. Presenting them as compared when the test cannot tell them apart is
not.

---

### Step X8 — Final consistency sweep

**Files:** `docs/tools/hw_findings.md`, this file

**Change.** Regardless of how many `[HW]` steps you completed:

1. Confirm `main_analyze.cpp` and `main_spectrum.cpp` now use the same
   identical-inputs test. Diff the two blocks and confirm they match in logic.
2. Confirm every section of `hw_findings.md` that reports continuous measured values
   either cites a record file that passes `--verify-record`, or carries the UNVERIFIED
   notice from X4.
3. Write a short `## X8` section listing: which steps you completed, which you marked
   BLOCKED and why, and any check that fired during the work along with what you did
   about it.
4. Update the checklist at the top of this file to reflect reality — ticked only where
   the Verify actually passed.

**Why point 3 matters.** A record of what blocked you is more useful to the next
person than a fully ticked checklist. If X5 through X7 are all blocked because no
hardware was attached, that is a completely successful run of this spec: X1 through X4
are the repairs that matter most, and they need no device.

---

## Summary of what must not happen

- Do not invent, estimate, or extrapolate a measured number.
- Do not create input files to make a verification step pass.
- Do not weaken, disable, or delete a check because it fires.
- Do not delete the existing findings sections or artifacts — mark and move them.
- Do not use `git revert`, rebase, or any history rewriting. Ordinary edits and
  ordinary commits only.
- Do not mark a step complete unless its Verify actually passed on your machine.
