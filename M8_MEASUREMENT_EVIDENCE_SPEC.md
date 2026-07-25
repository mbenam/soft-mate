# M8 Measurement Evidence Spec — Making Measurements Reproducible From The Repo

Follow-on to `M8_MEASUREMENT_VALIDITY_SPEC.md`, which is 8/8 ticked. V1, V4, and V5
landed cleanly: the saturation guard is correctly implemented in both analysis tools,
the mixer ceilings were measured as discrete UI maxima, and `ParamRange.h` marks
exactly the three measured fields `confirmed = true` while leaving `mixer.pan` and the
four mod fields at `false`.

This spec addresses the remaining gap: **§V2 and §V3 exist only as hand-typed tables
in a markdown file, with no artifact anyone can re-derive them from.**

That matters for a specific reason, not a general one. §V2 reports the probe peak and
the golden peak as identical to six decimal places on all five instrument types. Two
separately authored files, captured in separate takes through a USB audio tap, do not
produce bit-identical peaks — capture jitter, noise floor, and analysis-window
alignment move the fourth-to-sixth decimal even when settings match. §V3 shows RMS of
exactly `0.500` in eight of ten cells with both hold columns identical, and states a
50 ms bucket methodology while presenting five rows, one spanning 450 ms.

Those numbers may be real and surprising, or they may be an artifact of how the
measurement was run. **Nothing in the repo can distinguish those cases**, which is
the actual defect. V1 exists because a check that cannot fail is not evidence; a
number with no derivable source has the same property.

The fix is structural rather than procedural: make the tools emit the record, so the
evidence is a build output instead of something a person is trusted to transcribe.

> **Scope.** The analysis and capture tooling, plus two re-runs. No changes to
> `m8_nav`, the driver, `ParamRange.h`, or the UI parity work. Steps marked `[HW]`
> need the device on `COM4`.

### Progress checklist

```
[x] W1  --record flag emitting a measurement record
[x] W2  Input checksums in the record
[ ] W3  Capture manifest from m8_capture
[ ] W4  [HW] Re-run V2 with committed artifacts
[ ] W5  [HW] Re-run V3 at the stated bucket resolution
[ ] W6  [HW] Settle the hold comparison, or mark it untested
[ ] W7  Findings-format rule: measured tables cite their artifact
```

Same discipline as the other specs: one commit per step prefixed with the step id,
tick the checklist in this file in the same commit, re-read a step before starting
it, stop when a Verify fails.

---

### Step W1 — `--record` flag emitting a measurement record

**Files:** `src/tools/main_spectrum.cpp`, `src/tools/main_analyze.cpp`

**Change.** Add `--record <path>` to both tools. When present, write a JSON record
alongside the normal output:

```json
{
  "tool": "m8_spectrum",
  "argv": "m8_spectrum --ref golden.wav --test probe.wav --record rec.json",
  "timestamp_utc": "2026-07-25T14:02:11Z",
  "inputs": {
    "ref":  { "path": "golden.wav", "bytes": 352844, "sha256": "..." },
    "test": { "path": "probe.wav",  "bytes": 352844, "sha256": "..." }
  },
  "peaks": { "ref": 0.502310, "test": 0.502310 },
  "saturation": { "threshold": 0.995, "ref_saturated": false, "test_saturated": false },
  "norm_gain_db": { "ref": 5.9718, "test": 5.9718 },
  "result": { "ratio": 1.0000, "status": "PASS" }
}
```

Emit full float precision — do not round in the record. Rounding is a presentation
choice and belongs in the markdown table, not in the artifact.

**Why.** Every number in `hw_findings.md` should be traceable to a file produced by a
tool run. Making the tool write it removes transcription from the loop entirely,
which is more reliable than asking anyone to be careful.

**Guardrail.** The record is written on failure too — including saturation refusals
and below-threshold silence. A failed measurement is evidence and needs to be
committable. Write the record, then return the nonzero exit code.

**Verify.** `m8_spectrum` with `--record` on any two WAVs produces a parseable JSON
file whose `peaks` match the values printed to stdout.

---

### Step W2 — Input checksums in the record

**Files:** `src/tools/main_spectrum.cpp`, `src/tools/main_analyze.cpp`

**Change.** Populate the `sha256` and `bytes` fields for every input. Use a small
self-contained SHA-256 — no new third-party dependency (repo invariant). If adding one
is disproportionate, use a documented 64-bit content hash instead and name it honestly
in the field (`"fnv1a64"`, not `"sha256"`).

Then add an assertion to both tools: **if the reference and test inputs have identical
checksums, refuse to report a comparison.** Print an error naming both paths and exit
nonzero.

**Why this is the load-bearing step.** The specific thing that made §V2 implausible
was probe and golden matching exactly, five times. If the two inputs were in fact the
same recording, this check catches it instantly and permanently. If they were genuinely
different files that measured identically, the differing checksums in the committed
record prove it and turn a suspicious table into a real and interesting finding worth
investigating.

Either way the ambiguity is gone, and it stays gone for every future measurement
without anyone having to remember this conversation.

**Guardrail.** Compare content hashes, not paths. Two different filenames pointing at
copies of one recording is exactly the case this needs to catch.

**Verify.** Running a comparison with the same WAV passed as both `--ref` and `--test`
exits nonzero with the identical-input error. Running it with two genuinely different
WAVs succeeds and records two different hashes.

---

### Step W3 — Capture manifest from `m8_capture`

**Files:** `src/tools/main_capture.cpp`

**Change.** When `m8_capture` writes a WAV, also write `<name>.manifest.json`
recording: the output path, byte count and content hash, the capture settings
(duration, sample rate, keyjazz note and velocity), the `--check-level` reference
result from P0, the COM port, and a UTC timestamp.

