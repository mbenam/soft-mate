# FM Synth Implementation Plan

## Overview

A 4-operator, 12-algorithm FM synthesizer. Each operator has a wavetable oscillator with shape, ratio, level, feedback, and 2 mod slots for per-operator modulation routing.

Based on the M8 hardware FM synth manual (see `FMSYNTH_MANUAL.md` for reference).

---

## Files to modify/create

| File | Change |
|------|--------|
| `src/engine/Engine.h` | Add `INST_FMSYNTH`, `FMSynthState` struct, member in `Instrument` |
| `src/engine/SynthVoice.h` | Add FM wavetable storage, phase accumulators |
| `src/engine/SynthVoice.cpp` | FM wavetable generation + render path + algorithm routing |
| `src/engine/CommandRing.h` | Add `FM_*` ParamIDs |
| `src/engine/EngineStateUpdater.h` | FM parameter routing in the common-param ternary chain |
| `src/engine/Engine.cpp` | FM case in `render()` pan/dry/sends, `tickTrack()` transpose |
| `src/io/SongIO.cpp` | FM load/save in `convertSongToEngine()`, `convertEngineToSong()`, `buildSongFromEngine()` |
| `tests/test_fmsynth.cpp` | New test file |

---

## 1. `Engine.h` -- Data structures

Add `INST_FMSYNTH` to the enum (between `INST_HYPERSYN` and `INST_MIDI`):

```cpp
enum class InstType { INST_SAMPLER, INST_MACROSYN, INST_HYPERSYN, INST_FMSYNTH, INST_MIDI, INST_NONE };
```

Add `FMSynthState` after `HyperState`:

```cpp
struct FMSynthState {
    int transp = 1;        // 0 = OFF, 1 = ON
    int tbl_tic = 0xFF;
    int eq = 0;            // 0 = --
    int algo = 0;          // 0-11 (12 algorithms)

    struct FMOp {
        int shape = 0;     // FMWave: 0=SIN..11=NOISE, 12+=wavetable index
        int ratio = 0;     // integer part of frequency ratio (0-15)
        int ratio_fine = 0;// fractional part (0-255, combined: ratio + ratio_fine/256)
        int level = 0;     // output level (0-255)
        int feedback = 0;  // self-feedback amount (0-255)
        int retrigger = 1; // phase retrigger on note-on (0=free, 1=retrigger)
        int mod_a = 0;     // per-operator mod slot A (encoding below)
        int mod_b = 0;     // per-operator mod slot B (encoding below)
    };

    FMOp ops[4];           // operators A, B, C, D

    int mod1 = 0x80;       // mod macro 1 value (0-255)
    int mod2 = 0x80;       // mod macro 2 value
    int mod3 = 0x80;       // mod macro 3 value
    int mod4 = 0x80;       // mod macro 4 value

    // Common tail (same layout as MacrosynState/HyperState)
    int filter_type = 0;   // 0=OFF, 1=LP, 2=HP, 3=BP, 4=BS, 5=LP>HP, 6=ZDF LP, 7=ZDF HP
    int cutoff = 0xFF;
    int res = 0x00;
    int amp = 0x00;
    int lim = 0;           // 0=CLIP, 1=SIN, 2=FOLD, 3=WRAP, 4=POST, 5=POST:AD
    int pan = 0x80;
    int dry = 0xC0;
    int cho = 0x00;
    int del = 0x00;
    int rev = 0x00;
};
```

Add `FMSynthState fm;` to `Instrument` struct (after `HyperState hyper;`):

```cpp
struct Instrument {
    InstType type = InstType::INST_SAMPLER;
    char name[13] = "------------";
    SamplerState sampler;
    MacrosynState macrosyn;
    HyperState hyper;
    FMSynthState fm;
    Modulator mods[4];
};
```

### Per-operator mod_a / mod_b encoding

```
Bits 3:0 = destination:  0=none, 1=LEV, 2=RAT, 3=PIT, 4=FBK
Bits 7:4 = mod macro source index (0=mod1, 1=mod2, 2=mod3, 3=mod4, 4+=none)
```

Decode: `dest = mod_x & 0x0F; source = (mod_x >> 4) & 0x0F;`

---

## 2. `SynthVoice.h` -- FM voice state

Add after the HyperSynth members (after line 87):

