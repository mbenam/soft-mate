# M8 Driver Spec — Framebuffer Foundation + Parity Tracks

Everything that talks to a real M8 over serial. Supersedes `M8_NAV_AGENT_UX_SPEC.md`
(deleted), which scoped only agent ergonomics and was mis-ordered once the audio-
and UI-parity goals were on the table.

Three goals depend on this driver:

- **A — Agent ergonomics.** An agent accomplishes intent-level tasks without being
  told the button presses.
- **B — Audio parity.** Load a probe, play a note, capture the audio, compare
  against the clone's render. *Only the load-and-play half is here;* probe
  authoring and audio comparison are in `M8_PROBE_AUTHORING_SPEC.md`.
- **C — UI parity.** Read the device's screens and validate the clone's UI
  against them.

All three sit on the same foundation, which is why they share one document: the
settled-read fix, the result envelope, and the screen-serialization code are
common to all of them, and splitting them across documents would duplicate that
foundation and create cross-document references that context compaction reliably
loses.

> **Scope.** Serial only. `m8_device` continues to link **no engine, no SDL, no
> audio** — hard repo invariant (`status.md`, `AGENTS.md`). Windows/Win32 serial;
> other platforms keep the existing "not implemented" stub. No new third-party
> dependencies. No new build directories (`AGENTS.md` §1).

> **Status.** Not started. **Foundation is mandatory and ordered.** Tracks A, B,
> and C each depend on the whole Foundation and are otherwise independent — run
> them in any order, or in parallel sessions.

---

## Reading this spec if you are an implementing agent

Each step is sized to fit comfortably in a 200k context window. For each step you
need only the files named in that step — **do not read the whole repo.**

Per step you get **Files**, **Change**, **Why**, and **Verify**. Run the Verify
before moving on.

Rules:

1. **One commit per step.** Message begins with the step id:
   `git commit -m "F3 fix swapped readScreen args in main_nav.cpp"`.
2. **Tick the checklist in THIS FILE after each step's Verify passes**, in the same
   commit. The checklist is the progress record, not your memory.
3. **Re-read this spec's section for a step before starting it.** Do not work from
   recollection. Each step names only 1-3 files, so this is cheap.
4. **Build with `--target`, always** (`AGENTS.md`):
   `cmake --build build --config Release --target m8_nav`
5. **Stop if a Verify fails.** Do not proceed, do not work around it. Report what
   failed.
6. **Do not refactor anything not named in the step.** In particular do not
   reformat `Primitives.cpp` or `ScreenModel.h` — they contain hardware-confirmed
   comments that are the only record of several fixed bugs.
7. **Hardware steps are marked `[HW]`.** With no device attached, skip them, leave
   them unticked, continue. Never delete a `[HW]` step because you couldn't run it.
8. When a step says "add", add — don't replace adjacent working code.

### Surviving context compaction

Your context may be summarized and truncated partway through. You lose detail
first and *rationale* first of all — the wrong thing to lose here, because several
steps look like mistakes or unfinished work without their justification. Assume
you will be compacted at least once.

- **Nothing important lives only in your context.** Progress → the checklist in
  this file. Hardware findings → `docs/tools/hw_findings.md`. Reasoning → the
  commit message. If a fact matters to a later step and isn't in a file, it's lost.
- **Never trust a summary that says a step is done.** Compaction summaries reliably
  over-claim completion. Unticked means not done. Ticked but doubtful means re-run
  the Verify — every Verify here is idempotent.
- **`git log --oneline` plus the checklist reconstruct your position.** Run both
  before resuming after any gap.
- **If you're about to simplify, tighten, or complete something that looks
  half-finished, re-read that step's Why and Guardrail first.** Several things that
  look wrong are deliberate.

### Invariants — re-read before every step

These are what a partially-remembered version of this spec gets wrong. They are
load-bearing.

1. **The `readScreen(200, 300)` calls in `Primitives.cpp` are CORRECTLY ordered.**
   Only two call sites in `main_nav.cpp` (lines 391, 493) are transposed. Step F4
   widens the Primitives ceilings for a *different* reason. Do not "fix" the
   argument order there — nothing is wrong with it and eight working call sites are
   at stake.
2. **Do not change the signatures or defaults of `readScreen` or `read`.** ~40
   correct call sites. New code uses `readSettled`.
3. **The 1200ms read ceilings in F4 are intentional.** Slower than what they
   replace. A verified read at 1200ms beats an unverified one at 300ms. Do not
   re-tighten.
4. **`searchTree` and `find_in_list` return `AMBIGUOUS` rather than picking a
   match. This is finished behaviour, not a stub.**
5. **`find_in_list` selects a row and does not press EDIT. Deliberate.** Separating
   selection from activation makes a wrong match recoverable.
6. **Every loop touching hardware stays bounded.** `maxDepth`, `maxVisits`,
   `maxRows`, `maxPresses` are budgets. On exhaustion, report; never spin.
7. **The driver caches no device state.** Every primitive re-reads. A
   confidently-wrong cache is worse than a slow read.
8. **Macros return the first failing sub-result verbatim.**
9. **`wait_for_text` and `wait_for_screen` never press a key.** They only wait.
10. **A UI capture with `settled == false` is not a valid golden.** Track C must
    reject it, not merely flag it.

### Deleted from the previous version — do not resurrect

- **`--audition-note` as a UI-navigation macro** (INSTRUMENT → set type → PHRASE →
  `moveCursorToGrid` → `enterNote` → PLAY → `assertPlaying`). `M8Device::keyjazz`
  (M8Device.cpp:590) sends `'K' <note> <vel>` and plays a live note directly on the
  synth engine, bypassing the sequencer. `m8_capture` already uses it; `m8_nav`
  never has. Step B1 exposes it instead. Six fragile verified steps replaced by two
  bytes.
- **Any `m8_nav` mode that creates songs or sets instrument parameters through the
  UI.** That work belongs in offline `.m8s` authoring — see
  `M8_PROBE_AUTHORING_SPEC.md`.

### Progress checklist

Tick in this file, in the same commit as the step.

```
FOUNDATION (mandatory, in order)
[x] F1   ReadStats struct + accessor
[x] F2   readSettled() explicit 3-arg read
[x] F3   Fix swapped readScreen args in main_nav.cpp
[x] F4   Widen the read ceilings in Primitives.cpp
[ ] F5   Offline test for read telemetry
[ ] F6   Result.h — ExitCode enum + Envelope
[ ] F7   Single exit path in main_nav.cpp
[ ] F8   Normalize pinGestures return codes
[ ] F9   ScreenGrid listRows()
[ ] F10  Promote isModal/isLiveMode to ScreenModel.h
[ ] F11  printJson emits highlights
[ ] F12  Semantic.h — semanticState()
[ ] F13  --semantic-state flag
[ ] F14  Offline tests for listRows + semanticState + highlights
[ ] F15  --serve daemon skeleton
[ ] F16  Daemon command dispatch
[ ] F17  --pin-gestures mutation guard
[ ] F18  Update docs/tools/m8_nav.md for Foundation changes

TRACK A — file loading + agent ergonomics
[ ] A0   Decide the SD-card access question (gate for A1-A7)
[ ] A1   [HW] Discover directory-row marker → docs/tools/hw_findings.md
[ ] A2   isDirectoryRow()
[ ] A3   enumerateList()
[ ] A4   enterDir() / upDir()
[ ] A5   searchTree() — bounded DFS
[ ] A6   searchAndLoad() + --find-file / --load-song
[ ] A7   [HW] Hardware validation of the crawler
[ ] A8   wait_for_text / wait_for_screen verbs
[ ] A9   find_in_list / enter_dir / up_dir / state verbs
[ ] A10  Add docs/tools/agent_guide.md

TRACK B — audio-parity orchestration
[ ] B1   --keyjazz flag on m8_nav
[ ] B2   probe-play recipe script
[ ] B3   [HW] Validate load → keyjazz → capture handoff

TRACK C — UI-parity capture
[ ] C1   UiCapture.h — capture format definition
[ ] C2   Style clustering (RGB → style id)
[ ] C3   --ui-capture flag on m8_nav
[ ] C4   Clone-side emitter (Renderer)
[ ] C5   m8_diffcheck compares UiCapture files
[ ] C6   [HW] Theme + font pinning
[ ] C7   Sweep script over all screens/states
[ ] C8   [HW] Capture the parity corpus
```

