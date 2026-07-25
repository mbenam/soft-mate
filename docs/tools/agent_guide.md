# Agent & Subagent Usage Guide for `m8_nav`

**Source:** [`src/tools/main_nav.cpp`](../../src/tools/main_nav.cpp)  
**Specification:** [`M8_DRIVER_SPEC.md`](../../M8_DRIVER_SPEC.md)

This guide documents the recommended interfaces, flags, daemon protocol, and scripting verbs for AI agents driving an M8 headless hardware device over serial.

---

## 1. Overview & Architectural Rules

- **Serial Only:** `m8_nav` communicates strictly over USB serial (SLIP-framed display decoding + button input). It links no engine, no SDL, and no audio.
- **Closed-Loop Perception:** Never assume a button press succeeded without reading back the screen state. All high-level primitives perform double-read confirmation (`readSettled`).

---

## 2. High-Level Perception & Commands

### `--semantic-state`
Emits a single compact JSON object describing current device perception:
```json
{
  "screen": "SONG",
  "is_modal": false,
  "is_live_mode": false,
  "settled": true,
  "cursor_field": "TEMPO",
  "cursor_value": "120.00",
  "cursor_row": 50,
  "rows": [
    {"y": 30, "text": " SONG"},
    {"y": 50, "text": "TEMPO        120.00"}
  ]
}
```

### File Discovery & Loading
- **`m8_nav --port COM4 --find-file <pattern>`**: Performs bounded depth-first directory tree search across SD card folders to find matching files.
- **`m8_nav --port COM4 --load-song <pattern>`**: Automatically opens the `LOAD PROJECT` modal, searches the tree, navigates to the matching file, and opens it closed-loop.

---

## 3. Persistent Session Daemon (`--serve`)

Opening serial port per command incurs a ~700ms reset-and-resend delay. For automated multi-step workflows, use the resident daemon mode:

```bash
m8_nav --port COM4 --serve
```

### Protocol Format
Send line-delimited commands to `stdin`:
```
ACTION key=value key=value ...
```

### Supported Actions
- `press key=<mask_or_name> [hold=<ms>]` — Press button(s) (e.g. `press key=SHIFT+DOWN`, `press key=0x01`).
- `goto screen=<name>` — Closed-loop navigation to target screen (e.g. `goto screen=INSTRUMENT`).
- `cursor field=<name>` — Move cursor to named field (e.g. `cursor field=CUTOFF`).
- `read field=<name>` — Read adjacent value for field.
- `load path=<file>` — Load project file.
- `state` / `semantic_state` — Refresh perception and output JSON state.
- `quit` / `exit` — Terminate daemon session.

### Response Format
Outputs one JSON response line per command to `stdout`:
```json
{
  "ok": true,
  "code": 0,
  "state": { ... }
}
```

---

## 4. `.m8script` Verbs

When driving `.m8script` files via `m8_nav --script <path>`, the following verbs are supported:

| Verb | Syntax | Meaning |
|---|---|---|
| `goto` | `goto <screen>` | Navigate to screen (`SONG`, `CHAIN`, `PHRASE`, `INSTRUMENT`, `PROJECT`, etc.) |
| `cursor` | `cursor <field>` | Move cursor to field |
| `key` | `key <button>` | Press button (e.g. `key SHIFT+DOWN`, `key EDIT`) |
| `wait` | `wait <ms>` | Pause execution for N milliseconds |
| `wait_for_screen` | `wait_for_screen <screen>` | Poll until expected screen is reached |
| `wait_for_text` | `wait_for_text <substring>` | Poll until substring appears on screen |
| `find_in_list` | `find_in_list <pattern>` | Search directory tree for pattern |
| `enter_dir` | `enter_dir <folder>` | Descend into named folder row |
| `up_dir` | `up_dir` | Ascend to parent directory |
| `state` | `state` | Output semantic state JSON |
| `assert_screen` | `assert_screen <screen>` | Assert current screen identity |
| `assert_field` | `assert_field <field> "<value>"` | Assert field value text |

---

## 5. Exit Codes (`ExitCode` enum)

| Code | Name | Meaning |
|---|---|---|
| 0 | `SUCCESS` | Operation completed successfully |
| 1 | `DEVICE_NOT_FOUND` | Serial port could not be opened |
| 2 | `UNKNOWN_ARG` | Bad CLI flag or missing argument |
| 3 | `UNSETTLED_DISPLAY` | Display unreadable / no characters decoded |
| 4 | `COMMAND_FAILED` | Command or script assertion failed |
| 5 | `TIMED_OUT` | Timed out waiting for display settle |
| 6 | `AMBIGUOUS_MATCH` | Screen or file match was ambiguous |
| 7 | `TARGET_UNREACHABLE` | Target screen, field, or directory path unreachable |