```cpp
// FM Synth state
static constexpr int kFMWavetableSize = 2048;
static constexpr int kFMNumShapes = 12; // SIN, SW2, SW3, SW4, SW5, SW6, TRI, SAW, SQU, PUL, IMP, NOISE
float m_fmWavetable[kFMNumShapes][kFMWavetableSize] = {};
bool m_fmWavetableReady = false;
float m_fmPhase[4] = {};          // phase accumulators for 4 operators
float m_fmPrevOut[4] = {};        // previous outputs (for self-feedback)
void initFMWavetables();
static float readFMWavetable(const float* table, float phase);
```

---

## 3. `SynthVoice.cpp` -- FM DSP

### 3a. Wavetable generation (`initFMWavetables()`)

Called once (lazily on first FM render). Generates 2048-sample wavetables for 12 base shapes.

Shape definitions (all normalized to [-1, 1]):

| Index | Name | Description |
|-------|------|-------------|
| 0 | SIN | Pure sine wave |
| 1 | SW2 | Sine + 2nd harmonic (half-fold appearance) |
| 2 | SW3 | Sine + 2nd + 3rd harmonics (two-bump) |
| 3 | SW4 | Sine + 2nd + 3rd + 4th harmonics (three-bump) |
| 4 | SW5 | Asymmetric fold (sine + phase-shifted 2nd harmonic) |
| 5 | SW6 | Two-bump asymmetric (sine + 3rd harmonic) |
| 6 | TRI | Triangle wave (Fourier series) |
| 7 | SAW | Sawtooth wave (Fourier series) |
| 8 | SQU | Square wave (Fourier series) |
| 9 | PUL | Pulse 25% duty cycle |
| 10 | IMP | Single-sample impulse (1.0 at phase 0, 0 elsewhere) |
| 11 | NOISE | White noise (deterministic per-phase for wavetable consistency) |

Generation code sketch:

```cpp
void SynthVoice::initFMWavetables() {
    if (m_fmWavetableReady) return;
    constexpr float TWO_PI = 6.2831853f;
    for (int s = 0; s < kFMNumShapes; ++s) {
        for (int i = 0; i < kFMWavetableSize; ++i) {
            float phase = float(i) / float(kFMWavetableSize);
            float val = 0.0f;
            switch (s) {
            case 0: // SIN
                val = std::sin(phase * TWO_PI);
                break;
            case 1: // SW2 -- sine + 2nd harmonic at 0.5 amp
                val = std::sin(phase * TWO_PI) + 0.5f * std::sin(phase * TWO_PI * 2.0f);
                break;
            case 2: // SW3 -- sine + 2nd + 3rd
                val = std::sin(phase * TWO_PI)
                    + 0.4f * std::sin(phase * TWO_PI * 2.0f)
                    + 0.2f * std::sin(phase * TWO_PI * 3.0f);
                break;
            case 3: // SW4 -- sine + 2nd + 3rd + 4th
                val = std::sin(phase * TWO_PI)
                    + 0.35f * std::sin(phase * TWO_PI * 2.0f)
                    + 0.2f * std::sin(phase * TWO_PI * 3.0f)
                    + 0.1f * std::sin(phase * TWO_PI * 4.0f);
                break;
            case 4: // SW5 -- asymmetric fold
                val = std::sin(phase * TWO_PI)
                    + 0.6f * std::sin(phase * TWO_PI * 2.0f + 0.5f);
                break;
            case 5: // SW6 -- two-bump asymmetric
                val = std::sin(phase * TWO_PI)
                    + 0.3f * std::sin(phase * TWO_PI * 3.0f);
                break;
            case 6: { // TRI
                // Fourier series: 8/(pi^2) * sum of (-1)^n * sin((2n+1)x) / (2n+1)^2
                float t = 0.0f;
                for (int h = 0; h < 16; ++h) {
                    int n = 2 * h + 1;
                    t += ((h % 2 == 0 ? 1.0f : -1.0f) * std::sin(phase * TWO_PI * float(n))) / float(n * n);
                }
                val = t * 8.0f / (TWO_PI * TWO_PI);
                break;
            }
            case 7: { // SAW
                float t = 0.0f;
                for (int h = 1; h <= 32; ++h)
                    t += std::sin(phase * TWO_PI * float(h)) / float(h);
                val = -2.0f / PI * t;
                break;
            }
            case 8: { // SQU
                float t = 0.0f;
                for (int h = 0; h < 16; ++h) {
                    int n = 2 * h + 1;
                    t += std::sin(phase * TWO_PI * float(n)) / float(n);
                }
                val = 4.0f / PI * t;
                break;
            }
            case 9: // PUL (25% duty)
                val = (phase < 0.25f) ? 1.0f : -1.0f;
                break;
            case 10: // IMP
                val = (i == 0) ? 1.0f : 0.0f;
                break;
            case 11: // NOISE (deterministic from phase)
                {
                    uint32_t seed = static_cast<uint32_t>(i * 2654435761u);
                    seed ^= seed >> 13;
                    val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
                }
                break;
            }
            // Normalize shapes with harmonics to [-1, 1]
            if (s >= 1 && s <= 5) val *= 0.5f;  // prevent clipping from stacked harmonics
            m_fmWavetable[s][i] = std::clamp(val, -1.0f, 1.0f);
        }
    }
    m_fmWavetableReady = true;
}
```