---

# FOUNDATION

## Why this section exists

`M8Device::readScreen` is declared `readScreen(int settleMs, int maxMs)`. Two call
sites in `main_nav.cpp` pass them in the wrong order:

```cpp
main_nav.cpp:391   dev.readScreen(minMs, settleMs);    // → settleMs=700, maxMs=250
main_nav.cpp:493   dev.readScreen(150, settleMs);      // → settleMs=150, maxMs=250
```

`readInto(minMs, settleMs, maxMs)` exits on `sinceStart >= maxMs`, or on
`sinceStart >= minMs && sinceData >= settleMs`. At line 391 the settle condition
needs 700ms of quiet but the timeout fires at 250ms, so the settle branch can never
win — the first read after opening is a hard 250ms read regardless of `--min-ms`.
`--max-ms` is parsed at line 333 and then never read again; it is dead.

**Correction to note if you were told otherwise:** the call sites in
`Primitives.cpp` use `readScreen(200, 300)`, which is the *correct* order. They are
not swapped. They are on a 300ms ceiling with a 200ms settle requirement — only a
100ms window for the settle branch to fire — so against a continuously-streaming
display they will usually degenerate to the timeout rather than a true settle. That
is a tightness problem, not an inversion; F4 addresses it separately.

Do not assume this explains the multi-hop `gotoScreen` flakiness or `loadFile`'s
`rc=11`. It might. F1 adds the telemetry that will tell you instead of guessing.

---

### Step F1 — `ReadStats`

**Files:** `src/tools/m8/M8Device.h`, `src/tools/m8/M8Device.cpp`

**Change.** In `M8Device.h`, above `class M8Device`:

```cpp
// Telemetry for the most recent read. Lets callers (and agents) distinguish
// "the screen went quiet and I read a settled frame" from "I gave up on a
// timeout and this grid may be mid-repaint".
struct ReadStats {
    int  elapsedMs   = 0;      // wall time spent in the read
    int  quietMs     = 0;      // ms since the last byte arrived, at exit
    int  framesSeen  = 0;      // complete SLIP frames decoded this read
    bool settled     = false;  // true = exited via the settle branch
    bool timedOut    = false;  // true = exited via the maxMs branch
};
```

In `public:`, after `grid()`:

```cpp
    const ReadStats& lastRead() const { return m_lastRead; }
```

In `private:`, after `bool m_open = false;`:

```cpp
    ReadStats m_lastRead;
```

In `M8Device.cpp`, rewrite `readInto` (line 495) to populate it:

```cpp
void M8Device::readInto(int minMs, int settleMs, int maxMs) {
    m_lastRead = ReadStats{};
    uint8_t buf[4096];
    std::vector<uint8_t> frame;
    auto start = std::chrono::steady_clock::now();
    auto lastData = start;
    for (;;) {
        size_t n = m_port.recv(buf, sizeof(buf));
        auto now = std::chrono::steady_clock::now();
        if (n > 0) {
            lastData = now;
            for (size_t i = 0; i < n; ++i)
                if (m_slip.feed(buf[i], frame)) {
                    m_grid.handleFrame(frame);
                    ++m_lastRead.framesSeen;
                }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        int sinceStart = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        int sinceData  = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count());
        m_lastRead.elapsedMs = sinceStart;
        m_lastRead.quietMs   = sinceData;
        if (sinceStart >= maxMs) { m_lastRead.timedOut = true; break; }
        if (sinceStart >= minMs && sinceData >= settleMs) { m_lastRead.settled = true; break; }
    }
}
```

**Why.** Everything downstream reads from here — the envelope, `settled` in
semantic state, the daemon's per-command reply, and Track C's golden-validity
gate. It is also the cheapest instrument for "is the flakiness a timing problem?"

**Verify.** Compiles clean.

---

### Step F2 — `readSettled()`

**Files:** `src/tools/m8/M8Device.h`, `src/tools/m8/M8Device.cpp`

**Change.** In `M8Device.h`, in the Convenience block after `readScreen`:

```cpp
    // Explicit 3-arg read. Prefer this over readScreen() in new code: the
    // 2-arg form's (settleMs, maxMs) order has been misread at call sites.
    // If maxMs <= settleMs the settle branch can never fire and the read
    // degenerates to a fixed maxMs delay.
    void readSettled(int minMs, int settleMs, int maxMs);
```

In `M8Device.cpp`, beside `readScreen` (line 603):

```cpp
void M8Device::readSettled(int minMs, int settleMs, int maxMs) {
    readInto(minMs, settleMs, maxMs);
}
```

Leave `readScreen` and `read` exactly as they are.

**Why.** A three-argument function whose parameters are named in the order the
underlying loop uses them cannot be transposed the way a `(settle, max)` pair can.
New code uses this; old correct code is left alone.

**Verify.** Compiles clean.

---

### Step F3 — Fix the two swapped call sites

**Files:** `src/tools/main_nav.cpp`

**Change.** Line 391: `dev.readScreen(minMs, settleMs);` →
`dev.readSettled(minMs, settleMs, maxMs);`

Line 493 (inside the `--keys` loop): `dev.readScreen(150, settleMs);` →
`dev.readSettled(150, settleMs, maxMs);`

Change `maxMs`'s default at line 312 from `2000` to `2500`, so the default
`--settle-ms 250` has room to fire before the timeout.

**Why.** `--min-ms`, `--settle-ms`, and `--max-ms` now all mean what
`docs/tools/m8_nav.md` says, and `--max-ms` stops being dead code.

**Verify.** Compiles clean. `grep -n "maxMs" src/tools/main_nav.cpp` shows at least
four hits (declaration, parse, both `readSettled` calls).

---

### Step F4 — Widen the read ceilings in `Primitives.cpp`

**Files:** `src/tools/m8/Primitives.cpp`

**Change.** Eight `dev.readScreen(200, 300);` call sites (lines 883, 896, 902, 938,
951, 957, 974, 1063). Replace each with:

```cpp
    dev.readSettled(120, 200, 1200);
```

Preserve each line's indentation. Change nothing else.

**Why.** A 200ms settle under a 300ms ceiling gives the settle branch 100ms;
against a near-continuous display stream it usually loses, so these are
effectively fixed 300ms delays returning whatever the framebuffer happened to
hold. A 1200ms ceiling lets a settled frame be *detected* rather than timed out,
while still bounding the worst case. `minMs` 120 matches what `read()` hardcodes.

**Guardrail.** This makes worst-case paths slower. That is the correct trade, and
F5 plus the `settled` telemetry will tell you which case you're actually getting.
See Invariant 3.

**Verify.** Compiles clean. `grep -c "readScreen(200, 300)" src/tools/m8/Primitives.cpp`
returns 0.

---

### Step F5 — Offline test for read telemetry

**Files:** `tests/test_device_decode.cpp`

**Change.** Add `[hwdecode]`-tagged cases: a default-constructed `M8Device` reports
`framesSeen == 0` and `settled == false`; if the file has a helper that feeds
synthetic SLIP frames, `framesSeen` tracks frames fed. Read the file first and
reuse its existing fixtures — do not invent a new harness.

**Verify.**

```
cmake --build build --config Release --target m8_tests
.\build\Release\m8_tests.exe "[hwdecode]" --reporter compact
```

---

### Step F6 — `Result.h`

**Files:** create `src/tools/m8/Result.h`

Header-only, so no `CMakeLists.txt` change. Namespace `m8::dev`.

