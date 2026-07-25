# M8 Driver Spec — Addendum A: Post-Implementation Gaps

Follow-on to `M8_DRIVER_SPEC.md`, which is now 40/40 ticked. This addendum covers
gaps found by auditing the *implemented* code against what those steps specified.

The gaps are not implementation failures in general — the Foundation and Track A work
is solid, and F7 was implemented better than specified (an `emitExit()` helper instead
of the `goto done` pattern, which sidesteps the MSVC jump-across-initialization
problem entirely). These are three specific places where the delivered behaviour is
narrower than the step described, plus one process fix.

> **Why a new document rather than unticking steps in the original.** A ticked step
> with a modified body is ambiguous — a later reader can't tell whether the tick
> covers the original text or the revision. Fresh unticked steps with their own ids
> are unambiguous.

### Progress checklist

```
[x] D1  Restore the lost exit-code distinctions
[x] D2  Add the `extra` payload field to Envelope
[x] D3  Fix the settle/max naming at main_nav.cpp:614
[x] D4  Route ui_sweep through the daemon
[ ] D5  Expand the sweep to per-field cursor states
[ ] D6  Add modal and transport states to the corpus
[ ] D7  Re-verify the ticked steps whose output nobody saw
[ ] D8  Update docs for D1-D6
```

Same discipline: one commit per step prefixed with the step id, tick in this file in
the same commit, re-read the step before starting, stop when a Verify fails.

---

### Step D1 — Restore the lost exit-code distinctions

**Files:** `src/tools/m8/Result.h`, `src/tools/main_nav.cpp`

**Change.** The delivered enum is:

```cpp
SUCCESS=0, DEVICE_NOT_FOUND=1, UNKNOWN_ARG=2, UNSETTLED_DISPLAY=3,
COMMAND_FAILED=4, TIMED_OUT=5, AMBIGUOUS_MATCH=6, TARGET_UNREACHABLE=7
```

Two distinctions were collapsed. Restore both:

1. **Add `NOT_FOUND`.** Right now `--find-file zzzznotreal` (the file does not exist)
   and "could not drive the UI to the browser" both return `COMMAND_FAILED`. Add a
   distinct code and route every genuine no-match through it: `searchTree` returning
   zero matches, `readField` on an absent field, `find_in_list` with no hit.
2. **Split `DEVICE_NOT_FOUND`.** It currently covers both "the serial port would not
   open" and "the port opened but nothing decoded." Add `NO_DATA` for the second.

Append new values rather than renumbering — `AMBIGUOUS_MATCH=6` and friends are
already recorded in `hw_findings.md` §A7 output and in `agent_guide.md`. Renumbering
invalidates those records.

**Why.** These are the two branches an agent most needs to distinguish. "The file
doesn't exist" means stop and tell the user; "I couldn't drive the UI there" means
retry or re-orient. Collapsing them into one code defeats a primary purpose of the
envelope — an agent that retries a nonexistent filename three times and then reports a
hardware problem is the exact failure this was built to prevent. Likewise, "no port"
is a wiring problem and "no data" is a device-state problem, and they get fixed
differently.

**Verify.** `--find-file zzzznotreal` against real hardware returns the new
`NOT_FOUND` code, and `--find-file <real-file>` still returns 0. `--port COM99`
returns `DEVICE_NOT_FOUND`, distinct from a port that opens with a powered-off device.

---

### Step D2 — Add the `extra` payload field to `Envelope`

**Files:** `src/tools/m8/Result.h`, `src/tools/main_nav.cpp`

**Change.** The delivered `Envelope` carries `code`, `message`, `readStats`,
`screenName`, `cursorField`, `cursorText` — but no free-form payload. Add:

```cpp
    // Pre-formatted "key":<json-value> pairs appended to the emitted object.
    std::vector<std::string> extra;
```

and emit them in `emitExit`. Then use it where the driver spec's A6 said to: on
`AMBIGUOUS_MATCH`, attach `"matches":["a/b","c/d"]`; on `--find-file`, attach the
match list and `"dirs_visited"`/`"truncated"`.

**Why.** `AMBIGUOUS_MATCH` without the candidate list is close to useless. An agent
told "there are several matches" and not which ones has to re-run the search with
different phrasing, or ask the user a question it can't frame. The match list is
currently printed as human prose to stdout, which means the agent has to parse the
prose the envelope exists to replace.

**Verify.** `--find-file <string-matching-two-files>` emits an envelope whose
`matches` array contains both paths, and the process exit code is `AMBIGUOUS_MATCH`.

---

### Step D3 — Fix the settle/max naming at `main_nav.cpp:614`

**Files:** `src/tools/main_nav.cpp`

**Change.** Line 614 reads `dev.readSettled(120, 150, settleMs);` — so `--settle-ms`
is driving `maxMs` at this site while a hardcoded `150` drives the settle window. It
works (150 < the 250 default, so the settle branch can fire), but the flag does not
mean what its name and the docs say.

Change to `dev.readSettled(120, settleMs, maxMs);` to match line 439.

**Why.** F3 existed to end exactly this confusion. Leaving one site where
`--settle-ms` secretly means "timeout" guarantees someone re-derives the original bug
while debugging a `--keys` sequence.