### 3b. Wavetable read (linear interpolation)

```cpp
float SynthVoice::readFMWavetable(const float* table, float phase) {
    // phase is 0.0 to 1.0
    float idx = phase * kFMWavetableSize;
    int i = static_cast<int>(idx) & (kFMWavetableSize - 1);
    float frac = idx - std::floor(idx);
    int next = (i + 1) & (kFMWavetableSize - 1);
    return table[i] * (1.0f - frac) + table[next] * frac;
}
```

### 3c. FM constants

```cpp
static constexpr float kFMModIndex = 6.0f;   // max phase deviation (radians) for FM modulation
static constexpr float kFMFeedbackScale = 3.14159265f; // PI -- feedback phase deviation
```

### 3d. Per-operator mod decoding and application

```cpp
struct FMOpParams {
    float freq;        // effective frequency (Hz)
    float level;       // effective level (0.0 to 1.0)
    float feedback;    // effective feedback (0.0 to 1.0)
    int shape;         // wavetable shape index
};

static void decodeFMOpMod(uint8_t mod_byte, const float mod_values[4],
                           int base_level, int base_ratio, int base_ratio_fine,
                           int base_feedback, float noteFreq,
                           FMOpParams& out) {
    int dest = mod_byte & 0x0F;
    int src = (mod_byte >> 4) & 0x0F;
    float mod_val = (src < 4) ? (mod_values[src] - 128.0f) / 128.0f : 0.0f; // bipolar

    float effLevel = base_level / 255.0f;
    float effRatio = float(base_ratio) + float(base_ratio_fine) / 256.0f;
    float effFeedback = base_feedback / 255.0f;

    switch (dest) {
    case 1: effLevel *= (mod_val + 1.0f) * 0.5f; break;        // LEV: scale 0..base
    case 2: effRatio += mod_val * 16.0f; break;                  // RAT: add 0..+16
    case 3: /* PIT: applied later as semitone offset */ break;
    case 4: effFeedback *= (mod_val + 1.0f) * 0.5f; break;     // FBK: scale 0..base
    }

    out.level = std::clamp(effLevel, 0.0f, 1.0f);
    out.feedback = std::clamp(effFeedback, 0.0f, 1.0f);
    out.freq = noteFreq * effRatio;
}
```

### 3e. Algorithm routing

The 12 algorithms define modulator/carrier topology. For each sample:

1. Advance all 4 phase accumulators
2. Compute each operator's output in dependency order (modulators before carriers)
3. For each operator: `output = wavetable(phase + feedback_contribution + modulator_sum) * level`
4. Sum carrier outputs, normalize

**Modulation**: modulator_output * level * kFMModIndex is added to carrier's phase.

**Feedback**: previous_output * feedback * kFMFeedbackScale is added to same operator's phase.

```cpp
float computeFMOp(float phase, float mod_phase_offset, float feedback,
                   float prev_out, const float* wavetable) {
    float fb = prev_out * feedback * kFMFeedbackScale;
    float effective_phase = phase + mod_phase_offset + fb;
    // Wrap to [0, 1)
    effective_phase -= std::floor(effective_phase);
    return readFMWavetable(wavetable, effective_phase);
}
```

Algorithm implementations (A=0, B=1, C=2, D=3):

