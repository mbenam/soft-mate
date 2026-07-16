# M8-SDL3 — Song Persistence Spec (`.m8s` load/save)

**Goal:** open and save real M8 song files, using the parity-tested `m8-files-cxx` port at
`github.com/mbenam/m8-files-cxx`.

**Why now:** the demo song is generated in code. You cannot make a song, close the app, and
come back to it. That is the line between a tech demo and a tracker.

**The real payoff:** open `.m8s` files made on actual M8 hardware and hear this engine play
them. A real song exercises play modes, mod slots, FX chains and grooves in combinations no
unit test will think of. That is the strongest correctness check available.

---

## What the library actually gives you

```cpp
#include "song.hpp"

std::vector<uint8_t> data = readFile("SONG.m8s");
m8::BinaryReader r(data);
m8::Song s = m8::Song::from_reader(r);        // throws on malformed input

// Writing back over the original preserves everything the library doesn't model:
std::vector<uint8_t> out = s.write_over(data);

// Writing from scratch:
m8::BinaryWriter w;
s.write(w);
```

### Five things that will bite you

**1. `write()` throws for pre-4.0 songs.** The library's own test says so:

```cpp
TEST_CASE("writing a pre-4.0 song throws", "[parity]") {
    for (const char* name : {"DEFAULT.m8s", "TEST-FILE.m8s"}) { ... }
}
```

`DEFAULT.m8s` and `TEST-FILE.m8s` are therefore **read-only fixtures**. Only `V4EMPTY.m8s`
and `V4-1EMPTY.m8s` round-trip. Save must refuse cleanly on a pre-4.0 song rather than letting
an exception escape into the UI.

**2. `Groove` has no length field.**

```cpp
struct Groove { uint8_t number; std::array<uint8_t, 16> steps; };
```

Yours has `length`. On load, derive it: the index of the first `0xFF` step, or 16 if none. On
save, pad with `0xFF` beyond `length`. Check the sentinel against a real file rather than
assuming.

**3. `tempo` is a `float`.** Your engine has `bpm` (int) + `bpm_frac`. Convert both ways and
round-trip losslessly: `bpm = int(tempo)`, `bpm_frac = round((tempo - bpm) * 100)`.

**4. Sizes differ.** `Song::N_PHRASES = 255`, `N_CHAINS = 255`, `N_TABLES = 256`,
`N_INSTRUMENTS = 128`, `N_GROOVES = 32`. Your `Sequencer` has 256 phrases and chains. Index
`0xFF` is the **empty sentinel**, not a real phrase. Do not write to it.

`SongSteps::steps` is a flat `array<uint8_t, 8 * 256>`; yours is `song[256].tracks[8]`. Check
the stride against a loaded file — do not guess which dimension is major.

**5. `ChainStep::transpose` is `uint8_t`**, yours is `int8_t`. Same bits, different type.
Reinterpret; do not convert.

### The library already proves byte-parity

`tests/test_parity.cpp` asserts `write_over()` reproduces the original bytes exactly for V4
and V4.1. **That is the library's job, and it is done.** Your job is to prove your
*conversion layer* is lossless — test L4.

---

## Task 1 — Vendor and link

Add as a git submodule at `third_party/m8-files-cxx`.

**Do not `add_subdirectory()` the repo root.** Its top-level `CMakeLists.txt` FetchContents
Catch2 v3.5.3 (a different version to yours) and adds a `tests` subdirectory — you will get
duplicate Catch2 targets.

Add only the library:

```cmake
# m8-files-cxx's src/CMakeLists.txt PUBLIC-links nlohmann_json, so we must provide it.
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json)

add_subdirectory(third_party/m8-files-cxx/src)
target_link_libraries(m8_engine PUBLIC m8_files_cpp)
```

**Cheaper — try this first.** `nlohmann_json` does not appear in any of the library's public
headers (`grep nlohmann src/*.hpp` returns nothing). Change
`third_party/m8-files-cxx/src/CMakeLists.txt` to link it `PRIVATE`, or drop it if nothing
uses it, and skip the FetchContent entirely. It is your repo — fix it at the source.