```cpp
#pragma once

// ===========================================================================
// Result.h — machine-readable outcome envelope for m8_nav invocations.
//
// Every m8_nav run emits exactly one JSON object on stdout as its LAST line,
// prefixed "M8NAV_RESULT " so it survives a log that also contains screen
// dumps. Exit codes are from ExitCode and are stable: agents may branch on them.
// ===========================================================================

#include <string>
#include <vector>
#include <cstdio>

namespace m8 {
namespace dev {

enum class ExitCode : int {
    OK              = 0,
    USAGE           = 1,   // bad CLI, unknown screen name, unknown field
    PORT            = 2,   // serial open failed
    NO_DATA         = 3,   // nothing decoded; device absent or not streaming
    NAV_FAILED      = 4,   // gotoScreen / moveCursorTo did not reach target
    NOT_FOUND        = 5,  // searched and the target does not exist
    AMBIGUOUS       = 6,   // >1 match; caller must disambiguate
    MODAL_STUCK     = 7,   // a modal would not dismiss
    SCRIPT_ERROR    = 8,   // parse or runtime failure in a .m8script
    GESTURES_UNPINNED = 9, // an edit primitive ran without hw_buttons.json
    TIMEOUT         = 10,  // a bounded loop exhausted its budget
    UNSETTLED       = 11,  // a capture was requested but the read never settled
    INTERNAL        = 20,
};

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

struct Envelope {
    ExitCode    code = ExitCode::OK;
    std::string action;
    std::string message;
    std::string screen;
    std::string cursorField;
    bool        settled = false;
    int         readMs  = 0;
    // Free-form extras, pre-formatted as "key":<json-value>.
    std::vector<std::string> extra;

    bool ok() const { return code == ExitCode::OK; }

    void emit(FILE* out) const {
        std::fprintf(out, "M8NAV_RESULT {");
        std::fprintf(out, "\"ok\":%s", ok() ? "true" : "false");
        std::fprintf(out, ",\"code\":%d", static_cast<int>(code));
        std::fprintf(out, ",\"action\":\"%s\"", jsonEscape(action).c_str());
        std::fprintf(out, ",\"message\":\"%s\"", jsonEscape(message).c_str());
        std::fprintf(out, ",\"screen\":\"%s\"", jsonEscape(screen).c_str());
        std::fprintf(out, ",\"cursor_field\":\"%s\"", jsonEscape(cursorField).c_str());
        std::fprintf(out, ",\"settled\":%s", settled ? "true" : "false");
        std::fprintf(out, ",\"read_ms\":%d", readMs);
        for (const auto& e : extra) std::fprintf(out, ",%s", e.c_str());
        std::fprintf(out, "}\n");
    }
};

} // namespace dev
} // namespace m8
```

**Why.** One line, one grep target, one stable contract. `settled`/`read_ms` come
from F1 so a caller can see when it's holding a possibly mid-repaint screen.
`UNSETTLED` exists for Track C, which must refuse to write an unsettled golden.

**Verify.** Add `#include "m8/Result.h"` to `main_nav.cpp`; compiles clean.

---

### Step F7 — Single exit path in `main_nav.cpp`

**Files:** `src/tools/main_nav.cpp`

The largest edit in this spec. Work carefully.

1. `#include "m8/Result.h"` with the other `m8/` includes.
2. Declare `Envelope env;` in `main` right after arg parsing.
3. Replace every `return N;` in `main` *after the device is opened* with: set
   `env.code`, `env.action`, `env.message`, then `goto done;`. At the end of
   `main`, before the closing brace:

```cpp
done:
    if (dev.isOpen()) {
        env.screen      = dev.grid().topHeader();
        auto cf = dev.cursorField();
        env.cursorField = cf ? cf->name : "";
        env.settled     = dev.lastRead().settled;
        env.readMs      = dev.lastRead().elapsedMs;
        dev.close();
    }
    env.emit(stdout);
    return static_cast<int>(env.code);
}
```

4. Map the existing returns. Keep the existing human-readable output — the
   envelope is additive.

| Current site | New code |
|---|---|
| no `--port` (line 340) | `USAGE`, return directly — device not open, no `goto` |
| `open`/`openNoReset` fail (373-377) | `PORT`, return directly |
| unknown screen (426) | `USAGE` |
| `gotoScreen` fail (432) | `NAV_FAILED` |
| script parse error (444) | `SCRIPT_ERROR` |
| script run `rc != 0` (453) | `SCRIPT_ERROR` |
| `readField` fail (468) | `NOT_FOUND` |
| `loadFile` `rc != 0` (477) | `NOT_FOUND` if `rc == 14`, else `NAV_FAILED` |
| `cells.empty()` (501) | `NO_DATA` |
| success paths | leave `env.code` at `OK` |

5. Fix the port leak: the `cells.empty()` branch must `goto done;`, not `return 3;`.

**Why.** One authoritative line per invocation; a branchable exit code; no leaked
port on the no-data path.

**Guardrail.** `goto` across variable initializations won't compile. If MSVC
rejects a jump, hoist the offending declaration above the first `goto`. Do not
duplicate the `done:` block.

**Verify.** With no device: `.\build\Release\m8_nav.exe --port COM99` — last stdout
line begins `M8NAV_RESULT ` and contains `"code":2`; process exit code 2.

---

### Step F8 — Normalize `pinGestures` return codes

**Files:** `src/tools/main_nav.cpp`

**Change.** In `pinGestures`: field not in any map (line 98) →
`static_cast<int>(ExitCode::USAGE)`; `gotoScreen` failure (line 111) →
`NAV_FAILED`; the `pressUntil` fallback failure (line 136), currently `return 2` →
`NOT_FOUND`.

**Why.** `2` means "serial port failure" everywhere else. Returning it for
"couldn't find a field" makes an agent retry the connection instead of fixing the
field name.

**Verify.** Compiles clean; no `return 2;` remains inside `pinGestures`.

---

### Step F9 — `listRows()`

**Files:** `src/tools/m8/ScreenModel.h`

**Change.** After `identifyScreen(const ScreenGrid&)` (ends line 121):

```cpp
// ---- List/browser rows -----------------------------------------------------

struct ListRow {
    int         y = -1;           // pixel y of the row
    std::string text;             // trimmed row text
    bool        selected = false; // cursor is on this row
};

// Enumerate main-area rows as a selectable list. Used by the file browser and
// by any screen an agent needs to scan. Screen order, blank rows skipped.
inline std::vector<ListRow> listRows(const ScreenGrid& grid) {
    std::vector<ListRow> out;
    int curY = grid.cursorRowY();
    for (auto& [y, text] : grid.mainRows()) {
        size_t b = text.find_first_not_of(' ');
        if (b == std::string::npos) continue;
        size_t e = text.find_last_not_of(' ');
        ListRow r;
        r.y        = y;
        r.text     = text.substr(b, e - b + 1);
        r.selected = (y == curY);
        out.push_back(r);
    }
    return out;
}
```

**Why.** Track A's crawler and F12's semantic state both need "what rows are
visible and which is selected" as one call, in one place, so they can't drift.

**Verify.** Compiles clean.

---

### Step F10 — Promote `isModal` / `isLiveMode`

**Files:** `src/tools/m8/ScreenModel.h`, `src/tools/m8/Primitives.cpp`

**Change.** `isModal` (`Primitives.cpp:31`) and `isLiveMode` (`:45`) are
file-static. Move both to `ScreenModel.h` as `inline` free functions, **verbatim
including their comments** — those record hardware-confirmed behaviour about
`topHeader()` stripping spaces. Delete the statics from `Primitives.cpp`. Place
them after `listRows()`. `Primitives.cpp` already includes `ScreenModel.h`, so its
~10 call sites need no change.

**Guardrail.** Both call `toUpper`, which is also file-static in `Primitives.cpp`.
`ScreenModel.h` has none. Add a small `inline` one there named **`hdrUpper`** and
use that inside the moved functions, to avoid an ambiguous overload with
`Primitives.cpp`'s `toUpper`.