```
Algo 00: A>B>C>D           Carriers: D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outB * lvlB)
  outD = op(D, outC * lvlC)
  output = outD

Algo 01: [A+B]>C>D          Carriers: D
  outA = op(A)
  outB = op(B)
  outC = op(C, (outA*lvlA + outB*lvlB))
  outD = op(D, outC * lvlC)
  output = outD

Algo 02: [A>B+C]>D          Carriers: D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C)
  outD = op(D, outB*lvlB + outC*lvlC)
  output = outD

Algo 03: [A>B + A>C]>D      Carriers: D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outA * lvlA)
  outD = op(D, outB*lvlB + outC*lvlC)
  output = outD

Algo 04: [A+B+C]>D          Carriers: D
  outA = op(A)
  outB = op(B)
  outC = op(C)
  outD = op(D, outA*lvlA + outB*lvlB + outC*lvlC)
  output = outD

Algo 05: [A>B>C] + D        Carriers: C, D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outB * lvlB)
  outD = op(D)
  output = (outC*lvlC + outD*lvlD) / 2

Algo 06: [A>B>C] + [A>B>D]  Carriers: C, D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outB * lvlB)
  outD = op(D, outB * lvlB)
  output = (outC*lvlC + outD*lvlD) / 2

Algo 07: [A>B] + [C>D]      Carriers: B, D
  outA = op(A)
  outC = op(C)
  outB = op(B, outA * lvlA)
  outD = op(D, outC * lvlC)
  output = (outB*lvlB + outD*lvlD) / 2

Algo 08: [A>B] + [A>C] + [A>D]  Carriers: B, C, D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outA * lvlA)
  outD = op(D, outA * lvlA)
  output = (outB*lvlB + outC*lvlC + outD*lvlD) / 3

Algo 09: [A>B] + [A>C] + D  Carriers: B, C, D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C, outA * lvlA)
  outD = op(D)
  output = (outB*lvlB + outC*lvlC + outD*lvlD) / 3

Algo 0A: [A>B] + C + D      Carriers: B, C, D
  outA = op(A)
  outB = op(B, outA * lvlA)
  outC = op(C)
  outD = op(D)
  output = (outB*lvlB + outC*lvlC + outD*lvlD) / 3

Algo 0B: A + B + C + D      Carriers: A, B, C, D (additive)
  outA = op(A)
  outB = op(B)
  outC = op(C)
  outD = op(D)
  output = (outA*lvlA + outB*lvlB + outC*lvlC + outD*lvlD) / 4
```

### 3f. Render path in `renderSample()`

Insert FM block after the HyperSynth block (after line 323 of current code), before the fallback `if (!isBraids && !isHyper)`:

```cpp
bool isFM = false;
if (m_instrument && m_instrument->type == InstType::INST_FMSYNTH) {
    isFM = true;
    if (!m_fmWavetableReady) initFMWavetables();

    const FMSynthState& fm = m_instrument->fm;

    // Decode mod macro values (from instrument mod slots, same as other synths)
    float mod_values[4] = { float(fm.mod1), float(fm.mod2), float(fm.mod3), float(fm.mod4) };

    // Compute operator params (frequency, level, feedback) with per-op mod routing
    FMOpParams opParams[4];
    for (int i = 0; i < 4; ++i) {
        const auto& op = fm.ops[i];
        float noteFreq = m_frequency * std::pow(2.0f, mt.pitch / 12.0f);
        decodeFMOpMod(op.mod_a, mod_values, op.level, op.ratio, op.ratio_fine,
                       op.feedback, noteFreq, opParams[i]);
        // Apply mod_b (slot B) additively
        FMOpParams tmp{};
        decodeFMOpMod(op.mod_b, mod_values, op.level, op.ratio, op.ratio_fine,
                       op.feedback, noteFreq, tmp);
        // Merge: take the stronger modulation from each slot
        // (simplified: average the two slot contributions)
        opParams[i].level = 0.5f * (opParams[i].level + tmp.level);
        opParams[i].feedback = std::max(opParams[i].feedback, tmp.feedback);
        opParams[i].shape = op.shape;
    }

    // Advance phases and compute outputs in algorithm order
    float opOut[4] = {};
    for (int i = 0; i < 4; ++i) {
        float inc = opParams[i].freq / kSampleRate;
        m_fmPhase[i] += inc;
        m_fmPhase[i] -= std::floor(m_fmPhase[i]); // wrap to [0, 1)
    }

    // Algorithm routing (see table above)
    // ... (switch on fm.algo, compute opOut[0..3] with modulation)

    // Sum carriers based on algorithm
    // ... (normalize and assign to `sample`)

    // Store previous outputs for feedback
    for (int i = 0; i < 4; ++i) m_fmPrevOut[i] = opOut[i];
}

if (!isBraids && !isHyper && !isFM) {
    // existing polyBLEP fallback
    m_osc.SetFreq(m_frequency * std::pow(2.0f, mt.pitch / 12.0f));
    sample = m_osc.Process();
}
```