---

## Task 2 — The conversion layer

New files: **`src/io/SongIO.h` / `SongIO.cpp`**.

**This lives in `src/io/`, not `src/engine/`.** The engine stays free of `std::string`, file
I/O and exceptions. `SongIO` runs on the UI thread only.

```cpp
namespace m8::io {

struct LoadResult {
    bool ok = false;
    std::string error;                      // empty on success
    Sequencer   sequencer;                  // POD
    EngineState state;                      // instruments, mixer, effects, project
    std::vector<std::string> samplePaths;   // deduped, resolved
    std::vector<std::string> missing;       // samples not found on disk
    std::vector<uint8_t>     original;      // raw bytes, for write_over() on save
    bool writable = false;                  // false for pre-4.0 songs
};

LoadResult loadSong(const std::string& path, const std::string& sampleRoot);
bool       saveSong(const std::string& path, const LoadResult& origin,
                    const Sequencer&, const EngineState&, std::string& error);

} // namespace m8::io
```

### Load (UI thread)

1. Read the file, `Song::from_reader()`. **Catch every exception** — a corrupt file must not
   take the app down.
2. Convert to POD `Sequencer` + `EngineState`.
3. Collect the deduplicated `sample_path` set from every Sampler instrument. Resolve each as
   `sampleRoot + song.directory + sample_path`.
4. Decode each WAV once. Push one `LOAD_SAMPLE` per instrument — the pool's `find(path)`
   dedupes on the engine side. This is exactly what the path-keyed pool was built for.
5. **`PLAY_STOP` first**, then push the sequencer in **one** command.
6. Mirror everything into `uiSequencer` and `uiEngineState`.

### The bulk-load command

255 phrases × 16 rows is 4080 `SET_STEP` commands. The ring holds 1024. Do not do that.

```cpp
// CommandType::LOAD_SONG
//   payload: pointer to a heap-allocated Sequencer, UI-allocated.
//   audio thread: memcpy into m_sequencer (POD, trivially copyable),
//                 then push the pointer to the GC ring.
//   One command. One memcpy. No allocation on the audio thread.
```

Same for `EngineState` — either a second `LOAD_STATE` or fold both into one payload struct.

### Save (UI thread)

The UI holds the authoritative copy, so **saving never touches the engine.**

Convert `uiSequencer` + `uiEngineState` → `m8::Song` → `write_over(origin.original)` if the
song came from disk, `write()` if it is new. If `origin.writable == false`, refuse with a
clear message: *"pre-4.0 song — cannot be saved in place."*

---

## Task 3 — Field mapping

Mostly mechanical. Four things are not.

### 3a. Sample paths

`m8::Sampler::sample_path` is a `std::string`; your `SamplerState::samplePath` is
`char[128]`. Truncate on load and record a warning — do not silently corrupt.

Add a **sample root** setting: a directory the user points at their SD card (or a local copy).
Paths in the file are relative to the card root.

**A missing sample must not fail the load.** Leave `sampler.sample = -1`, add the path to
`LoadResult::missing`, show it in the UI. A song with three missing WAVs should still open and
play the other five.

### 3b. The `Mod` variant → your generic slots

`m8-files` stores `Mod` as a `std::variant` of six structs; you store
`Modulator { type, dest, amt, p1..p4 }`. The mapping is positional, and the field order is
confirmed against hardware:

| Type | p1 | p2 | p3 | p4 |
|---|---|---|---|---|
| `00` AHD ENV | ATK | HOLD | DEC | — |
| `01` ADSR ENV | ATK | DEC | SUS | REL |
| `02` DRUM ENV | PEAK | BODY | DEC | — |
| `03` LFO | SHAPE | TRIG | FREQ | — |
| `04` TRIG ENV | ATK | HOLD | DEC | SRC |
| `05` TRACKING | SRC | LVAL | HVAL | — |

