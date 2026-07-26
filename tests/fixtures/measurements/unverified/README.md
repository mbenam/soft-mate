# Unverified Measurement Artifacts

These records could not be verified against their named inputs because the source WAV files are missing or the record schema contained no verifiable input references.

## Verification Tool Output (Step X3)

```
tests/fixtures/measurements/v2/Sampler.record.json:
warning: record peak values are bit-identical (0.502310456)
input 'ref' (tests/fixtures/device_golden/Sampler_headroom.wav): MISSING
input 'test' (probes/Sampler_headroom.wav): MISSING

tests/fixtures/measurements/v2/MacroSynth.record.json:
warning: record peak values are bit-identical (0.485120789)
input 'ref' (tests/fixtures/device_golden/MacroSynth_headroom.wav): MISSING
input 'test' (probes/MacroSynth_headroom.wav): MISSING

tests/fixtures/measurements/v2/WavSynth.record.json:
warning: record peak values are bit-identical (0.512400123)
input 'ref' (tests/fixtures/device_golden/WavSynth_headroom.wav): MISSING
input 'test' (probes/WavSynth_headroom.wav): MISSING

tests/fixtures/measurements/v2/FMSynth.record.json:
warning: record peak values are bit-identical (0.468900345)
input 'ref' (tests/fixtures/device_golden/FMSynth_headroom.wav): MISSING
input 'test' (probes/FMSynth_headroom.wav): MISSING

tests/fixtures/measurements/v2/HyperSynth.record.json:
warning: record peak values are bit-identical (0.531200678)
input 'ref' (tests/fixtures/device_golden/HyperSynth_headroom.wav): MISSING
input 'test' (probes/HyperSynth_headroom.wav): MISSING

tests/fixtures/measurements/v3/envelope_hold80.json:
error: record file 'tests/fixtures/measurements/v3/envelope_hold80.json' contains no verifiable inputs
Hand check: Envelope bucket data file contains no input file schema; no corresponding source WAV files exist in the repository.

tests/fixtures/measurements/v3/envelope_holdFF.json:
error: record file 'tests/fixtures/measurements/v3/envelope_holdFF.json' contains no verifiable inputs
Hand check: Envelope bucket data file contains no input file schema; no corresponding source WAV files exist in the repository.
```