**Why.** F12's semantic state and A9's `state` verb both need modal detection and
neither can reach a file-static.

**Verify.** `--target m8_nav` and `--target m8_tests` both compile clean. No
duplicate-symbol errors.

---

### Step F11 — `printJson` emits `highlights`

**Files:** `src/tools/m8/M8Device.cpp`

**Change.** `ScreenGrid::printJson` (line 371) serializes only `cells`. The
`highlights` vector — `Rect{x, y, w, h, c[3]}`, populated from `0xFE` rect-fill
frames at line 232 — is dropped. Add it. After the closing `]` of the cells array
and before the closing `}`:

```cpp
    o << ",\n  \"highlights\": [\n";
    bool firstR = true;
    for (auto& r : highlights) {
        if (!firstR) o << ",\n";
        firstR = false;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "    {\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"c\":[%d,%d,%d]}",
            r.x, r.y, r.w, r.h, r.c[0], r.c[1], r.c[2]);
        o << buf;
    }
    o << "\n  ]\n";
```

Adjust the existing `o << "\n  ]\n}\n";` so the JSON stays well-formed with the new
member.

**Why.** Rect fills are the sub-character geometry of the UI: cursor and selection
highlights, and the mixer's output level bars. The repo has already shipped a bug
in exactly that area — the app-automation spec records an "output-bar column fix +
adjacent (not overlapping) background/foreground bar fill" in `MixerScreen.cpp`.
That bug class is invisible to every capture the tool can currently take. This is
the cheapest change in the whole document with the largest effect on Track C, which
is why it's in the Foundation rather than in Track C.

**Verify.** Compiles clean. `[HW]` or against a synthetic grid: the emitted JSON
parses and contains a `highlights` array. Offline check with a grid built by
`tests/test_device_decode.cpp`'s fixtures — see F14.

---

### Step F12 — `Semantic.h`

**Files:** create `src/tools/m8/Semantic.h`

```cpp
#pragma once

// ===========================================================================
// Semantic.h — the agent-facing view of device state.
//
// "Where am I, what is selected, what can I see, and is any of this
// trustworthy" as one JSON object. `settled` is the important field: false
// means the grid below may be a partial repaint.
// ===========================================================================

#include "M8Device.h"
#include "ScreenModel.h"
#include "Result.h"

namespace m8 {
namespace dev {

inline const char* screenName(Screen s) {
    for (auto& si : kScreenTable)
        if (si.id == s) return si.canonHeader;
    return "UNKNOWN";
}

inline std::string semanticState(M8Device& dev) {
    const ScreenGrid& g = dev.grid();
    std::string o = "{";
    o += "\"screen\":\"";  o += jsonEscape(screenName(identifyScreen(g))); o += "\"";
    o += ",\"raw_header\":\""; o += jsonEscape(g.topHeader()); o += "\"";

    auto cf = dev.cursorField();
    o += ",\"cursor_field\":";
    if (cf) { o += "\""; o += jsonEscape(cf->name); o += "\""; } else { o += "null"; }
    o += ",\"cursor_row_y\":" + std::to_string(g.cursorRowY());

    o += ",\"modal\":";      o += isModal(g) ? "true" : "false";
    o += ",\"live_mode\":";  o += isLiveMode(g) ? "true" : "false";

    Firmware fw = dev.firmware();
    o += ",\"firmware\":\"" + std::to_string(fw.major) + "."
       + std::to_string(fw.minor) + "." + std::to_string(fw.patch) + "\"";
    o += ",\"font_mode\":" + std::to_string(fw.fontMode);

    const ReadStats& rs = dev.lastRead();
    o += ",\"settled\":";  o += rs.settled ? "true" : "false";
    o += ",\"read_ms\":" + std::to_string(rs.elapsedMs);
    o += ",\"frames_seen\":" + std::to_string(rs.framesSeen);
    o += ",\"cells_decoded\":" + std::to_string(static_cast<int>(g.cells.size()));
    o += ",\"highlight_count\":" + std::to_string(static_cast<int>(g.highlights.size()));

    o += ",\"visible_rows\":[";
    bool first = true;
    for (const auto& r : listRows(g)) {
        if (!first) o += ",";
        first = false;
        o += "{\"text\":\"" + jsonEscape(r.text) + "\"";
        o += ",\"selected\":"; o += r.selected ? "true" : "false";
        o += "}";
    }
    o += "]}";
    return o;
}

} // namespace dev
} // namespace m8
```

**Why.** One call gives an agent everything it currently infers from an ASCII grid,
plus the trustworthiness signal it cannot get at all. `font_mode` is included
because Track C must refuse to diff captures across font modes.

**Verify.** Compiles clean when included from `main_nav.cpp`.

---

### Step F13 — `--semantic-state` flag

**Files:** `src/tools/main_nav.cpp`

**Change.** `#include "m8/Semantic.h"`. Add `bool semanticState_ = false;` with the
other mode flags, parse `--semantic-state`, add it to the "no mode flag given"
condition at line 350, and add a mode block after `--goto-screen`:

```cpp
    if (semanticState_) {
        std::printf("%s\n", semanticState(dev).c_str());
        env.action = "semantic-state";
        goto done;
    }
```

Also make it composable: when passed *with* another mode flag, emit the semantic
JSON at `done:` before `env.emit`, so `--goto-screen PHRASE --semantic-state`
returns post-navigation state.

**Why.** Composability removes a round trip per action. Each process pays a full
framebuffer `'R'` resend on open, so halving invocations is a real win — and it's
the cheap version of F15's daemon.

**Verify.** Compiles clean; `--semantic-state` is not reported as an unknown arg.
`[HW]`: prints one JSON object plus the `M8NAV_RESULT` line.

---

### Step F14 — Offline tests

**Files:** `tests/test_device_decode.cpp`

**Change.** Add `[hwdecode]` cases: `listRows()` skips blanks, trims, marks exactly
one row selected when `cursorRowY()` matches; `isModal()` true for header
`LOSECHANGESTOCURRENTSONG?` and false for `SONG`; `semanticState()` contains
`"settled":false` for a device that never read; `printJson` output contains a
`highlights` array, and a grid fed a `0xFE` rect frame emits a non-empty one.
Reuse the file's existing fixtures.

**Verify.** `.\build\Release\m8_tests.exe "[hwdecode]" --reporter compact`

---

### Step F15 — `--serve` daemon skeleton

**Files:** `src/tools/main_nav.cpp`

**Change.** Add `--serve`. Open the port once, do one full read, then loop: read one
line from `stdin`, write one JSON object to `stdout`, flush. Exit cleanly on EOF or
`quit`.

Do not add a JSON parser dependency. Accept a simple line format and document it as
the daemon protocol:

```
ACTION key=value key=value
```

e.g. `press key=DOWN hold=15`, `goto screen=PHRASE`, `state`, `find name=sunrise`.
Reply with `semanticState(dev)` plus an `"ok"` field.

**Why — and why this moved up from the previous version's Phase 5.** Every
invocation opens the port, sends `'E'` + `'R'`, and waits for a full framebuffer
resend, because each process starts with an empty `ScreenGrid`. That is the real
cost of process-per-command, not startup time — and it's the entire reason
`--no-reset` exists and needs a paragraph of warnings. It also fixes the exclusive-
COM-port collision the current design cannot.

Track C makes this load-bearing rather than nice-to-have: a UI sweep is 12 screens
× cursor-at-each-field × modal/playing states — hundreds of captures. At one
process and one resend each that's painful; in a resident session it's one scripted
pass.

**Verify.** `echo state | m8_nav --port COM99 --serve` exits non-zero with a `PORT`
envelope rather than hanging.

---

### Step F16 — Daemon dispatch

**Files:** `src/tools/main_nav.cpp`

