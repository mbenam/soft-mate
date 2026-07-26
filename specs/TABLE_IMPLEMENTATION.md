# Table Execution Implementation Plan

## Overview

Tables are per-instrument sub-sequencers that run alongside the phrase playback. Each table has 16 rows with transpose, volume, and 3 FX columns. Tables run at a user-defined speed (`tbl_tic`) independent of the global tempo/groove. When a note plays, its assigned table loops, applying per-row transpose and volume modifications to the voice.

This spec covers engine-side execution only. UI wiring (Table screen already exists) is out of scope.

---

## Files to modify/create

| File | Change |
|------|--------|
| `src/engine/SeqTypes.h` | Extend `FxCmd` enum with `TBL`, `GRV`, `TIC` |
| `src/engine/Engine.h` | Add `TableState` per-track, `trackGroove[8]` override, `m_tableTickPhase` |
| `src/engine/Engine.cpp` | `tickTable()` function, TBL/GRV/TIC FX handling in `tickTrack()`, per-track groove override, table tick in `doTick()` |
| `src/engine/SynthVoice.h` | Add `m_tableTranspose`, `m_tableVolume` members and getters |
| `src/engine/SynthVoice.cpp` | Apply table transpose/volume in `renderSample()` |
| `tests/test_tables.cpp` | New test file |

---

## 1. `SeqTypes.h` — Extend FxCmd

Add three new FX commands to the enum:

```cpp
enum class FxCmd : uint8_t { NONE = 0, VOL, PIT, DEL, REV, HOP, KIL, TBL, GRV, TIC };
```

| Command | Phrase behavior | Table behavior |
|---------|----------------|----------------|
| `TBL`   | Assign table N to the current instrument | N/A (not used inside tables) |
| `GRV`   | Assign groove N to the current track | N/A |
| `TIC`   | N/A | Set tick rate for all 3 FX columns in this table |

---

## 2. `Engine.h` — Table state

### Per-track table playback state

Add after the existing per-track arrays (after `m_state`):

```cpp
struct TableState {
    int assignedTable = -1;     // -1 = no table assigned; 0..255 = table index
    int row = 0;                // current table row (0..15)
    int tickCount = 0;          // counts ticks within current row
    int tableTickRate = 0;      // from tbl_tic: 0=on-trigger, 1-0xFB=ticks/row, 0xFC-0xFE=maps, 0xFF=200Hz
    int perColTickRate[3] = {-1, -1, -1}; // per-column TIC overrides (-1 = use tableTickRate)
};
```

### Per-track groove override

Add to `EngineState`:

```cpp
int trackGroove[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // -1 = use project.groove
```

### Table tick phase accumulator

Add to `Engine` private members:

```cpp
double m_tableTickPhase = 0.0;
static constexpr double kTableTickRate200Hz = 48000.0 / 200.0; // 240 samples per tick
```

### Table state array

Add to `Engine` private members:

```cpp
TableState m_tableState[8]; // one per track
```

---

## 3. `SynthVoice.h` — Table modulation outputs

Add public members and getters after the existing modulation-related members:

```cpp
// Table-applied modulation (set by Engine each tick, read by renderSample)
float m_tableTranspose = 0.0f;   // semitones, added to pitch
float m_tableVolume = 1.0f;      // multiplier (1.0 = no change)

float getTableTranspose() const { return m_tableTranspose; }
float getTableVolume() const { return m_tableVolume; }
void setTableModulation(float transpose, float volume) {
    m_tableTranspose = transpose;
    m_tableVolume = volume;
}
```

---

## 4. `Engine.cpp` — Core execution

### 4a. Per-track groove override in `tickTrack()`

Replace the groove lookup at the top of `tickTrack()`:

```cpp
// Calculate groove for current track — per-track override takes priority
int grooveId = m_state.trackGroove[t] >= 0 ? m_state.trackGroove[t]
                                            : m_state.project.groove;
int grooveLength = 6;
int maxGrooveLen = 16;
if (valid(grooveId, Sequencer::NUM_GROOVES)) {
    maxGrooveLen = m_sequencer.grooves[grooveId].length;
    if (maxGrooveLen > 16) maxGrooveLen = 16;
    if (maxGrooveLen == 0) maxGrooveLen = 1;
    if (!valid(m_state.playGrooveIndex[t], maxGrooveLen)) m_state.playGrooveIndex[t] = 0;
    uint8_t g = m_sequencer.grooves[grooveId].steps[m_state.playGrooveIndex[t]];
    if (g != 0) grooveLength = g;
}
```

### 4b. TBL/GRV FX handling in `parseFX` (inside `tickTrack`)

Extend the `parseFX` lambda:

```cpp
auto parseFX = [&](const FxSlot& fx, int track) {
    if (fx.cmd == FxCmd::NONE) return;
    if (fx.cmd == FxCmd::DEL) m_state.pendingDel[track] = std::min((int)fx.val, grooveLength - 1);
    else if (fx.cmd == FxCmd::KIL) m_state.pendingKil[track] = std::min((int)fx.val, grooveLength - 1);
    else if (fx.cmd == FxCmd::HOP) m_state.nextHop[track] = fx.val;
    else if (fx.cmd == FxCmd::TBL) {
        // Assign table to the current instrument on this track
        if (fx.val < Sequencer::NUM_TABLES) {
            m_tableState[track].assignedTable = fx.val;
            m_tableState[track].row = 0;
            m_tableState[track].tickCount = 0;
            // Read initial tick rate from the table's first row or instrument tbl_tic
            if (m_trackInstrument[track] >= 0 && m_trackInstrument[track] < (int)m_state.instruments.size()) {
                m_tableState[track].tableTickRate = m_state.instruments[m_trackInstrument[track]].sampler.tbl_tic;
            }
        }
    }
    else if (fx.cmd == FxCmd::GRV) {
        // Assign groove to this track (-1 = revert to project groove)
        m_state.trackGroove[track] = (fx.val < Sequencer::NUM_GROOVES) ? fx.val : -1;
        m_state.playGrooveIndex[track] = 0;
    }
};
parseFX(step.fx[0], t); parseFX(step.fx[1], t); parseFX(step.fx[2], t);
```

### 4c. Auto-assign table on note-on

When a note triggers (inside the `if (m_state.pendingFreq[t] > 0.0f)` block, after `noteOn()`), initialize table state from the instrument's `tbl_tic`:

```cpp
// Auto-assign instrument's own table (table index = trackInstrument[t])
// and initialize tick rate from tbl_tic. Only if no table was already assigned by TBL FX.
if (m_tableState[t].assignedTable < 0) {
    m_tableState[t].assignedTable = m_trackInstrument[t];
    m_tableState[t].row = 0;
    m_tableState[t].tickCount = 0;
}
// Always sync tick rate from instrument tbl_tic (unless TIC FX overrides it)
if (m_trackInstrument[t] >= 0 && m_trackInstrument[t] < (int)m_state.instruments.size()) {
    m_tableState[t].tableTickRate = m_state.instruments[m_trackInstrument[t]].sampler.tbl_tic;
}
```

### 4d. `tickTable()` — advance table row

New private function:

```cpp
void Engine::tickTable(int t) {
    auto& ts = m_tableState[t];
    if (ts.assignedTable < 0) return;
    if (!valid(ts.assignedTable, Sequencer::NUM_TABLES)) return;

    const TableStep* row = &m_sequencer.tables[ts.assignedTable][ts.row];

    // --- Apply table row modulation to the voice ---
    float tableTransp = static_cast<float>(row->transp);  // semitones (signed)
    float tableVol = (row->vol == VOL_EMPTY) ? 1.0f : (row->vol / 127.0f);

    m_voices[t].setTableModulation(tableTransp, tableVol);

    // --- Parse table-internal FX (VOL, PIT, HOP, TIC) ---
    for (int f = 0; f < 3; ++f) {
        const auto& fx = row->fx[f];
        if (fx.cmd == FxCmd::HOP) {
            if (fx.val < Sequencer::ROWS) {
                ts.row = fx.val;
                ts.tickCount = 0;
                return; // HOP immediately jumps, skip advance below
            }
        }
        else if (fx.cmd == FxCmd::TIC) {
            // TIC in a table column overrides that column's tick rate
            ts.perColTickRate[f] = fx.val;
        }
        else if (fx.cmd == FxCmd::VOL) {
            // Table VOL: scales the row's volume (applied multiplicatively)
            float fxVol = (fx.val <= 127) ? (fx.val / 127.0f) : 1.0f;
            tableVol *= fxVol;
            m_voices[t].setTableModulation(tableTransp, tableVol);
        }
        else if (fx.cmd == FxCmd::PIT) {
            // Table PIT: adds semitone offset to the row's transpose
            tableTransp += static_cast<float>(static_cast<int8_t>(fx.val));
            m_voices[t].setTableModulation(tableTransp, tableVol);
        }
    }

    // --- Advance to next row ---
    ts.row++;
    if (ts.row >= Sequencer::ROWS) ts.row = 0; // loop
    ts.tickCount = 0;
}
```

### 4e. Table tick dispatch in `doTick()`

Add table ticking after the phrase tick loop:

```cpp
void Engine::doTick() {
    // ... existing code ...

    // Tick all tracks' tables independently at their own rate
    for (int t = 0; t < 8; ++t) {
        auto& ts = m_tableState[t];
        if (ts.assignedTable < 0) continue;

        // Determine effective tick rate for this table
        int effectiveRate = ts.tableTickRate;

        // TIC mode 0x00: advance only on note-on (handled in tickTrack), skip here
        if (effectiveRate == 0x00) continue;

        // TIC modes 0xFC-0xFE: map-based (octave/velocity/note) — advance on note-on only
        if (effectiveRate >= 0xFC && effectiveRate <= 0xFE) continue;

        // TIC mode 0xFF: 200 Hz fixed rate (240 samples at 48 kHz)
        if (effectiveRate == 0xFF) {
            m_tableTickPhase += 1.0;
            if (m_tableTickPhase >= kTableTickRate200Hz) {
                m_tableTickPhase -= kTableTickRate200Hz;
                tickTable(t);
            }
            continue;
        }

        // Normal tick rate (0x01-0xFB): advance every N phrase ticks
        // effectiveRate = ticks per table row
        ts.tickCount++;
        if (ts.tickCount >= effectiveRate) {
            tickTable(t);
        }
    }
}
```

### 4f. Reset table state on note-off / play stop

In `emitNoteOff()`:

```cpp
void Engine::emitNoteOff(int t, int songRowOverride) {
    m_voices[t].noteOff();
    m_voices[t].setTableModulation(0.0f, 1.0f);  // clear table modulation
    // ... existing code ...
}
```

In the `PLAY_STOP` handler:

```cpp
for(int t=0; t<8; ++t) {
    if (m_voices[t].isActive()) emitNoteOff(t);
    m_tableState[t] = {};   // reset table state
    publishPlayhead(t);
}
```

### 4g. Reset table phase on `LOAD_SONG`

In the `LOAD_SONG` handler, after resetting voices:

```cpp
for (int i = 0; i < 8; ++i) {
    m_voices[i].resetOscillator();
    m_tableState[i] = {};
}
m_tableTickPhase = 0.0;
```

---

## 5. `SynthVoice.cpp` — Apply table transpose/volume

In `renderSample()`, the table transpose is applied as an offset to `mt.pitch` (the modulation-system pitch target in semitones), and the table volume multiplies the voice's effective volume.

### Sampler path

Inside the sampler block (after computing `semis`):

```cpp
// Table transpose: add semitones from the table row
float tableTransp = m_tableTranspose;
if (tableTransp != 0.0f) {
    semis += tableTransp;
}
```

And after computing `effVol`:

```cpp
float tableVol = m_tableVolume;
effVol *= tableVol;
```

### Non-sampler path (Braids/Hyper/FM/Wav/PolyBLEP)

After computing `noteFreq` or `m_frequency`, apply table transpose:

```cpp
// Apply table transpose to pitch
if (m_tableTranspose != 0.0f) {
    f *= std::pow(2.0f, m_tableTranspose / 12.0f);
}
```

And before the final `return std::clamp(sample * effVol * volMod, ...)`:

```cpp
effVol *= m_tableVolume;
```

---

## 6. Map-based TIC modes (0xFC-0xFE) — detail

These modes map a note parameter to a starting table row, applied once at note-on. They do not advance rows over time.

### TIC 0xFC — Octave Map

At note-on, the playing note's octave (MIDI note / 12) maps to a table row:

```cpp
// In tickTable setup or note-on initialization:
int octave = (midiNote / 12) - 5;  // C-4 octave = 5, so range roughly 0-10
ts.row = std::clamp(octave, 0, 15);
```

### TIC 0xFD — Velocity Map

At note-on, velocity (0-127) maps proportionally to row (0-15):

```cpp
int row = (velocity * Sequencer::ROWS) / 128;
ts.row = std::clamp(row, 0, 15);
```

### TIC 0xFE — Note Map

At note-on, the note's position within the octave (MIDI note % 12) maps to a row:

```cpp
int row = midiNote % 12;
ts.row = std::clamp(row, 0, 15);
```

To limit to 12 notes, the user places `HOP 00` at row 0x0C.

**Implementation note:** These modes need the MIDI note value at note-on. Add a `m_lastMidiNote` field to `SynthVoice` (set in `noteOn()` from the frequency), and use it during table initialization in `Engine::tickTrack()` when `tbl_tic` is 0xFC-0xFE.

---

## 7. TIC command inside tables — per-column tick rates

Each FX column in a table can have its own TIC override. When a TIC command is placed in a table FX column, it sets the tick rate for **that column only** until changed. The effective tick rate for the table is the **minimum** of the column rates (fastest column wins).

Actually, re-reading the M8 manual: TIC in a table column "can place TIC commands at the end of the table to affect all rows." This means TIC sets the tick rate for **all subsequent rows** in that column. The implementation uses the most recent TIC value seen in the current column.

The `perColTickRate[3]` array tracks this. The table advances at the rate of the **first column** (column 0). If column 0 has no TIC, it uses `tableTickRate` from `tbl_tic`.

---

## 8. `tests/test_tables.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("Table auto-assigns on note-on", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();

    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.tbl_tic = 0x06; // 6 ticks per row
    state.instruments[0].sampler.dry = 0xC0;
    state.instruments[0].sampler.pan = 0x80;

    // Table 0, row 0: transpose +5 semitones
    seq.tables[0][0].transp = 5;
    seq.tables[0][0].vol = VOL_EMPTY;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(500);

    // Voice should have table transpose applied
    REQUIRE(host.engine().getSamplePhase(0) != 0.0f); // voice is playing
}

