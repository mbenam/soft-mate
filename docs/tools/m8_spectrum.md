# m8_spectrum

**Source:** [`src/tools/main_spectrum.cpp`](../../src/tools/main_spectrum.cpp) (394 lines)
**Build target:** `m8_spectrum` (CMakeLists.txt)
**Category:** A/B spectral comparison (per `M8_AUDIO_ANALYSIS_SPEC.md` Part D — the timbre-parity
half of the hardware-vs-clone comparison; distinct from [`m8_analyze`](m8_analyze.md), which
checks one file's own health, not two files against each other)
**Links:** `m8_engine` (for `kissfft` + `dr_wav`). No SDL. **Does not render** — compares two
already-existing WAV files (typically a real hardware capture vs. an `m8_render` output).

## What it does

Given a reference WAV (usually a real hardware capture, e.g. from
[`m8_capture`](m8_capture.md)) and a test WAV (usually an `m8_render` output of the same note),
reports exactly where their spectra diverge: fundamental frequency, a harmonic/partial table
(freq, ref dB, test dB, delta — flagged if `|delta| >= 3dB`), spectral centroid (brightness), and
a single scalar **log-spectral distance** — the number to minimize while tuning a synth model to
match real hardware.

**This tool never fails.** It always exits 0 (assuming both files load and the analysis window is
long enough) — it's a *reporting* tool, not a gate. `fundamental_ok`/the harmonic deltas are
printed and included in the JSON for a human or a calling script to interpret; nothing here sets
a nonzero exit code based on how close the match is.

## Pipeline (what happens internally, useful for interpreting the output)

1. **Downmix** both files to mono (channel-average).
2. **Onset detection** (unless `--no-align`): per-file, independently — finds the first
   ~1.3ms window whose RMS envelope crosses 10% of that file's own peak envelope (-20dB
   relative). Two *independent* per-file onsets rather than cross-correlation, because it's
   simpler/more robust for single-note captures and needs no buffer shifting — the tool just
   starts reading each file from its own detected onset.
3. **Skip 50ms of attack** past each onset — the two recordings are guaranteed to differ most in
   the attack transient, and the steady-state oscillator content (what this tool is actually
   comparing) hasn't settled yet.
4. **Windowed FFT** — up to 65536 samples (~1.4s at 48kHz, chosen for frequency resolution),
   minimum 2048 samples required (else: error, exit 2). Both files use the **same window length**
   (`min(availRef, availTest, 65536)`), so bin indices line up directly between the two spectra.
5. **Fundamental via autocorrelation, not spectral peak-picking.** `findFundamentalAcf` exists
   specifically because a spectral global-max (or even harmonic-product-spectrum) approach fails
   on bright M8 MacroSynth (Braids) tones — a captured C-4 showed a complete 1×-6× harmonic series
   at 261.6Hz, but the *loudest* bin was an inharmonic partial at 988Hz, with secondary inharmonic
   partials forming a decoy series. Autocorrelation keys on the signal's *period*, which is
   correct regardless of spectral tilt. It also specifically avoids latching onto a period
   *multiple* (an octave-too-low subharmonic error) by preferring the smallest lag that's a local
   correlation max at ≥85% of the peak correlation, not just the single highest-correlation lag.
6. **Peak-picking** (`pickPeaks`) — local maxima in the *reference* spectrum within 60dB of that
   spectrum's own loudest bin, with close peaks (within 3 bins) merged (keeping the louder). The
   **test** spectrum is read at the exact same bin indices the reference peaks were found at —
   deliberately, not at the parabolically-refined continuous frequency, because reading test's
   spectrum at a slightly different frequency than where ref's peak actually is would compare two
   different points even for identical signals.
7. **Log-spectral distance** — mean `|dB(ref) - dB(test)|` across all bins (excluding DC), the one
   number to watch while iterating on a synth's DSP.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--ref <path>` | yes | Reference WAV (ground truth — typically real hardware). |
| `--test <path>` | yes | Test WAV to compare against the reference (typically `m8_render` output). |
| `--no-align` | no | Skip per-file onset detection; both files are read from sample 0 (still skips 50ms for the attack-transient exclusion, just measured from file start rather than detected onset). |
| `--json <path>` | no | Also write a machine-readable report. |

## JSON schema

```jsonc
{
  "ref": "...", "test": "...", "sample_rate": 48000, "window_samples": N, "aligned": true,
  "onset_ref_samples": N, "onset_test_samples": N,
  "fundamental_ref_hz": 0.0, "fundamental_test_hz": 0.0, "fundamental_ok": true,
  "centroid_ref_hz": 0.0, "centroid_test_hz": 0.0,
  "log_spectral_distance_db": 0.0,
  "harmonics": [
    { "freq_hz": 0.0, "ref_db": 0.0, "test_db": 0.0, "delta_db": 0.0 }
  ]
}
```
`fundamental_ok` is `true` iff both fundamentals were detected (nonzero) and are within ~2%
(~34 cents) of each other — this is the field to check programmatically for "same pitch," but
remember it does not affect the exit code.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success — **including when the spectra don't match.** Read the output, don't rely on the exit code for a pass/fail verdict. |
| 2 | Usage error (missing `--ref`/`--test`), unreadable file, sample-rate mismatch between ref/test, or the sustained portion after onset+attack-skip is too short to analyze (< 2048 samples) |

## Examples

```
m8_spectrum --ref m8_capture.wav --test my_render.wav

m8_spectrum --ref ref.wav --test mine.wav --no-align --json diff.json
```

## Gotchas

- **Not a CI gate by itself.** Since it always exits 0 on success regardless of match quality, a
  script that wants a hard pass/fail must parse the JSON's `fundamental_ok` and/or threshold
  `log_spectral_distance_db` itself.
- **Requires matching sample rates** between ref and test (hard error, exit 2, if they differ) —
  it does not resample.
- **`--no-align` still skips 50ms of attack**, it only disables the per-file onset *detection*
  (both files are then read from sample 0 instead of a detected onset before that 50ms skip is
  applied). It is not a way to compare the attack transients themselves.
- The autocorrelation pitch detector (`findFundamentalAcf`) searches 50Hz-2000Hz only — sounds
  outside that fundamental range won't be found (`fundamental_*_hz` reports 0, `fundamental_ok`
  is false).
- Harmonic-table peaks are always picked from the **reference** spectrum; the test spectrum is
  only ever read at those same bins for comparison — a peak that exists in test but not in ref
  (e.g. test has extra harmonic content) will not appear in the `harmonics` table at all.