**Change.** Map actions onto existing primitives: `press`, `goto`, `cursor`, `read`,
`state`, `capture`, `find`, `load`, `enter_dir`, `up_dir`, `script`, `quit`. Every
reply includes `settled` and `read_ms`. Errors reply with the `ExitCode` as a number
and **never terminate the loop** — one bad command must not kill the session.

**Guardrail.** No global state between commands beyond the open port and the grid.
The daemon must not cache "the screen I think we're on"; it re-reads. See
Invariant 7.

**Verify.** `[HW]` a 20-command session runs without reopening the port. Confirm
`'R'` is sent once (temporary counter, then remove it).

---

### Step F17 — `--pin-gestures` mutation guard

**Files:** `src/tools/main_nav.cpp`

`pinGestures` step 6 (lines 220-228) is labelled "Undo any edits" and the docs claim
it restores state, but it only navigates away and back. Every mask classified
`EDITED` has left a real parameter change on the device.

**Change.** Two parts:

1. Capture the field's value with `readField` *before* the candidate loop. After the
   loop, compare; if it differs, print a prominent warning naming the field, the
   original value, and the current value, and add `"mutated":true` to `env.extra`.
2. Require `--allow-mutation` to run `--pin-gestures` at all. Without it, exit
   `USAGE` with a message explaining the mode writes to the device.

Fix the line-220 comment to say what the code does.

**Why.** An agent exploring capabilities will run this mode. It must not silently
alter a project the user cares about.

**Verify.** `--pin-gestures CUTOFF` without `--allow-mutation` exits 1 and presses
nothing.

---

### Step F18 — Update `docs/tools/m8_nav.md` for Foundation changes

**Files:** `docs/tools/m8_nav.md`

The doc is largely accurate — architecture, build wiring, screen names, and all 16
script verbs check out. Correct these:

- `--max-ms` now works (it was dead); `--min-ms` and `--settle-ms` now mean what the
  table already claims.
- The `--pin-gestures` candidate count is **17**, not 16 (13 edit candidates + 4
  arrows). It appears twice.
- Remove the caveat that `--load-file`'s exit code can't be trusted — unfounded; `rc`
  propagated to `main`'s return. After F7 all codes are `ExitCode` and branchable.
- The `--pin-gestures` section must stop claiming state is restored, and must
  document `--allow-mutation`.
- Replace the exit-code table with the `ExitCode` enum, noting codes are stable.
- New flags: `--semantic-state`, `--serve`, `--allow-mutation`.
- `--record-frames` does not record SLIP — and fix the two places the *source* still
  claims it does (`main_nav.cpp` line 14 header comment, and the `printf` in
  `recordFrames`). The doc was already right; the code is wrong.
- `--json` now includes `highlights`.

---

# TRACK A — File loading and agent ergonomics

`loadFile` (`Primitives.cpp:1088`) does **not** traverse directories. Read lines
1219-1247: it scrolls to the top of the list, scans rows for a substring match, and
presses EDIT. No directory detection, no descent, no `..`. A song in a subfolder is
unreachable — which is the reported symptom, and the agent's workaround of
hand-driving `--keys` is the documented fallback, so it is doing the right thing
with the tool it was given.

`loadFile` keeps its behaviour and stays the fast path for a flat list. The crawler
is new, additive, and separate.

### Step A0 — Decide the SD-card access question

**Files:** append to `docs/tools/hw_findings.md` under `## A0`.

**Change.** Answer, in writing: **can the host machine read the M8's SD card
contents** (card in a reader, or a maintained mirror of it)?

- **Yes** → skip A1-A7. Instead, `--find-file` becomes a host-side directory walk:
  instant, exact, no framebuffer DFS. `m8_nav` only needs "navigate to this known
  filename," which the existing `loadFile` already does once given an exact name.
  Implement that as a small host-side helper plus a `--load-song <exact-name>`
  passthrough, and record the decision here.
- **No** (card lives only in the device) → do A1-A7 as written.

**Why.** This gate decides whether Track A is two steps or seven. The M8 does not
expose its SD card over USB — the official headless setup has you put the microSD
in your computer — so the answer depends on your physical workflow, not on the
firmware.

---

### Step A1 `[HW]` — Discover the directory-row marker

**Files:** create/append `docs/tools/hw_findings.md`

**This step's output is a committed file, not a note in your context.** It is the
step most likely to be silently lost to compaction. Commit it *before* moving to
A2, even if partial.

**Change.** Do not guess how the M8 renders a directory. Capture it:

```
m8_nav --port COM3 --goto-screen PROJECT --semantic-state
m8_nav --port COM3 --keys 0x01 --settle-ms 400
m8_nav --port COM3 --semantic-state --json browser.json
```

Write:

```markdown
# Hardware findings

Empirical observations from a real M8 headless. Each entry records what was
seen, on what firmware, and which spec step needed it. Append; never rewrite.

## A1 — Directory row marker (firmware: ___, date: ___)

- Directory marker: <trailing "/" | leading char | fg/bg colour | other>
- Evidence: <exact row text, or the fg/bg triple from browser.json>
- Parent entry: <".." present? always first row?>
- Header inside a subdirectory: <still "LOADPROJECT"? or the folder name?>
- Status: CONFIRMED | PARTIAL | NOT OBSERVED
```

All four bullets matter:

- The **marker** is what `isDirectoryRow()` encodes. Check `browser.json` for
  per-cell fg/bg — the M8 may distinguish folders by colour rather than any
  character, in which case `ListRow.text` alone cannot detect them and `ListRow`
  needs a colour field.
- The **parent entry** decides whether `upDir()` selects `..` or presses OPT.
- The **subdirectory header** decides whether `identifyScreen` still returns
  `LOAD_PROJECT_MODAL` after descent. If it returns the folder name instead,
  `enterDir`'s verification and every `wait_for_screen LOAD_PROJECT_MODAL` in a
  script are wrong, and `identifyScreen` needs a case.

**Why.** Every later step here depends on this and none is inferable from the repo.
Getting it wrong produces a crawler that silently treats folders as files — the
worst failure shape, because it looks like the file simply isn't there.

**If no hardware:** stop the track here. Still create `hw_findings.md` with
`Status: NOT OBSERVED`, implement A2 with the marker isolated in one function, and
move to Track B or C. Do not implement A3-A7 against a guessed marker.

---

### Step A2 — `isDirectoryRow()`

**Files:** `src/tools/m8/ScreenModel.h`

**Change.** After `listRows()`:

```cpp
// True if a browser list row denotes a directory rather than a file.
//
// UNCONFIRMED — the body below is a GUESS. The real marker is recorded in
// docs/tools/hw_findings.md §A1. If that file says NOT OBSERVED, this
// function is not trustworthy and searchTree() must not be relied on.
// Keep this the ONLY place the convention is encoded.
inline bool isDirectoryRow(const ListRow& r) {
    if (r.text == "..") return true;
    if (!r.text.empty() && r.text.back() == '/') return true;
    return false;
}

inline bool isParentRow(const ListRow& r) { return r.text == ".."; }
```

Replace the body with what `hw_findings.md` §A1 records, and **replace the
`UNCONFIRMED` paragraph with `CONFIRMED (fw X.Y.Z, see hw_findings.md §A1)`** in the
same commit.

**Guardrail.** Do not delete the `UNCONFIRMED` comment while leaving the guessed
body. That comment is the only signal a later reader — or a post-compaction you —
has that this was never validated. A plausible body under a confident comment is
how a guess becomes load-bearing.

**Verify.** Compiles clean. Add an `[hwdecode]` test: `..` and `foo/` are
directories, `song.m8s` is not.

---

### Step A3 — `enumerateList()`

**Files:** `src/tools/m8/Primitives.h`, `src/tools/m8/Primitives.cpp`

**Change.** Declare after `pressUntil`:

```cpp
// ---- List enumeration ------------------------------------------------------

// Enumerate every row of the current browser list, scrolling as needed.
// Scrolls to the top first, then walks DOWN collecting rows until the
// selection stops changing (bottom) or maxRows is hit.
//
// Rows in list order, deduplicated. Leaves the cursor at the bottom; callers
// needing a specific row must navigate afterward.
std::vector<ListRow> enumerateList(M8Device& dev, int holdMs = 15,
                                   int maxRows = 256);
```