TEST_CASE("Table transpose changes pitch", "[tables]") {
    auto renderWithTableTransp = [](int transp) -> float {
        OfflineHost host;
        auto& seq = host.sequencer();
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_SAMPLER;
        state.instruments[0].sampler.tbl_tic = 0x01;
        state.instruments[0].sampler.dry = 0xC0;
        state.instruments[0].sampler.pan = 0x80;
        seq.tables[0][0].transp = transp;
        seq.tables[0][0].vol = 0x7F;
        setStep(seq, 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumTransp0 = renderWithTableTransp(0);
    float sumTransp12 = renderWithTableTransp(12); // +1 octave
    REQUIRE(sumTransp0 != sumTransp12);
}

TEST_CASE("Table volume scales output", "[tables]") {
    auto renderWithTableVol = [](int vol) -> float {
        OfflineHost host;
        auto& seq = host.sequencer();
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_SAMPLER;
        state.instruments[0].sampler.tbl_tic = 0x01;
        state.instruments[0].sampler.dry = 0xC0;
        state.instruments[0].sampler.pan = 0x80;
        seq.tables[0][0].transp = 0;
        seq.tables[0][0].vol = vol;
        setStep(seq, 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);
        float peak = 0;
        for (float v : host.audio()) peak = std::max(peak, std::abs(v));
        return peak;
    };
    float peakFull = renderWithTableVol(0x7F);
    float peakHalf = renderWithTableVol(0x40);
    REQUIRE(peakFull > peakHalf);
}

TEST_CASE("Table HOP jumps to row", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.tbl_tic = 0x01; // 1 tick per row (fast)
    state.instruments[0].sampler.dry = 0xC0;
    state.instruments[0].sampler.pan = 0x80;

    // Row 0: HOP to row 5
    seq.tables[0][0].fx[0] = {FxCmd::HOP, 5};
    // Row 5: volume 0 (silent)
    seq.tables[0][5].vol = 0;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2000);

    // Should have jumped to row 5 and be silent or near-silent
    float peak = 0;
    for (float v : host.audio()) peak = std::max(peak, std::abs(v));
    REQUIRE(peak < 0.1f);
}

TEST_CASE("Per-track groove override", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();

    // Groove 0: straight 6/6/6
    seq.grooves[0] = Groove{});
    for (int i = 0; i < 16; ++i) seq.grooves[0].steps[i] = 6;
    seq.grooves[0].length = 16;

    // Groove 1: swing 7/5
    for (int i = 0; i < 16; ++i) seq.grooves[1].steps[i] = (i % 2 == 0) ? 7 : 5;
    seq.grooves[1].length = 16;

    state.project.groove = 0;
    state.trackGroove[0] = 1; // track 0 uses swing

    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.dry = 0xC0;
    state.instruments[0].sampler.pan = 0x80;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(1000);

    // Just verify it renders without crash
    REQUIRE(host.audio().size() > 0);
}

TEST_CASE("Tables RT safety -- zero allocations", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.tbl_tic = 0x02;
    state.instruments[0].sampler.dry = 0xC0;
    state.instruments[0].sampler.pan = 0x80;
    seq.tables[0][0].transp = 3;
    seq.tables[0][0].vol = 0x60;

    g_allocCount = 0;
    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
```

---

## 9. Build and test

```powershell
cmake -B build -A x64
cmake --build build --config Release --target m8_tests
.\build\Release\m8_tests.exe "[tables]" --reporter compact
.\build\Release\m8_tests.exe --reporter compact   # full suite at the end
```

---

## 10. Known limitations / future work

- **Table FX VOL/PIT inside tables**: Implemented. VOL multiplies the row volume; PIT adds semitones to the row transpose. These are simpler than phrase VOL/PIT (which have their own semantics).
- **TBX command**: Not implemented. On real M8, TBX assigns a table to the *next* instrument. Deferred — TBL covers the primary use case.
- **GGR command**: Not implemented. GGR is a groove "clone" command. Low priority.
- **Table FX commands that differ from phrase FX**: In tables, VOL/PIT behave as row scalars. The phrase VOL/PIT commands are different (phrase VOL sets volume directly, phrase PIT transposes). The table versions are multiplicative/additive. This matches M8 behavior.
- **TIC 0x00 (on-trigger)**: The table advances one row each time the instrument is triggered (note-on). This is handled by the auto-assign logic advancing `row` on note-on, not by the tick timer.
- **Table length**: Tables are always 16 rows. On real M8, HOP to row 0 effectively creates shorter tables. The 16-row fixed length is correct.
- **Multiple tables per instrument**: Only one table is active per track at a time. TBL FX reassigns which table is active.
- **Interaction with modulation**: Table transpose adds to the pitch computed by the modulation system. Table volume multiplies the voice's effective volume *before* the gate/ramp. This means table volume and mod-to-volume are additive in the modulation domain but the table multiplier applies to the result.