**Guardrail.** Check the resulting values are sane: with defaults this becomes
`(120, 250, 2500)`, which is fine. If a caller passes `--settle-ms` larger than
`--max-ms`, the settle branch can never fire — add a startup check that rejects
`settleMs >= maxMs` with `UNKNOWN_ARG` and an explanatory message, since that
combination is always a mistake.

**Verify.** `grep -n "readSettled" src/tools/main_nav.cpp` shows both sites using
`settleMs, maxMs` in that order. `--settle-ms 3000 --max-ms 1000` is rejected.

---

### Step D4 — Route `ui_sweep` through the daemon

**Files:** `tools/ui_sweep.ps1`

**Change.** The script spawns a fresh `m8_nav` process per screen (`--goto-screen X
--ui-capture X.json`). At 11 screens that's tolerable. D5 and D6 multiply the state
count by roughly the field-count per screen, at which point process-per-capture is
the dominant cost — each one re-opens the port and waits for a full framebuffer `'R'`
resend.

Convert the sweep to open one `--serve` session and drive it with `goto` / `capture`
commands over stdin.

**Why.** F15/F16 built the daemon specifically so the sweep would be cheap, and the
sweep doesn't use it. Do this **before** D5, not after — expanding the state count
first and then optimising means one painfully slow corpus run in between.

**Verify.** The sweep produces a byte-identical corpus to the current one (modulo
`read_ms`), in a single process. Confirm `'R'` is sent once per run.

---

### Step D5 — Expand the sweep to per-field cursor states

**Files:** `tools/ui_sweep.ps1`, `tests/ui/golden/device/`

**Change.** The corpus is 11 files, one per screen. Driver spec step C7 specified "on
each screen, the cursor at each field in its field map (`getFieldMap`)." Add that
dimension: for each screen, walk the cursor to each field and capture.

Keep the existing naming convention and extend it:
`<SCREEN>__<CURSOR-FIELD>.json`.

**Why this is the gap that matters most.** Highlight and cursor geometry is precisely
what F11 unlocked by making `printJson` emit `highlights` — and it's the class of bug
the repo has already shipped once, in `MixerScreen.cpp`'s output-bar fill. A corpus
with one capture per screen exercises the cursor in exactly one position per screen,
so the capability F11 added is barely tested. You built the instrument and the corpus
doesn't use it.

**Guardrail.** Every capture must come from a settled read. On `UNSETTLED_DISPLAY`,
record the failure and continue, then report the list of states that could not be
captured. Do not skip silently — a gap in the corpus that looks like "not covered
yet" is worse than a recorded failure.

**Verify.** Corpus file count is roughly the sum of field-map sizes across screens,
not 11. Spot-check two captures of the same screen with different cursor positions:
their `rects` arrays differ.

---

### Step D6 — Add modal and transport states

**Files:** `tools/ui_sweep.ps1`, `tests/ui/golden/device/`

**Change.** The sweep's screen list explicitly skips modals. Add the reachable ones
(`LOAD_PROJECT_MODAL` and any other in `kScreenTable` marked modal), and add a
playing/stopped dimension for the screens where transport state changes what renders
— SONG, CHAIN, PHRASE at minimum.

Extend naming to `<SCREEN>__<CURSOR-FIELD>__<MODIFIERS>.json`.

**Guardrail.** Capturing a modal means opening one, which on some screens means
touching device state. Use only read-only modals, or restore state afterward and
verify the restore — do not leave the sweep mutating projects. If a modal can't be
opened without a write, record it as uncovered rather than writing to the device.

**Verify.** Corpus contains at least one modal capture and one playing-state capture,
and re-running the sweep twice produces the same corpus (proving it isn't mutating
state between runs).

---

### Step D7 — Re-verify the ticked steps whose output nobody saw

**Files:** `M8_DRIVER_SPEC.md`, `M8_PROBE_AUTHORING_SPEC.md`, append `## D7` to
`docs/tools/hw_findings.md`

**Change.** Both specs report 100% complete. At least one ticked step — C7 — was
delivered narrower than its text describes, and the tick gives no signal of that.

Re-run the **Verify** line of every step whose output is not already recorded in
`hw_findings.md` or a committed artifact. Record per step: pass, fail, or
"delivered narrower than specified — see D<n>". Where a step was under-delivered and
is not covered by D1-D6, add it to this addendum's checklist.

**Why.** Ticking on intent rather than on a passed Verify is the failure mode the
compaction section was written to prevent, and it happened here without any
compaction — just optimism. A checklist that over-reports is worse than no checklist,
because it stops anyone looking.

**Guardrail.** Do not untick anything in the original specs. Record findings here.
See the note at the top of this document.

---

### Step D8 — Update the docs

**Files:** `docs/tools/m8_nav.md`, `docs/tools/agent_guide.md`

**Change.**

- Exit-code table: add `NOT_FOUND` and `NO_DATA`, and state that codes are append-only
  and stable.
- Document the `extra` payload and the `matches` array on `AMBIGUOUS_MATCH`.
- `agent_guide.md`: add the rule that `NOT_FOUND` means *stop and ask*, while
  `TARGET_UNREACHABLE` and `COMMAND_FAILED` mean *re-orient with `state` and retry
  once*. That branch is the practical payoff of D1 and an agent won't infer it.
- Note the `settleMs >= maxMs` rejection from D3.