Write it as a pair of free functions with a `static_assert` on the variant index order, so a
library change that reorders the variant **fails to compile** rather than silently mis-loading
every patch in every song.

### 3c. Instrument types you don't implement — preserve, don't drop

`m8-files` models seven: `WavSynth`, `MacroSynth`, `Sampler`, `MIDIOut`, `FMSynth`,
`HyperSynth`, `ExternalInst`. You have Sampler and a placeholder Macrosyn.

**Keep the raw `m8::Song` alongside the converted PODs.** Unimplemented types get
`INST_NONE` in the engine (silent), but the original is retained and written back on save.
`write_over()` does most of this for you.

Otherwise opening and saving someone's song silently destroys their FM patches. That is worse
than not playing them.

The same applies to `scales`, `eqs`, `midi_mappings`, `midi_settings`, `quantize` and `key` —
all present in `Song`, none used by your engine. Preserve every one.

### 3d. Version

Read `song.version`, keep it, write the same version back. Do not upgrade or normalise.

---

## Task 4 — Tests

Fixtures: the four `.m8s` files in `third_party/m8-files-cxx/examples/songs/`. Tag `[io]`.

| # | Test | Assert |
|---|---|---|
| L1 | Load `V4EMPTY.m8s` | no throw; phrases empty; tempo and name match |
| L2 | Load `DEFAULT.m8s` | no throw; `writable == false` (pre-4.0) |
| L3 | Load `TEST-FILE.m8s` | no throw; some phrase has a non-empty step; some chain is non-empty |
| L4 | **Round-trip** | `V4EMPTY.m8s` and `V4-1EMPTY.m8s`: load → POD → back → `write_over` → **bytes identical**. Use `diff_count()`, as the library's own parity test does. |
| L5 | Engine round-trip | load → `LOAD_SONG` → read the engine's sequencer → `memcmp` equals `uiSequencer` |
| L6 | Missing sample | song with a non-existent WAV loads; that instrument has `sample == -1` and is listed in `missing`; the others still play |
| L7 | Unimplemented type | a song with an FMSynth loads; that track is silent; saving preserves the FMSynth bytes |
| L8 | **Offline render** | load `TEST-FILE.m8s`, `PLAY_SONG`, render 30 s: no crash, no NaN, no hang. Under ASan. |
| L9 | Bulk load | `LOAD_SONG` is one command; the ring never fills; B8.1 (no allocation on the audio thread) still passes |
| L10 | Save refuses pre-4.0 | returns an error; no exception escapes |

**L4 is the one that matters.** Byte-identical round-trip means the conversion layer is
lossless — you cannot corrupt someone's song. If it fails, the byte diff tells you exactly
which field is wrong.

**L8 is the payoff.** A real M8 song, rendering through this engine.

---

## Task 5 — The file browser

`FileBrowser` already walks the filesystem for `.wav`. Extend it:

- Filter by extension: `.m8s`, `.m8i`, `.wav`.
- Wire the Project screen's `LOAD` / `SAVE` fields to it — they exist in the layout and do
  nothing today.
- Save with no filename: prompt via a plain SDL text-input line. **Do not build the M8's
  on-screen keyboard.** Cosmetic; it can wait.
- Add a **sample root** setting to the Project screen.

`.m8i` (single instrument) uses the same library. Cheap once songs work, and the repo ships
three as fixtures.

---

## Order

1. Task 1 — vendor and link. Get `m8_files_cpp` compiling into `m8_engine`.
2. Task 2 + 3 — the conversion layer and the `LOAD_SONG` bulk command.
3. **L4 first.** Byte-identical round-trip before anything touches the engine.
4. L1, L2, L3, L5–L10.
5. Task 5 — file browser and Project screen.

Then load a real song off an SD card and listen to it.

---

## Out of scope

- On-screen keyboard.
- Sample browser / preview.
- `.m8i` **save** (load only, initially).
- New-song template — `V4EMPTY.m8s` is a perfectly good one.
