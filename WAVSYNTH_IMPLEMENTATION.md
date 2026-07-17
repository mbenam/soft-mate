# WavSynth Implementation Plan (Phase 1: 9 Base Shapes)

## Overview

A wavetable synthesizer that generates waveforms in real-time from 9 basic shapes, with size/mult/warp/scan controls. Wavetables (shapes 9+) deferred to a later phase.

Based on the M8 WavSynth manual section.

---

## Files to modify/create

| File | Change |
|------|--------|
| `src/engine/Engine.h` | Add `INST_WAVSYNTH`, `WavSynthState` struct, member in `Instrument` |
| `src/engine/SynthVoice.h` | Add WavSynth phase accumulator and waveform buffer |
| `src/engine/SynthVoice.cpp` | WavSynth waveform generation + render path |
| `src/engine/CommandRing.h` | Add `WAV_*` ParamIDs |
| `src/engine/EngineStateUpdater.h` | WavSynth parameter routing in the common-param ternary chain |
| `src/engine/Engine.cpp` | WavSynth case in `render()` pan/dry/sends, `tickTrack()` transpose |
| `src/io/SongIO.cpp` | WavSynth load/save/new in `convertSongToEngine()`, `convertEngineToSong()`, `buildSongFromEngine()` |
| `tests/test_wavsynth.cpp` | New test file |

---

## 1. `Engine.h` -- Data structures

Add `INST_WAVSYNTH` to the enum (between `INST_FMSYNTH` and `INST_MIDI`):

```cpp
enum class InstType { INST_SAMPLER, INST_MACROSYN, INST_HYPERSYN, INST_FMSYNTH, INST_WAVSYNTH, INST_MIDI, INST_NONE };
```

Add `WavSynthState` after `FMSynthState`:

```cpp
struct WavSynthState {
    int transp = 1;        // 0 = OFF, 1 = ON
    int tbl_tic = 0xFF;
    int eq = 0;            // 0 = --

    int shape = 0;         // WavShape: 0=Pulse12..8=Noise (9 base shapes), 9+=wavetable index
    int size = 0x80;       // horizontal size (0-255, controls waveform width / # samples)
    int mult = 0x00;       // multiplication / repeat count (hard-sync-like effect)
    int warp = 0x80;       // push shape to one side (0=left, 0x80=center, 0xFF=right)
    int scan = 0x00;       // on base shapes: mirror position (0-200%); on wavetables: morph (later)

    int filter_type = 0;   // 0=OFF, 1=LP, 2=HP, 3=BP, 4=BS, 5=LP>HP, 6=ZDF LP, 7=ZDF HP,
                           // 8=WAV LP, 9=WAV HP, 10=WAV BP, 11=WAV BS
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

Add `WavSynthState wav;` to `Instrument` struct (after `FMSynthState fm;`):

```cpp
struct Instrument {
    InstType type = InstType::INST_SAMPLER;
    char name[13] = "------------";
    SamplerState sampler;
    MacrosynState macrosyn;
    HyperState hyper;
    FMSynthState fm;
    WavSynthState wav;
    Modulator mods[4];
};
```

---

## 2. `SynthVoice.h` -- WavSynth voice state

Add after the FM members (after `static float readFMWavetable(const float* table, float phase);`):

```cpp
// WavSynth state
static constexpr int kWavBufSize = 2048;
float m_wavBuf[kWavBufSize] = {};      // generated waveform buffer
int m_wavBufLen = 0;                    // active waveform length (<= kWavBufSize)
uint32_t m_wavPhase = 0;               // phase accumulator (32-bit, same as HyperSynth)
void generateWavShape(const WavSynthState& ws, float noteFreq);
static float readWavBuf(const float* buf, int len, float phase);
```

---

## 3. `SynthVoice.cpp` -- WavSynth DSP

### 3a. Waveform generation (`generateWavShape()`)

Called once per note-on (or when params change). Generates a single cycle of the selected base shape into `m_wavBuf[]`, applying SIZE, MULT, WARP, and MIRROR (SCAN).

The 9 base shapes (indices 0-8 from `WavShape` enum):

| Index | Name | Shape |
|-------|------|-------|
| 0 | Pulse12 | Pulse 12% duty cycle |
| 1 | Pulse25 | Pulse 25% duty cycle |
| 2 | Pulse50 | Pulse 50% duty cycle (square) |
| 3 | Pulse75 | Pulse 75% duty cycle |
| 4 | Saw | Sawtooth (linear ramp 0 to 1) |
| 5 | Triangle | Triangle (linear ramp -1 to 1 to -1) |
| 6 | Sine | Pure sine wave |
| 7 | NoisePitched | LFSR noise seeded by phase position |
| 8 | Noise | White noise (deterministic per-sample) |

**SIZE** controls the number of unique samples in one cycle:

    waveLen = clamp(size * kWavBufSize / 256, 4, kWavBufSize)

**MULT** repeats the base shape `mult+1` times within the cycle:

    repeats = 1 + clamp(mult, 0, 15)   // 1-16 repeats

**WARP** shifts the waveform horizontally:

    warpOffset = (warp - 128) / 128.0f  // -1.0 to +1.0

Applied as a phase offset to the waveform lookup.

**SCAN** (on base shapes 0-8) mirrors the waveform at a given position:

    mirrorPos = scan / 200.0f  // 0.0 to 1.0 normalized position

For each sample at position `t`: if `t > mirrorPos`, the output mirrors from `mirrorPos`.

Generation sketch:

```cpp
void SynthVoice::generateWavShape(const WavSynthState& ws, float noteFreq) {
    int shape = std::clamp(ws.shape, 0, 8);
    int waveLen = std::clamp((ws.size * kWavBufSize) / 256, 4, kWavBufSize);
    int repeats = 1 + std::clamp(ws.mult, 0, 15);
    float warpShift = (ws.warp - 128) / 128.0f;  // -1..+1
    float mirrorPos = ws.scan / 200.0f;            // 0..1

    for (int i = 0; i < waveLen; ++i) {
        float t = float(i) / float(waveLen);        // 0..1 position in cycle
        float shaped_t = std::fmod(t * repeats + warpShift, 1.0f);
        if (shaped_t < 0.0f) shaped_t += 1.0f;

        float val = 0.0f;
        switch (shape) {
        case 0: val = (shaped_t < 0.12f) ? 1.0f : -1.0f; break;  // Pulse12
        case 1: val = (shaped_t < 0.25f) ? 1.0f : -1.0f; break;  // Pulse25
        case 2: val = (shaped_t < 0.50f) ? 1.0f : -1.0f; break;  // Pulse50
        case 3: val = (shaped_t < 0.75f) ? 1.0f : -1.0f; break;  // Pulse75
        case 4: // Saw
            val = 2.0f * shaped_t - 1.0f;
            break;
        case 5: // Triangle
            val = (shaped_t < 0.5f) ? (4.0f * shaped_t - 1.0f) : (3.0f - 4.0f * shaped_t);
            break;
        case 6: // Sine
            val = std::sin(shaped_t * 6.2831853f);
            break;
        case 7: { // NoisePitched -- LFSR seeded by position, gives tonal pitch
            uint32_t seed = static_cast<uint32_t>(i * repeats) * 2654435761u;
            seed ^= seed >> 13;
            val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
            break;
        }
        case 8: { // Noise -- pure white
            uint32_t seed = static_cast<uint32_t>(i) * 2654435761u;
            seed ^= seed >> 13;
            val = (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
            break;
        }
        }

        // MIRROR (SCAN on base shapes)
        if (t > mirrorPos && mirrorPos > 0.0f) {
            float mirrorT = 2.0f * mirrorPos - t;
            if (mirrorT >= 0.0f) {
                float mt = std::fmod(mirrorT * repeats + warpShift, 1.0f);
                if (mt < 0.0f) mt += 1.0f;
                float mirrorVal = 0.0f;
                switch (shape) {
                case 0: mirrorVal = (mt < 0.12f) ? 1.0f : -1.0f; break;
                case 1: mirrorVal = (mt < 0.25f) ? 1.0f : -1.0f; break;
                case 2: mirrorVal = (mt < 0.50f) ? 1.0f : -1.0f; break;
                case 3: mirrorVal = (mt < 0.75f) ? 1.0f : -1.0f; break;
                case 4: mirrorVal = 2.0f * mt - 1.0f; break;
                case 5: mirrorVal = (mt < 0.5f) ? (4.0f * mt - 1.0f) : (3.0f - 4.0f * mt); break;
                case 6: mirrorVal = std::sin(mt * 6.2831853f); break;
                default: mirrorVal = val; break;
                }
                val = mirrorVal;
            }
        }

        m_wavBuf[i] = std::clamp(val, -1.0f, 1.0f);
    }
    m_wavBufLen = waveLen;
}
```

**Optimization note**: The mirror re-evaluation inside the generation loop is acceptable because `waveLen` is at most 2048 and this runs once per note-on, not per sample. If profiling shows it matters, pre-generate unmirrored, then apply mirror in a second pass.

### 3b. Wavetable read (linear interpolation)

```cpp
float SynthVoice::readWavBuf(const float* buf, int len, float phase) {
    float idx = phase * len;
    int i = static_cast<int>(idx) & (kWavBufSize - 1);
    float frac = idx - std::floor(idx);
    int next = (i + 1) & (kWavBufSize - 1);
    return buf[i] * (1.0f - frac) + buf[next] * frac;
}
```

### 3c. Render path in `renderSample()`

Insert WavSynth block after the FM block (after `for (int i = 0; i < 4; ++i) m_fmPrevOut[i] = opOut[i];`), before the final volume/gate:

```cpp
bool isWav = false;

if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
    isWav = true;
    const WavSynthState& ws = m_instrument->wav;

    float noteFreq = m_frequency * std::pow(2.0f, mt.pitch / 12.0f);

    // Regenerate waveform buffer
    generateWavShape(ws, noteFreq);

    // Apply WAV filter modes (8-11) into the waveform buffer
    if (ws.filter_type >= 8 && ws.filter_type <= 11) {
        float baseCutoff = 20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f);
        float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
        float finalRes = std::clamp(ws.res / 255.0f + mt.res, 0.0f, 1.0f);
        m_filter.SetFreq(finalCutoff);
        m_filter.SetRes(finalRes);
        for (int i = 0; i < m_wavBufLen; ++i) {
            m_filter.Process(m_wavBuf[i]);
            switch (ws.filter_type) {
            case 8:  m_wavBuf[i] = m_filter.Low(); break;
            case 9:  m_wavBuf[i] = m_filter.High(); break;
            case 10: m_wavBuf[i] = m_filter.Band(); break;
            case 11: m_wavBuf[i] = m_wavBuf[i] - m_filter.Band(); break;
            }
        }
    }

    // Phase accumulation
    float incF = noteFreq / kSampleRate;
    m_wavPhase += static_cast<uint32_t>(incF * 4294967296.0f);
    float phase01 = static_cast<float>(m_wavPhase) / 4294967296.0f;

    sample = readWavBuf(m_wavBuf, m_wavBufLen, phase01);
}
```

Update the fallback condition:

```cpp
if (!isBraids && !isHyper && !isFM && !isWav) {
    m_osc.SetFreq(m_frequency * std::pow(2.0f, mt.pitch / 12.0f));
    sample = m_osc.Process();
}
```

### 3d. DSP tail (amp/filter/limiter)

After the WavSynth oscillator block, apply the common DSP tail (same pattern as FMSynth/HyperSynth):

```cpp
if (isWav) {
    const WavSynthState& ws = m_instrument->wav;
    float ampVal = std::clamp(1.0f + (ws.amp / 255.0f) * 7.0f + mt.amp * 7.0f, 0.0f, 8.0f);
    int limMode = ws.lim;
    int filterType = ws.filter_type;
    float baseCutoff = 20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f);
    float finalCutoff = std::clamp(baseCutoff * std::pow(2.0f, mt.cutoff * 5.0f), 20.0f, 20000.0f);
    float finalRes = std::clamp(ws.res / 255.0f + mt.res, 0.0f, 1.0f);

    // For WAV filter modes (8-11), filter is already applied to the waveform.
    // Only apply standard filter for modes 0-7.
    int stdFilter = (filterType >= 8) ? 0 : filterType;

    if (limMode < 4) {
        sample *= ampVal;
        sample = applyLimiter(sample, limMode);
        if (stdFilter > 0) sample = applyFilter(sample, stdFilter, finalCutoff, finalRes);
    } else {
        if (stdFilter > 0) sample = applyFilter(sample, stdFilter, finalCutoff, finalRes);
        sample *= ampVal;
        sample = applyLimiter(sample, limMode);
    }
}
```

### 3e. `noteOn()` changes

Add WavSynth waveform generation in `SynthVoice::noteOn()`:

```cpp
if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
    m_wavPhase = 0;
    generateWavShape(m_instrument->wav, m_frequency);
}
```

---

## 4. `CommandRing.h` -- ParamIDs

Add after `FM_MOD4`:

```cpp
// WavSynth Specific
WAV_SHAPE, WAV_SIZE, WAV_MULT, WAV_WARP, WAV_SCAN,
```

---

## 5. `EngineStateUpdater.h` -- Parameter routing

### Add `isWav` bool

After line 104 (`const bool isFm = ...`):

```cpp
const bool isWav = (inst.type == InstType::INST_WAVSYNTH);
```

### Update common-param cases

Every common-param case that uses the `isHyp`/`isMac`/`isFm` ternary needs `isWav` inserted. For example:

```cpp
case ParamID::INST_AMP:
    if (isHyp) inst.hyper.amp = cmd.value;
    else if (isMac) inst.macrosyn.amp = cmd.value;
    else if (isFm) inst.fm.amp = cmd.value;
    else if (isWav) inst.wav.amp = cmd.value;
    else inst.sampler.amp = cmd.value;
    break;