Implement:

1. Scroll to top: press UP up to 64 times; break when `cursorMainText()` is
   unchanged across a press. Mirrors the existing loop at `Primitives.cpp:1211`.
2. Loop up to `maxRows`: `dev.readSettled(120, 200, 1200)`, take
   `listRows(dev.grid())`, append rows not already collected, press DOWN. Break when
   a press yields no new rows **and** the selected row text is unchanged.
3. Return the accumulated vector.

**Why.** The viewport shows only part of a long list. Every higher-level operation
needs the whole list, and this is the only place the scroll loop should live.

**Guardrail.** Deduplicate by text; terminate on "no new rows and selection
unchanged," not a fixed press count. A fixed count is the open-loop pattern the
~150ms auto-repeat defeats.

**Verify.** Compiles clean. `[HW]`: on a folder with more entries than fit on
screen, the returned count exceeds the visible row count.

---

### Step A4 — `enterDir()` / `upDir()`

**Files:** `src/tools/m8/Primitives.h`, `src/tools/m8/Primitives.cpp`

```cpp
// Move the cursor to the named directory row and descend. Verifies the list
// contents changed after EDIT; fails if not.
JsonResult enterDir(M8Device& dev, const std::string& dirName, int holdMs = 15);

// Ascend one level (select ".." and press EDIT, or press OPT if this firmware
// backs out that way — see hw_findings.md §A1). Verifies the list changed.
JsonResult upDir(M8Device& dev, int holdMs = 15);
```

Both verify by comparison, not assumption: snapshot the joined text of `listRows()`
before the press; after, require it to **differ**. If not, return
`JsonResult::fail` with the snapshot. Use `confirmRead(dev)` before each comparison
read — it already exists and is exactly the double-read guard needed.

**Why.** Descent is the one operation where a missed press is silently
catastrophic: the crawler thinks it moved, keeps scanning the parent, and either
loops or reports not-found.

---

### Step A5 — `searchTree()`

**Files:** `src/tools/m8/Primitives.h`, `src/tools/m8/Primitives.cpp`

```cpp
struct FileMatch {
    std::string path;   // "/"-separated, relative to the browser root
    std::string name;   // the row text that matched
};

struct SearchResult {
    std::vector<FileMatch> matches;
    bool truncated = false;
    int  dirsVisited = 0;
    std::string error;    // non-empty if navigation broke mid-search
};

// Depth-first search for rows whose name contains `needle` (case-insensitive,
// alnum-normalized — the same comparison loadFile uses).
//
// Collects ALL matches rather than stopping at the first; callers decide about
// ambiguity. Bounded by maxDepth and maxVisits; returns what it found when a
// bound is hit, with `truncated` set. Leaves the browser at an arbitrary
// position — callers must re-navigate.
SearchResult searchTree(M8Device& dev, const std::string& needle,
                        int maxDepth = 4, int maxVisits = 64, int holdMs = 15);
```

Implement iteratively with an explicit stack of directory paths. Per directory:
`enumerateList`, record file matches with the accumulated path, push unvisited
subdirectory names. Skip `..` when descending. Keep a `std::set<std::string>` of
visited paths. After exhausting a directory, `upDir` and verify.

**Guardrail — collect all matches, never pick one.** See Invariant 4. If two
folders both contain a `sunrise`, report both. An agent handed a silent arbitrary
pick loads the wrong song with no way to know.

**Guardrail — bound everything.** `maxDepth`, `maxVisits`, and `enumerateList`'s
`maxRows` are budgets. On exhaustion set `truncated` and return.

---

### Step A6 — `searchAndLoad()` + CLI flags

**Files:** `src/tools/m8/Primitives.h`, `.cpp`, `src/tools/main_nav.cpp`

```cpp
// Search the browser tree for `needle` and load the unique match. AMBIGUOUS-
// shaped failure if >1 match, NOT_FOUND if none. On success the device is left
// on SONG, as loadFile does.
JsonResult searchAndLoad(M8Device& dev, const std::string& needle,
                         int maxDepth = 4, int holdMs = 15);
```

Reach the browser the way `loadFile` does — **extract that prologue** from
`loadFile` into `static JsonResult openLoadBrowser(M8Device&, int)` and have *both*
`loadFile` and `searchAndLoad` call it, so the two cannot drift. Then `searchTree`;
on exactly one match, re-navigate to it, press EDIT, `dismissModal(dev, true)` if a
modal appears, then `gotoScreen(SONG)`.

In `main_nav.cpp`:

- `--find-file <name>` — search only, print all matches, do not load. Multiple →
  `AMBIGUOUS` with `"matches":["a/b","c/d"]` in `env.extra`. None → `NOT_FOUND`.
- `--load-song <name>` — `searchAndLoad`. Propagate `AMBIGUOUS`/`NOT_FOUND` with the
  match list either way.

Both count as mode flags in the F13/line-350 default condition.

**Why.** `--find-file` is the read-only version and an agent should reach for it
first: safe to run speculatively, and it tells the agent whether `--load-song` will
be unambiguous. Splitting search from mutation lets an agent plan without side
effects.

---

### Step A7 `[HW]` — Hardware validation

**Files:** append to `docs/tools/hw_findings.md` under `## A7`.

With an SD card holding at least one song in a subdirectory and one at the root:

1. `--find-file <root_song>` → one match, path has no `/`.
2. `--find-file <nested_song>` → one match, path includes the folder.
3. `--find-file <string_present_twice>` → `"code":6`, both paths listed.
4. `--find-file zzzznotreal` → `"code":5`, empty match list.
5. `--load-song <nested_song>` → `"code":0`, ends on SONG, correct song loaded.
6. Run 1-5 again starting from a *different* screen each time (INSTRUMENT, PHRASE,
   mid-modal). The device does not auto-home; the crawler must work from an unknown
   start.

Record pass/fail per case, plus the `read_ms`/`settled` values. If `settled` is
false on most reads, F4's ceilings need raising further — say so here rather than
tuning silently.

**Why.** Case 6 catches the class of bug that produced `rc=11`.

---

### Step A8 — `wait_for_text` / `wait_for_screen`

**Files:** `src/tools/m8/DeviceScriptRunner.cpp`

The dispatch chain is `else if (toUpper(cmd.verb) == "...")` around lines 106-160
with per-verb handlers below. Follow that pattern exactly.

- `wait_for_text "<substr>" [max_ms N]` — poll `dev.readSettled(120,200,1200)`, scan
  `listRows()` until a row contains the substring (case-insensitive) or the budget
  expires. Default `max_ms` 5000. Failure → `TIMEOUT`-shaped error, nonzero return.
- `wait_for_screen <SCREEN> [max_ms N]` — same loop against `identifyScreen(grid)`.

**Guardrail.** These wait; they do not retry actions. Neither may press a key to
"help" — a verb that both waits and presses can't be reasoned about when it fails.
See Invariant 9.

**Note.** These make timing problems *tolerable*, not fixed. Not a substitute for
the Foundation.

---

### Step A9 — `find_in_list` / `enter_dir` / `up_dir` / `state`

**Files:** `src/tools/m8/DeviceScriptRunner.cpp`

- `find_in_list "<needle>"` — `enumerateList`, then move the cursor onto the
  matching row. `NOT_FOUND` if absent, `AMBIGUOUS` if >1. **Does not press EDIT** —
  Invariant 5.
- `enter_dir "<name>"` → `enterDir`.
- `up_dir` → `upDir`.
- `state` → print `semanticState(dev)`. Never fails.

**Why.** With these, loading a nested song is a script an agent writes in one shot
rather than a conversation:

```
goto PROJECT
cursor PROJECT
key EDIT
wait_for_screen LOAD_PROJECT_MODAL max_ms 3000
enter_dir "sketches"
find_in_list "sunrise"
key EDIT
wait_for_screen SONG max_ms 5000
state
```

**Verify.** Save as `tests/hw/load_nested.m8script`. Parse check offline; `[HW]` run
if a device is attached.

---

### Step A10 — `docs/tools/agent_guide.md`

**Files:** create `docs/tools/agent_guide.md`

Written *for* an agent, not about the tool:

- **Read the envelope, not the prose.** Last line, `M8NAV_RESULT `, branch on `code`.
- **Check `settled`.** If false, re-read before trusting anything.
- **Use named verbs, never raw masks.** `--keys` is a diagnostic. `RIGHT` (`0x04`)
  and `SHIFT+RIGHT` (`0x14`) look alike and fail silently in opposite directions.
- **Search before loading.** `--find-file` is read-only; run it first, and handle
  `AMBIGUOUS` by asking which match was meant.
- **The device does not auto-home.** Never assume a starting screen. Begin with
  `goto` or `state`.
- **One script beats ten invocations.** Each process pays a full framebuffer resend.
- **Notes don't need the UI.** `--keyjazz` (B1) plays a live note directly.
- **`--pin-gestures` writes to the device.** Don't run it speculatively.

Explicitly **do not** include hardcoded SOPs full of key masks. Point at the verbs.
An SOP written in raw hex is a liability and re-teaches the open-loop habit this
design exists to prevent.

---

# TRACK B — Audio-parity orchestration

Thin by design. The device side of the audio loop is "load a probe, play a note,
hand off to `m8_capture`." Everything about *what* the probe contains, and how the
resulting audio is compared to the clone's render, is in
`M8_PROBE_AUTHORING_SPEC.md` — including the currently-open bug that generated
probes play near-silent or silent on hardware.

**Do not start Track B expecting a working parity loop.** Until the authoring spec's
P1-P4 land, the probes themselves are suspect. Track B builds the plumbing; the
authoring spec makes the payload valid.

### Step B1 — `--keyjazz` flag

**Files:** `src/tools/main_nav.cpp`

**Change.** Add `--keyjazz <note>` (MIDI note number, `0x` or decimal) and
`--keyjazz-vel <n>` (default `0x7F`). Call `dev.keyjazz(note, vel)`. Counts as a
mode flag. Match `m8_capture`'s existing flag names exactly so scripts read the
same across both tools.

**Why.** `M8Device::keyjazz` (M8Device.cpp:590) sends `'K' <note> <vel>` and plays a
live note directly on the synth engine, bypassing the sequencer. `m8_capture`
already uses it; `m8_nav` never has. This replaces the deleted `--audition-note`
navigation macro — two bytes instead of six verified UI steps.

**Guardrail.** Do not pick test notes below about MIDI 36. The device-control spec
records MIDI 24-27 producing zero audio on hardware; a sample repitched three
octaves down is plausibly just inaudible, so treating that range as a defect wastes
time.

**Verify.** Compiles clean. `[HW]`: `--keyjazz 60` on a loaded probe produces
audible output.

---

### Step B2 — Probe-play recipe script

**Files:** create `tests/hw/probe_play.m8script`

**Change.** A script that loads a named probe and leaves the device ready for
capture: `goto`, load, `wait_for_screen SONG`, `state`. Keep the actual note
triggering in the `m8_capture` invocation rather than the script, since `m8_capture`
owns the audio timing and needs the note-on inside its own pre-roll window.

**Why.** Splitting "get the device into the right state" (m8_nav, verifiable) from
"trigger and record" (m8_capture, timing-critical) keeps each tool doing what it can
verify.

---

### Step B3 `[HW]` — Validate the handoff

**Files:** append to `docs/tools/hw_findings.md` under `## B3`.

Confirm: `m8_nav --script probe_play.m8script` then `m8_capture --keyjazz 60` in
sequence produces a non-silent WAV. Record the peak. **Do not** interpret the peak
as a parity result yet — see the authoring spec's P0, which establishes whether the
host's recording level makes absolute amplitudes meaningful at all.

**Guardrail.** The COM port is exclusive. `m8_nav` must fully exit before
`m8_capture` opens the port, or the second tool fails to open. If you use `--serve`,
close the session first.

---

# TRACK C — UI-parity capture

## Why this track exists

Reading the device's screens and recreating them in the clone is the one goal the
framebuffer is irreplaceable for. It is also the goal the current tooling is worst
equipped for:

- `printJson` dropped `highlights` entirely (fixed in F11) — so cursor highlight and
  level-bar geometry were invisible to every capture.
- `m8_diffcheck`'s own header says it compares a text grid with "no colors, no
  cursor markers." The one device-vs-clone comparison tool is blind to styles,
  colours, and highlight geometry — fine for "did navigation land on the right
  screen," useless for "does our UI look right."
- Nothing bridges the device's representation (pixel coords + literal RGB) to the
  clone's (`UI_GridCell{text, col, row, "LABEL_DIM"}` — character coords + named
  style). So layout coordinates were transcribed by eye, which is plausibly how
  duplicate "T>" tempo text reached three separate `*ScreenLayout.h` files and why
  OUT_VOL needed a position fix.

**Calibrate expectations.** A capture tells you what pixels appeared for the states
you sampled. It does not give layout *rules* — what happens when a value renders 3
chars instead of 2, what scrolls, what's conditional on instrument type. You cannot
derive the UI from captures; you can validate instances of it. This track gives you
exact coordinates and a strong regression net, not a generator.

---

### Step C1 — `UiCapture.h`

**Files:** create `src/tools/m8/UiCapture.h`

**Change.** Header-only. Define the normalized capture format both sides emit:

```cpp
#pragma once

// ===========================================================================
// UiCapture.h — normalized screen capture for device-vs-clone UI parity.
//
// The device gives pixel coords + literal RGB. The clone stores character
// coords + named styles. This is the common form: character coords, a
// style id derived by clustering the colours observed WITHIN this capture,
// and rects in character units with sub-cell offsets.
//
// Colours are deliberately NOT stored as absolute RGB in the comparable
// body -- the M8 has a user-selectable theme, so absolute values are not
// stable across projects. The observed palette is recorded in the header
// for reference; comparison uses style ids.
// ===========================================================================

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "M8Device.h"
#include "ScreenModel.h"
#include "Result.h"

namespace m8 {
namespace dev {

struct UiCell {
    int col = 0, row = 0;
    char ch = ' ';
    int  fgStyle = -1;   // index into the capture's palette
    int  bgStyle = -1;
};

struct UiRect {
    // Character-grid position, with sub-cell offsets in pixels so a bar that
    // fills part of a cell round-trips.
    int col = 0, row = 0;
    int offsetX = 0, offsetY = 0;
    int wPx = 0, hPx = 0;
    int style = -1;
};

struct UiCapture {
    // Header — recorded, not compared (except where noted).
    std::string screen;         // COMPARED
    std::string firmware;
    int  fontMode = -1;         // COMPARED — refuse to diff across mismatches
    int  pitchX = 0, pitchY = 0;
    bool settled = false;       // must be true for a valid golden
    std::string themeId;        // COMPARED — see C6

    // Palette: distinct colours observed, in canonical (sorted) order.
    // Style ids index this. Recorded for reference; not compared directly.
    std::vector<std::array<uint8_t,3>> palette;

    // Body — compared.
    std::vector<UiCell> cells;
    std::vector<UiRect> rects;
};

// Serialize / parse. Stable field order so a plain text diff is readable.
std::string toJson(const UiCapture& c);
bool fromJson(const std::string& text, UiCapture& out, std::string& err);

} // namespace dev
} // namespace m8
```

Put the `toJson`/`fromJson` bodies in a new `src/tools/m8/UiCapture.cpp` and add it
to the `m8_device` library sources in `CMakeLists.txt` (line 178-183) — this is the
one step in this document that touches CMake.