**Why.** W1 and W2 make the *analysis* reproducible. This makes the *inputs*
attributable — which probe file, captured with what settings, at what recording level.
Without it, a committed record proves two different files were compared but not what
either of them was.

**Verify.** A capture produces both the WAV and its manifest; the manifest's hash
matches what `m8_spectrum --record` independently computes for the same file.

---

### Step W4 `[HW]` — Re-run V2 with committed artifacts

**Files:** `tests/fixtures/measurements/v2/` (new), rewrite `## V2` in
`docs/tools/hw_findings.md`

**Change.** Repeat the five-type amplitude parity comparison at the same headroom
settings §V2 used (instrument volume `0x40`, mod 0 amount `0x80`). For each type,
commit:

- the probe capture manifest and the golden capture manifest,
- the `--record` JSON from the comparison,
- the exact commands, in a `run.sh` or `run.ps1` alongside them.

Commit the WAVs themselves if size permits; if not, commit the manifests and note in
the findings where the WAVs live.

Then rewrite `## V2` to cite the record file for each row, and report the ratio at the
precision the record contains.

**Why.** This is the re-run that either confirms §V2 or corrects it, and it is the
first measurement in the project that a third party could check.

**Guardrail.** Do not copy the existing §V2 numbers forward. Report whatever this run
produces. If it disagrees with the current table, that disagreement is the finding —
record both and say which is superseded. Reproducing the previous numbers exactly,
without artifacts differing, would itself warrant a note.

**Verify.** Every row in the rewritten §V2 names a record file that exists in the
repo, and each record shows two distinct input hashes.

---

### Step W5 `[HW]` — Re-run V3 at the stated bucket resolution

**Files:** `tests/fixtures/measurements/v3/` (new), rewrite `## V3`

**Change.** §V3 states "fine-grained 50 ms RMS buckets across 2.0s" — that is 40
buckets — and presents five rows, one spanning 450 ms. Re-run at the resolution
actually claimed, and commit the per-bucket data as CSV or JSON rather than as a
markdown table.

Then rewrite `## V3` with a summary (attack duration, sustain level, whether and when
decay begins) that cites the committed data file. A 40-row table does not belong in a
findings document; the summary plus the artifact does.

**Guardrail.** Keep §V3's correct conclusion. The note sustains, and the "~0.5 s blip"
claim in `M8_HARDWARE_TEST_SPEC.md` is wrong — that part stands regardless of what the
re-run shows about bucket detail, and `M8_HARDWARE_TEST_SPEC.md` should carry the
correction.

**Verify.** The committed data file has ~40 rows and the summary in §V3 is consistent
with it.

---

### Step W6 `[HW]` — Settle the hold comparison, or mark it untested

**Files:** `docs/tools/hw_findings.md`, `src/tools/main_makeprobe.cpp` if the finding
changes the value

**Change.** §V3 presents `hold = 0x80` versus `hold = 0xFF` as compared. By §V3's own
figures those are ~5.33 s and ~10.6 s, both longer than the 2.0 s capture window — so
every bucket sustains for both values no matter which is correct. The comparison is
decided by construction and cannot distinguish them.

Do one of:

- **Capture past the shorter hold.** A window of ~7 s would show `0x80` beginning to
  decay while `0xFF` does not, which is a real discriminator. Commit the artifacts per
  W1-W3.
- **Or mark it untested.** State plainly in §V3 that the two hold values were not
  distinguished, and that `main_makeprobe.cpp:191`'s `0xFF` remains unvalidated against
  the device golden's `0x80`.

Either is acceptable. Presenting them as compared is not.

**Why.** This is the same defect V1 was written to catch — a comparison whose outcome
is fixed by the setup rather than by the thing being measured — relocated from the
amplitude dimension into the time dimension. Worth naming as such in the findings, so
the pattern is recognized next time it appears in a third dimension.

---

### Step W7 — Findings-format rule

**Files:** `docs/tools/hw_findings.md`, `M8_PROBE_AUTHORING_SPEC.md`

**Change.**

1. Add a short header to `hw_findings.md`: **any table of measured continuous values
   must cite the record file it came from.** Discrete readings taken off the device UI
   (§R1's volume ceilings, §V4's mixer ceilings) are exempt and should say so — a UI
   maximum is an integer a person reads, not a measurement a tool produces, and
   demanding an artifact for those would be noise.
2. Add to the authoring spec's invariants: **a measured number with no committed
   artifact is a claim, not a result.**
3. Retroactively mark the current §V2 and §V3 as superseded by §W4 and §W5 once those
   land, rather than deleting them. The original tables are useful — if the re-run
   disagrees, the difference is itself informative.

**Why.** The distinction in point 1 is the whole rule. §R1 and §V4 are trustworthy
precisely because a UI ceiling is discrete and directly observed; §V2 and §V3 are not,
because continuous audio measurements have noise and theirs had none. Encoding that
difference stops the rule from becoming bureaucratic overhead applied to everything.

---

## What this spec deliberately does not do

- **Does not assume §V2 and §V3 are wrong.** They may be correct. The defect is that
  the repo cannot tell, and W1-W3 fix that permanently regardless of how the re-runs
  come out.
- **Does not re-open V1, V4, or V5.** The saturation guard is correctly implemented
  and the mixer ceilings are discrete UI readings of the kind §R1 established as
  reliable.
- **Does not require artifacts for discrete UI readings.** See W7.
- **Does not add a third-party hashing dependency.** A documented 64-bit content hash,
  honestly named, is sufficient for the identical-input check.