```

Same pattern for: `INST_TRANSP`, `INST_TBL_TIC`, `INST_EQ`, `INST_LIM`, `INST_PAN`, `INST_DRY`, `INST_CHO`, `INST_DEL`, `INST_REV`, `INST_FILTER`, `INST_CUTOFF`, `INST_RES`.

### Add INST_TYPE name

```cpp
else if (inst.type == InstType::INST_WAVSYNTH) setName(inst.name, "WAVSYNTH    ");
```

### Add WavSynth-specific cases

After the FM-specific cases:

```cpp
// WavSynth Specific
case ParamID::WAV_SHAPE: inst.wav.shape = cmd.value; break;
case ParamID::WAV_SIZE: inst.wav.size = cmd.value; break;
case ParamID::WAV_MULT: inst.wav.mult = cmd.value; break;
case ParamID::WAV_WARP: inst.wav.warp = cmd.value; break;
case ParamID::WAV_SCAN: inst.wav.scan = cmd.value; break;
```

---

## 6. `Engine.cpp` -- Output routing + transpose

### `render()` -- pan/dry/sends

Add WavSynth case after the FMSynth case:

```cpp
else if (inst.type == InstType::INST_WAVSYNTH) {
    pan = inst.wav.pan / 255.0f;
    dry = inst.wav.dry / 255.0f;
    cho = inst.wav.cho / 255.0f;
    del = inst.wav.del / 255.0f;
    rev = inst.wav.rev / 255.0f;
}
```

### `tickTrack()` -- transpose

Add WavSynth case:

```cpp
else if (currentInst->type == InstType::INST_WAVSYNTH && currentInst->wav.transp == 0)
    effTranspose = 0;