After the FM block, apply the common DSP tail (same pattern as HyperSynth):

```cpp
if (isFM) {
    const FMSynthState& fm = m_instrument->fm;
    float ampVal = std::clamp(1.0f + (fm.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
    int limMode = fm.lim;
    int filterType = fm.filter_type;
    float baseCutoff = 20.0f * std::pow(2.0f, (fm.cutoff / 255.0f) * 10.0f);
    float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
    float finalRes = std::clamp(fm.res / 255.0f + mt.res, 0.0f, 1.0f);

    if (limMode < 4) {
        sample *= ampVal;
        sample = applyLimiter(sample, limMode);
        sample = applyFilter(sample, filterType, finalCutoff, finalRes);
    } else {
        sample = applyFilter(sample, filterType, finalCutoff, finalRes);
        sample *= ampVal;
        sample = applyLimiter(sample, limMode);
    }
}
```

### 3g. `noteOn()` changes

Add FM phase reset in `SynthVoice::noteOn()` (after line 42):

```cpp
if (m_instrument && m_instrument->type == InstType::INST_FMSYNTH) {
    for (int i = 0; i < 4; ++i) {
        if (m_instrument->fm.ops[i].retrigger) {
            m_fmPhase[i] = 0.0f;
            m_fmPrevOut[i] = 0.0f;
        }
    }
}
```

---

## 4. `CommandRing.h` -- ParamIDs

Add after `MAC_REDUX` (line 54):

```cpp
// FMSynth Specific
FM_ALGO,
FM_OP_SHAPE, FM_OP_RATIO, FM_OP_RATIO_FINE, FM_OP_LEVEL, FM_OP_FB,
FM_OP_RETRIG, FM_OP_MOD_A, FM_OP_MOD_B,
FM_MOD1, FM_MOD2, FM_MOD3, FM_MOD4,
```

---

## 5. `EngineStateUpdater.h` -- Parameter routing

### Add `isFm` bool

After line 103 (`const bool isHyp = ...`):

```cpp
const bool isFm = (inst.type == InstType::INST_FMSYNTH);
```

### Update common-param cases

Every common-param case that uses the `isHyp`/`isMac` ternary needs `isFm` inserted. For example (line 117):

```cpp
case ParamID::INST_AMP:
    if (isHyp) inst.hyper.amp = cmd.value;
    else if (isMac) inst.macrosyn.amp = cmd.value;
    else if (isFm) inst.fm.amp = cmd.value;
    else inst.sampler.amp = cmd.value;
    break;
```

Same pattern for: `INST_TRANSP`, `INST_TBL_TIC`, `INST_EQ`, `INST_LIM`, `INST_PAN`, `INST_DRY`, `INST_CHO`, `INST_DEL`, `INST_REV`, `INST_FILTER`, `INST_CUTOFF`, `INST_RES`.

### Add INST_TYPE name

After line 111:

```cpp
else if (inst.type == InstType::INST_FMSYNTH) setName(inst.name, "FMSYNTH     ");
```

### Add FM-specific cases

After the `MAC_REDUX` case (line 140):

```cpp
// FMSynth Specific
case ParamID::FM_ALGO: inst.fm.algo = cmd.value; break;
case ParamID::FM_OP_SHAPE: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].shape = cmd.value; break;
case ParamID::FM_OP_RATIO: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].ratio = cmd.value; break;
case ParamID::FM_OP_RATIO_FINE: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].ratio_fine = cmd.value; break;
case ParamID::FM_OP_LEVEL: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].level = cmd.value; break;
case ParamID::FM_OP_FB: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].feedback = cmd.value; break;
case ParamID::FM_OP_RETRIG: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].retrigger = cmd.value; break;
case ParamID::FM_OP_MOD_A: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].mod_a = cmd.value; break;
case ParamID::FM_OP_MOD_B: if (cmd.row >= 0 && cmd.row < 4) inst.fm.ops[cmd.row].mod_b = cmd.value; break;
case ParamID::FM_MOD1: inst.fm.mod1 = cmd.value; break;
case ParamID::FM_MOD2: inst.fm.mod2 = cmd.value; break;
case ParamID::FM_MOD3: inst.fm.mod3 = cmd.value; break;
case ParamID::FM_MOD4: inst.fm.mod4 = cmd.value; break;
```