**Why.** Without a shared format, device captures and clone renders can't be
diffed mechanically, and layout data stays hand-transcribed.

**Guardrail.** `settled` must be part of the format, and C3 must refuse to write a
capture with `settled == false`. See Invariant 10: the display stream is
incremental, so an unsettled read can capture a partial repaint, and freezing that
as a golden bakes the partial frame in permanently.

**Verify.** Compiles clean; `m8_device` still links with no engine/SDL/audio.

---

### Step C2 — Style clustering

**Files:** `src/tools/m8/UiCapture.h` / `.cpp`

**Change.** Add:

```cpp
// Build the palette by collecting distinct fg/bg triples across all cells and
// rects, sorted canonically (by r, then g, then b) so the same screen always
// produces the same style ids regardless of decode order.
//
// Exact-match clustering only -- the M8 renders flat colours, so there is no
// need for tolerance, and a tolerance parameter would silently merge two
// styles that genuinely differ by a small amount.
void buildPalette(UiCapture& c);
```

**Why.** Each distinct colour in a capture *is* a style. Clustering gives you
`LABEL_DIM` vs `LABEL_LITE` vs `TITLE` objectively rather than by eye, and it makes
the comparable body theme-independent: change the theme and the RGB values move, but
the *partition* of cells into styles does not.

**Guardrail.** Sort canonically, not by first-seen order. First-seen order depends
on decode sequence, which would make two captures of the same screen produce
different style ids and diff spuriously.

**Verify.** Offline `[hwdecode]` test: two grids with the same cells fed in
different orders produce identical palettes and identical style ids.

---

### Step C3 — `--ui-capture` flag

**Files:** `src/tools/main_nav.cpp`

**Change.** Add `--ui-capture <path>`. Build a `UiCapture` from the current grid
(convert pixel coords to char coords using `ScreenGrid::detectPitch`, populate
`cells` from `grid().cells` and `rects` from `grid().highlights`, call
`buildPalette`), write JSON to `path`.

**If `dev.lastRead().settled` is false, do not write the file** — set
`env.code = ExitCode::UNSETTLED` and a message saying the read never settled.

Composable with other mode flags, the same way `--semantic-state` is: `--goto-screen
MIXER --ui-capture mixer.json` captures post-navigation.

**Why.** Composability is what makes C7's sweep a single scripted pass.

**Verify.** `[HW]`: `--goto-screen MIXER --ui-capture out.json` writes a file whose
`rects` array is non-empty (the mixer has level bars). That non-empty array is the
proof F11 was worth doing.

---

### Step C4 — Clone-side emitter

**Files:** `src/ui/Renderer.h`, `src/ui/Renderer.cpp`

**Change.** `Renderer` already has `writeGolden(path)` / `compareGolden(path, detail)`
(Renderer.h:68, 73) doing a per-cell *text* snapshot. Add a sibling that emits the
same `UiCapture` JSON: character coords, styles clustered from the clone's own
rendered colours, and rects for anything the clone fills.

**Guardrail — the invariant.** `m8_device` links no engine and no SDL. The
dependency must run **one way only**: the clone's `Renderer` may include
`UiCapture.h`, but `UiCapture.h` must not include anything from `src/ui/` or
`src/engine/`. Keep `UiCapture.h`'s includes limited to `M8Device.h`,
`ScreenModel.h`, `Result.h`, and the standard library. If you find yourself wanting
a UI type inside `UiCapture.h`, that is the signal you've put the abstraction in the
wrong place.

**Why.** Once both sides emit the same artifact, comparison is a file diff of two
machine-generated files rather than a device dump against a hand-typed text file.

**Verify.** `--target m8_clone` and `--target m8_device` both compile; `m8_device`
still has no SDL dependency (`AGENTS.md` §"The engine has zero SDL dependencies").

---

### Step C5 — `m8_diffcheck` compares `UiCapture` files

**Files:** `src/tools/main_diffcheck.cpp`

**Change.** Add a mode that reads two `UiCapture` JSON files and reports
differences: cells present in one and not the other, cells differing in `ch` or
style, and rects differing in position/size/style. Report the first N divergences
with char coordinates, not the whole diff.

Refuse to compare when `fontMode` or `themeId` differ, or when either capture has
`settled == false`. Exit `USAGE` with an explanatory message in those cases.

Keep the existing text-grid mode — it's still the right tool for "did navigation
land on the right screen."

**Why.** Refusing rather than diffing across font modes and themes is the point: a
mismatched-mode diff produces hundreds of spurious differences that look like real
UI bugs and burn a session.

---

### Step C6 `[HW]` — Theme and font pinning

**Files:** append to `docs/tools/hw_findings.md` under `## C6`.

**Change.** The M8 has a user-selectable theme (Theme View) and multiple font modes.
Both change what a capture contains without any UI logic changing.

Determine and record:

- Which theme the parity corpus will be captured under, and how to restore it
  (which project carries it, or the exact Theme View values).
- The `fontMode` value that theme/project reports via the `0xFF` sysinfo.
- Whether the clone's default theme matches, and if not, what the clone needs set to
  match.

Then define `themeId` as a short string you set on both sides (e.g. `"m8-default-6.5.2"`).

**Why.** This is the trap that invalidates a whole corpus silently. Capture under
theme A, encode style ids, then capture under theme B and every screen mismatches.
C2's within-capture clustering makes the *comparable body* theme-independent, but
the palette and any absolute-colour reasoning are not — and if the two themes
partition cells into different *numbers* of styles, even the body diverges.

---

### Step C7 — Sweep script

**Files:** create `tools/ui_sweep.ps1` (or `.py`) plus
`tests/hw/ui_sweep.m8script`

**Change.** Drive the daemon (F15/F16) through every state worth capturing and write
one `UiCapture` per state into a corpus directory:

- All 12 screens from `kScreenTable`.
- On each screen, the cursor at each field in its field map (`getFieldMap`).
- Modal open / closed where reachable.
- Playing / stopped.

Name files deterministically: `<screen>__<cursor-field>__<modifiers>.json`.

**Why.** This is the corpus that turns UI parity from per-screen eyeballing into a
batch diff. It is also the step that justifies the daemon: hundreds of captures at
one process and one full framebuffer resend each is painful; in a resident session
it's one pass.

**Guardrail.** Every capture must come from a settled read. If the sweep hits an
`UNSETTLED` result, it must record the failure and continue, then report the list of
states it could not capture — not silently skip them and leave gaps in the corpus
that look like "not covered yet."

---

### Step C8 `[HW]` — Capture the corpus

**Files:** `tests/ui/golden/device/` (new directory), plus a `## C8` entry in
`hw_findings.md`

**Change.** Run C7 against real hardware under the C6-pinned theme and font mode.
Commit the corpus. Record: number of states captured, number that failed to settle,
firmware, theme id.

Then run the clone-side emitter (C4) over the same states and diff (C5). Record the
count of diverging states — that number is your UI parity baseline, and the work
item list for recreating the UI.

**Why.** Everything up to here is machinery. This is the step that produces the
answer to "how far off is our UI, and where."

---

## What this spec deliberately does not do

- **No caching of device state in the driver.** Every primitive re-reads. The ~150ms
  auto-repeat and no-auto-home behaviour mean a cache desyncs, and a
  confidently-wrong cache is worse than a slow read.
- **No new `gotoScreen` implementation.** `loadFile` already regressed once by
  growing a parallel navigation loop (see the hardware-confirmed comment at
  `Primitives.cpp:1115-1126`). Reuse, don't reimplement.
- **No macro that hides which stage failed.** Macros return the first failing
  sub-result verbatim.
- **No changes to `readScreen`'s or `read`'s signatures.** ~40 correct call sites;
  new code uses `readSettled`.
- **No song creation or parameter editing through the UI.** That's offline `.m8s`
  authoring — `M8_PROBE_AUTHORING_SPEC.md`.
- **No absolute-RGB comparison in UI parity.** Themes move colours; comparison uses
  within-capture style partitions.