```

---

## 7. `SongIO.cpp` -- Load / Save / New

### Load (`convertSongToEngine`, after the `m8::FMSynth` branch)

```cpp
else if constexpr (std::is_same_v<T, m8::WavSynth>) {
    engInst.type = engine::InstType::INST_WAVSYNTH;
    engine::setName(engInst.name, inst.name.c_str());
    auto& ws = engInst.wav;
    ws.transp  = inst.transpose ? 1 : 0;
    ws.tbl_tic = inst.table_tick;
    ws.shape   = static_cast<int>(inst.shape);
    ws.size    = inst.size;
    ws.mult    = inst.mult;
    ws.warp    = inst.warp;
    ws.scan    = inst.scan;
    ws.amp         = inst.synth_params.volume;
    ws.filter_type = inst.synth_params.filter_type;
    ws.cutoff      = inst.synth_params.filter_cutoff;
    ws.res         = inst.synth_params.filter_res;
    ws.lim         = inst.synth_params.amp_type;
    ws.pan         = inst.synth_params.mixer_pan;
    ws.dry         = inst.synth_params.mixer_dry;
    ws.cho         = inst.synth_params.mixer_chorus;
    ws.del         = inst.synth_params.mixer_delay;
    ws.rev         = inst.synth_params.mixer_reverb;
    for (int m = 0; m < 4; ++m)
        libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
}
```

### Save (`convertEngineToSong`, after the FMSynth save block)

```cpp
else if (engInst.type == engine::InstType::INST_WAVSYNTH &&
         std::holds_alternative<m8::WavSynth>(song.instruments[i])) {
    auto& wvs = std::get<m8::WavSynth>(song.instruments[i]);
    const auto& ws = engInst.wav;
    wvs.transpose  = (ws.transp != 0);
    wvs.table_tick = static_cast<uint8_t>(ws.tbl_tic);
    wvs.shape      = static_cast<m8::WavShape>(ws.shape);
    wvs.size       = static_cast<uint8_t>(ws.size);
    wvs.mult       = static_cast<uint8_t>(ws.mult);
    wvs.warp       = static_cast<uint8_t>(ws.warp);
    wvs.scan       = static_cast<uint8_t>(ws.scan);
    wvs.synth_params.volume        = static_cast<uint8_t>(ws.amp);
    wvs.synth_params.filter_type   = static_cast<uint8_t>(ws.filter_type);
    wvs.synth_params.filter_cutoff = static_cast<uint8_t>(ws.cutoff);
    wvs.synth_params.filter_res    = static_cast<uint8_t>(ws.res);
    wvs.synth_params.amp_type      = static_cast<uint8_t>(ws.lim);
    wvs.synth_params.mixer_pan     = static_cast<uint8_t>(ws.pan);
    wvs.synth_params.mixer_dry     = static_cast<uint8_t>(ws.dry);
    wvs.synth_params.mixer_chorus  = static_cast<uint8_t>(ws.cho);
    wvs.synth_params.mixer_delay   = static_cast<uint8_t>(ws.del);
    wvs.synth_params.mixer_reverb  = static_cast<uint8_t>(ws.rev);
    for (int m = 0; m < 4; ++m)
        wvs.synth_params.mods[m] = engineModToLib(engInst.mods[m]);
}
```

### New song (`buildSongFromEngine`, before the `else` branch)

```cpp
else if (e.type == engine::InstType::INST_WAVSYNTH) {
    m8::WavSynth wvs{};
    wvs.number = static_cast<uint8_t>(i);
    wvs.name = trimName(e.name);
    wvs.shape = m8::WavShape::Sine;
    wvs.size = 0x80;
    wvs.mult = 0x00;
    wvs.warp = 0x80;
    wvs.scan = 0x00;
    wvs.synth_params = {};
    wvs.synth_params.mixer_pan = 0x80;
    for (int k = 0; k < 4; ++k)
        wvs.synth_params.mods[k] = engineModToLib(e.mods[k]);
    wvs.synth_params.associated_eq = 0xFF;
    base.instruments[i] = wvs;
}
```

---

## 8. `tests/test_wavsynth.cpp` -- Tests

New test file using Catch2 (same framework as other tests).

```cpp
#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("WavSynth renders all 9 base shapes without NaN", "[wavsynth]") {
    for (int shape = 0; shape < 9; ++shape) {
        DYNAMIC_SECTION("Shape " << shape) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_WAVSYNTH;
            auto& ws = state.instruments[0].wav;
            ws.shape = shape;
            ws.size = 0x80; ws.mult = 0x00; ws.warp = 0x80; ws.scan = 0x00;
            ws.amp = 0x40; ws.lim = 0; ws.filter_type = 0;
            ws.dry = 0xC0; ws.pan = 0x80;

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

TEST_CASE("WavSynth different shapes produce different output", "[wavsynth]") {
    auto renderWav = [](int shape) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = shape; ws.size = 0x80; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSine = renderWav(6);
    float sumSaw = renderWav(4);
    REQUIRE(sumSine != sumSaw);
}

TEST_CASE("WavSynth SIZE parameter changes output", "[wavsynth]") {
    auto renderWavSize = [](int size) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = size; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSmall = renderWavSize(0x20);
    float sumLarge = renderWavSize(0xF0);
    REQUIRE(sumSmall != sumLarge);
}

TEST_CASE("WavSynth MULT parameter changes output", "[wavsynth]") {
    auto renderWavMult = [](int mult) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = 0x80; ws.mult = mult; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumNoMult = renderWavMult(0x00);
    float sumHighMult = renderWavMult(0x0F);
    REQUIRE(sumNoMult != sumHighMult);
}

TEST_CASE("WavSynth RT safety -- zero allocations", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 6; ws.size = 0x80; ws.amp = 0x40;
    ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;

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
.\build\Release\m8_tests.exe "[wavsynth]" --reporter compact
.\build\Release\m8_tests.exe --reporter compact   # full suite at the end
```

---

## 10. Known limitations / placeholders

- **Wavetable shapes (indices 9-70)**: The 61 built-in wavetables with SCAN morphing are not implemented. Shape index 9+ aliases to Sine. The `WavShape` enum values are defined in the library but the waveform data is not present -- each wavetable is 64 waveforms x 64 samples, which requires hardware dumps or reverse-engineered generation.
- **WAV filter modes (8-11)**: Filter-in-waveform is implemented by filtering the waveform buffer in-place. This is an approximation -- the real M8 may use a different topology (e.g. per-sample filter with feedback into the wavetable read position).
- **PolyBLEP on saw/pulse edges**: The base shapes use raw discontinuities. The sharp edges of Pulse/Saw may produce slight aliasing at high frequencies. A polyBLEP correction can be added later if needed.
- **SIZE interaction with MULT**: When MULT > 0, the effective waveform is `size / (mult+1)` samples per repeat. The current implementation handles this correctly via `shaped_t = fmod(t * repeats + warpShift, 1.0f)`.