Note: The FM-specific cases should be placed inside the bounds-checked block (after `auto& inst = state.instruments[cmd.targetId];`), alongside the SAMPLER and MACROSYN specific cases. The existing structure already has all these cases inside the same `if (cmd.targetId < 0 || ...) break;` guard.

---

## 6. `Engine.cpp` -- Output routing + transpose

### `render()` -- pan/dry/sends (line 458-478)

Add FM case after the HyperSynth case:

```cpp
else if (inst.type == InstType::INST_FMSYNTH) {
    pan = inst.fm.pan / 255.0f;
    dry = inst.fm.dry / 255.0f;
    cho = inst.fm.cho / 255.0f;
    del = inst.fm.del / 255.0f;
    rev = inst.fm.rev / 255.0f;
}
```

### `tickTrack()` -- transpose (line 314-319)

Add FM case:

```cpp
else if (currentInst->type == InstType::INST_FMSYNTH && currentInst->fm.transp == 0)
    effTranspose = 0;
```

---

## 7. `SongIO.cpp` -- Load / Save / New

### Load (`convertSongToEngine`, line 327-410)

Add `m8::FMSynth` branch after the `m8::HyperSynth` branch:

```cpp
else if constexpr (std::is_same_v<T, m8::FMSynth>) {
    engInst.type = engine::InstType::INST_FMSYNTH;
    engine::setName(engInst.name, inst.name.c_str());
    auto& fm = engInst.fm;
    fm.transp  = inst.transpose ? 1 : 0;
    fm.tbl_tic = inst.table_tick;
    fm.algo    = static_cast<int>(inst.algo);
    for (int i = 0; i < 4; ++i) {
        fm.ops[i].shape      = static_cast<int>(inst.operators[i].shape);
        fm.ops[i].ratio      = inst.operators[i].ratio;
        fm.ops[i].ratio_fine = inst.operators[i].ratio_fine;
        fm.ops[i].level      = inst.operators[i].level;
        fm.ops[i].feedback   = inst.operators[i].feedback;
        fm.ops[i].retrigger  = inst.operators[i].retrigger;
        fm.ops[i].mod_a      = inst.operators[i].mod_a;
        fm.ops[i].mod_b      = inst.operators[i].mod_b;
    }
    fm.mod1 = inst.mod1;
    fm.mod2 = inst.mod2;
    fm.mod3 = inst.mod3;
    fm.mod4 = inst.mod4;
    fm.amp         = inst.synth_params.volume;
    fm.filter_type = inst.synth_params.filter_type;
    fm.cutoff      = inst.synth_params.filter_cutoff;
    fm.res         = inst.synth_params.filter_res;
    fm.lim         = inst.synth_params.amp_type;
    fm.pan         = inst.synth_params.mixer_pan;
    fm.dry         = inst.synth_params.mixer_dry;
    fm.cho         = inst.synth_params.mixer_chorus;
    fm.del         = inst.synth_params.mixer_delay;
    fm.rev         = inst.synth_params.mixer_reverb;
    for (int m = 0; m < 4; ++m)
        libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
}
```

### Save (`convertEngineToSong`, line 494-558)

Add FM case after the HyperSynth save block:

```cpp
else if (engInst.type == engine::InstType::INST_FMSYNTH &&
         std::holds_alternative<m8::FMSynth>(song.instruments[i])) {
    auto& fms = std::get<m8::FMSynth>(song.instruments[i]);
    const auto& fm = engInst.fm;
    fms.transpose  = (fm.transp != 0);
    fms.table_tick = static_cast<uint8_t>(fm.tbl_tic);
    fms.algo       = static_cast<m8::FmAlgo>(fm.algo);
    for (int k = 0; k < 4; ++k) {
        fms.operators[k].shape      = static_cast<m8::FMWave>(fm.ops[k].shape);
        fms.operators[k].ratio      = static_cast<uint8_t>(fm.ops[k].ratio);
        fms.operators[k].ratio_fine = static_cast<uint8_t>(fm.ops[k].ratio_fine);
        fms.operators[k].level      = static_cast<uint8_t>(fm.ops[k].level);
        fms.operators[k].feedback   = static_cast<uint8_t>(fm.ops[k].feedback);
        fms.operators[k].retrigger  = static_cast<uint8_t>(fm.ops[k].retrigger);
        fms.operators[k].mod_a      = static_cast<uint8_t>(fm.ops[k].mod_a);
        fms.operators[k].mod_b      = static_cast<uint8_t>(fm.ops[k].mod_b);
    }
    fms.mod1 = static_cast<uint8_t>(fm.mod1);
    fms.mod2 = static_cast<uint8_t>(fm.mod2);
    fms.mod3 = static_cast<uint8_t>(fm.mod3);
    fms.mod4 = static_cast<uint8_t>(fm.mod4);
    fms.synth_params.volume        = static_cast<uint8_t>(fm.amp);
    fms.synth_params.filter_type   = static_cast<uint8_t>(fm.filter_type);
    fms.synth_params.filter_cutoff = static_cast<uint8_t>(fm.cutoff);
    fms.synth_params.filter_res    = static_cast<uint8_t>(fm.res);
    fms.synth_params.amp_type      = static_cast<uint8_t>(fm.lim);
    fms.synth_params.mixer_pan     = static_cast<uint8_t>(fm.pan);
    fms.synth_params.mixer_dry     = static_cast<uint8_t>(fm.dry);
    fms.synth_params.mixer_chorus  = static_cast<uint8_t>(fm.cho);
    fms.synth_params.mixer_delay   = static_cast<uint8_t>(fm.del);
    fms.synth_params.mixer_reverb  = static_cast<uint8_t>(fm.rev);
    for (int m = 0; m < 4; ++m)
        fms.synth_params.mods[m] = engineModToLib(engInst.mods[m]);
}
```

### New song (`buildSongFromEngine`, line 576-614)

Add FM case before the `else` branch:

```cpp
else if (e.type == engine::InstType::INST_FMSYNTH) {
    m8::FMSynth fms{};
    fms.number = static_cast<uint8_t>(i);
    fms.name = trimName(e.name);
    fms.algo = m8::FmAlgo::Algo0;
    for (int k = 0; k < 4; ++k) {
        fms.operators[k].shape = m8::FMWave::Sin;
        fms.operators[k].ratio = 1;
        fms.operators[k].ratio_fine = 0;
        fms.operators[k].level = 0xFF;
        fms.operators[k].feedback = 0;
        fms.operators[k].retrigger = 1;
        fms.operators[k].mod_a = 0;
        fms.operators[k].mod_b = 0;
    }
    fms.mod1 = 0x80; fms.mod2 = 0x80; fms.mod3 = 0x80; fms.mod4 = 0x80;
    fms.synth_params = {};
    fms.synth_params.mixer_pan = 0x80;
    for (int k = 0; k < 4; ++k)
        fms.synth_params.mods[k] = engineModToLib(e.mods[k]);
    fms.synth_params.associated_eq = 0xFF;
    base.instruments[i] = fms;
}
```

---

## 8. `tests/test_fmsynth.cpp` -- Tests

New test file using Catch2 (same framework as other tests).

