# Repository Documentation Index

| File | Kind | Status | Summary |
|---|---|---|---|
| `AGENTS.md` | reference | active | Working agreement, architecture rules, hardware-verified constants |
| `ARCHITECTURE.md` | reference | active | System architecture, thread boundaries, data structures, code critique |
| `status.md` | reference | active | Master status tracking of implemented vs spec'd features |
| `docs/README.md` | reference | active | Master documentation index (this file) |
| `docs/tools/README.md` | reference | active | CLI tools index, architecture, and flag verification script guide |
| `docs/tools/agent_guide.md` | reference | active | Operational guide for driving real M8 hardware over serial |
| `docs/tools/hw_findings.md` | findings | active | Append-only record of hardware measurements and empirical findings |
| `docs/tools/m8_*.md` (9 files) | reference | active | Tool docs (`analyze`, `capture`, `composesong`, `diffcheck`, `makeprobe`, `makesong`, `nav`, `render`, `spectrum`) |
| `songs/README.md` | reference | active | Demo song structures, asset paths, and playback instructions |
| `src/engine/braids/README.md` | reference | active | Braids oscillator port notes and licensing |
| `tests/fixtures/measurements/unverified/README.md` | reference | active | Quarantined measurement records lacking verified inputs |
| `specs/CODE_CLEANUP_SPEC.md` | plan | active | Codebase refactoring and hygiene tracking |
| `specs/FMSYNTH_IMPLEMENTATION.md` | plan | active | 4-operator FM synth implementation plan |
| `specs/FX_COMMANDS_SPEC.md` | plan | active | FX command execution matrix and value models |
| `specs/M8_APP_AUTOMATION_SPEC.md` | plan | active | Headless script harness and CI test coverage |
| `specs/M8_DEVICE_CONTROL_SPEC.md` | plan | active | Closed-loop device driver & framebuffer verification plan |
| `specs/M8_DRIVER_BUGS.md` | plan | active | Chronological log of driver bugs and hardware findings |
| `specs/M8_EVIDENCE_REPAIR_SPEC.md` | plan | active | Hardware measurement re-taking plan (X5-X7 blocked pending hardware) |
| `specs/M8_HARDWARE_TEST_SPEC.md` | plan | active | Real-hardware test rig integration plan |
| `specs/M8_MEASUREMENT_EVIDENCE_SPEC.md` | plan | active | Reproducible measurement artifacts plan (W4-W5 invalidated) |
| `specs/M8_SAMPLER_COMPLETION_SPEC.md` | plan | active | Sampler instrument feature completion plan |
| `specs/TABLE_IMPLEMENTATION.md` | plan | active | Table execution engine implementation plan |
| `specs/WAVSYNTH_IMPLEMENTATION.md` | plan | active | Real-time wavetable synth implementation plan |
| `archive/BUG_TESTFILE_DC_DRONE.md` | plan | complete-archived | Archived fix record for DC drone bug |
| `archive/M8_AUDIO_ANALYSIS_SPEC.md` | plan | complete-archived | Audio analysis & A/B spectrum tools spec (see `docs/tools/m8_analyze.md`) |
| `archive/M8_CAPTURE_SPEC.md` | plan | complete-archived | Hardware audio capture spec (see `docs/tools/m8_capture.md`) |
| `archive/M8_DRIVER_ADDENDUM_A.md` | plan | complete-archived | Driver gap fixes addendum (see `docs/tools/m8_nav.md`) |
| `archive/M8_DRIVER_SPEC.md` | plan | complete-archived | Device driver spec (see `docs/tools/m8_nav.md`) |
| `archive/M8_MEASUREMENT_VALIDITY_SPEC.md` | plan | complete-archived | Measurement validity spec (see `docs/tools/m8_analyze.md`) |
| `archive/M8_PARAM_RANGE_SPEC.md` | plan | complete-archived | Parameter range spec (see `status.md` & `ParamRange.h`) |
| `archive/M8_PERSISTENCE_SPEC.md` | plan | complete-archived | `.m8s` load/save persistence spec (see `src/io/SongIO.cpp`) |
| `archive/M8_PROBE_AUTHORING_SPEC.md` | plan | complete-archived | Probe authoring spec (see `docs/tools/m8_makeprobe.md`) |
| `archive/M8_UI_HARNESS_SPEC.md` | plan | complete-archived | UI test harness spec (see `tests/test_ui_scripts.cpp`) |
| `archive/TESTING.MD` | plan | complete-archived | Early test guidelines (superseded-by-ARCHITECTURE.md) |
| `docs/ui_screen_spec.md` | reference | active | UI screen ground rules: 40x30 grid, what captures are and aren't for, out-of-scope items. Read before screen work|