```cpp
#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

// Helper: set up an FM instrument and render
static float renderFM(int algo, int shape, int level, int ratio, int feedback, int samples = 1000) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_FMSYNTH;
    auto& fm = state.instruments[0].fm;
    fm.algo = algo;
    fm.ops[0].shape = shape; fm.ops[0].level = level;
    fm.ops[0].ratio = ratio; fm.ops[0].feedback = feedback;
    fm.ops[1].shape = shape; fm.ops[1].level = level;
    fm.ops[1].ratio = ratio; fm.ops[1].feedback = feedback;
    fm.ops[2].shape = shape; fm.ops[2].level = level;
    fm.ops[2].ratio = ratio; fm.ops[2].feedback = feedback;
    fm.ops[3].shape = shape; fm.ops[3].level = level;
    fm.ops[3].ratio = ratio; fm.ops[3].feedback = feedback;
    fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
    fm.pan = 0x80; fm.dry = 0xC0;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(samples);

    float sum = 0;
    for (float v : host.audio()) sum += std::abs(v);
    return sum;
}

TEST_CASE("FMSynth renders all 12 algorithms without NaN", "[fmsynth]") {
    for (int algo = 0; algo < 12; ++algo) {
        DYNAMIC_SECTION("Algorithm " << algo) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_FMSYNTH;
            auto& fm = state.instruments[0].fm;
            fm.algo = algo;
            fm.ops[0].shape = 0; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
            fm.ops[1].shape = 0; fm.ops[1].level = 0x80; fm.ops[1].ratio = 2;
            fm.ops[2].shape = 0; fm.ops[2].level = 0x80; fm.ops[2].ratio = 3;
            fm.ops[3].shape = 0; fm.ops[3].level = 0x80; fm.ops[3].ratio = 4;
            fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
            fm.dry = 0xC0; fm.pan = 0x80;

            g_allocCount = 0;
            setStep(host.sequencer(), 0, 0, 60, 100, 0);
            host.push(playPhrase(0, 0, 0));
            host.render(1000);

            REQUIRE(g_allocCount == 0);

            const auto& a = host.audio();
            bool hasNonZero = false;
            for (float val : a) {
                REQUIRE(std::isfinite(val));
                REQUIRE(val >= -1.05f);
                REQUIRE(val <= 1.05f);
                if (std::abs(val) > 0.0001f) hasNonZero = true;
            }
            REQUIRE(hasNonZero);
        }
    }
}

TEST_CASE("FMSynth different algorithms produce different output", "[fmsynth]") {
    float sum00 = renderFM(0, 0, 0x80, 1, 0);
    float sum0B = renderFM(0xB, 0, 0x80, 1, 0);
    REQUIRE(sum00 != sum0B);
}

TEST_CASE("FMSynth different shapes produce different output", "[fmsynth]") {
    float sumSIN = renderFM(0, 0, 0x80, 1, 0);
    float sumSAW = renderFM(0, 7, 0x80, 1, 0);
    REQUIRE(sumSIN != sumSAW);
}

TEST_CASE("FMSynth feedback increases complexity", "[fmsynth]") {
    float sumNoFB = renderFM(0, 0, 0x80, 1, 0);
    float sumHighFB = renderFM(0, 0, 0x80, 1, 0xFF);
    REQUIRE(sumHighFB != sumNoFB);
}

TEST_CASE("FMSynth RT safety -- zero allocations", "[fmsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_FMSYNTH;
    auto& fm = state.instruments[0].fm;
    fm.algo = 0; fm.ops[0].shape = 0; fm.ops[0].level = 0x80;
    fm.ops[0].ratio = 1; fm.amp = 0x40; fm.lim = 0;
    fm.filter_type = 0; fm.dry = 0xC0; fm.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
```

---

## 9. Build and test

```powershell
cmake -B build -A x64
cmake --build build --config Release --target m8_clone
cmake --build build --config Release --target m8_tests
.\build\Release\m8_tests.exe "[fmsynth]" --reporter compact
.\build\Release\m8_tests.exe --reporter compact   # full suite at the end
```

---

## 10. Known limitations / placeholders

- **Wavetable indices 0x12-0x45** beyond the 12 base shapes: Aliased to SIN. The real M8 stores 60+ wavetables in flash; without that data, procedural generation of that many distinct wavetables is not worth the effort.
- **Per-operator mod routing encoding** (mod_a/mod_b bit layout): Based on community reverse-engineering, not hardware-verified. The encoding is: bits 3:0 = destination (0=off, 1=LEV, 2=RAT, 3=PIT, 4=FBK), bits 7:4 = mod macro source (0=mod1..3=mod3, 4+=none).
- **Mod macro 1-4 values**: The engine's standard modulator slots produce these. The file's `mod1-mod4` are the same outputs stored redundantly. On load, we use the file values; at runtime, the engine's mod processing overwrites them.
- **Level scaling for modulation**: Uses a fixed modulation index constant (`kFMModIndex = 6.0f`). May need tuning against hardware recordings.
- **Dual mod slots (mod_a + mod_b)**: Merged by averaging level and maxing feedback. A more precise model would apply both slots independently to their respective destinations.
- **PIT per-operator destination**: Not fully implemented in the decode function -- marked as a TODO in the code. Needs semitone-to-frequency conversion.
