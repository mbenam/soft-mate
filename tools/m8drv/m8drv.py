#!/usr/bin/env python3
"""
m8drv -- unattended Python driver for a real M8 headless.

WHY THIS EXISTS
---------------
`m8_nav` already contains the hard-won part: the SLIP display decoder, the field
maps, the read-verify-act primitives, and the fixes for 19 hardware-confirmed
bugs (specs/M8_DRIVER_BUGS.md). What it lacked was a way to drive all that
*without a human in the loop*.

The failure mode this fixes: every one-shot `m8_nav --flag` invocation is its own
process, and M8Device::open/close (src/tools/m8/M8Device.cpp:491) does

    open():  'E' -> sleep 500ms -> 'R'   (enable display, full display reset)
    close(): 'D'                         (disconnect)

so a sequence of N CLI calls is N connect / display-reset / disconnect cycles.
Every read lands on a freshly reset framebuffer and any per-connection device
state dies between presses. That looks exactly like "the device stopped
accepting keys" when nothing is actually wrong with it.

This client instead talks to `m8_nav --serve`: ONE process, ONE connection, many
commands, with semantic state JSON on every reply.

WHAT MAKES IT UNATTENDED
------------------------
1. Every command has a timeout.
2. On timeout the daemon is killed and restarted -- necessary, not lazy: when
   the C++ side is spinning inside a primitive it is not reading stdin, so no
   command can reach it. Killing releases COM3; restart re-runs the 'E'/'R'
   handshake.
3. After a restart we send a key RELEASE first ('C' 0x00) in case the kill
   interrupted a press between mask-down and mask-up, leaving a key stuck.
4. Then HOME (Primitives::panicHome) to escape whatever state we were in.
5. Fields that provably cannot be driven are refused up front instead of
   thrashing for 30s (see FENCED_FIELDS).

Nothing here needs a display, a browser, or a human hand on the device.

USAGE
-----
    python tools/m8drv/m8drv.py doctor
    python tools/m8drv/m8drv.py dump
    python tools/m8drv/m8drv.py goto INSTRUMENT
    python tools/m8drv/m8drv.py read CUTOFF
    python tools/m8drv/m8drv.py set CUTOFF 40
    python tools/m8drv/m8drv.py press SHIFT+RIGHT
    python tools/m8drv/m8drv.py home
    python tools/m8drv/m8drv.py repl

As a library:

    from m8drv import M8Driver
    with M8Driver(port="COM3") as d:
        d.goto("INSTRUMENT")
        print(d.read_field("CUTOFF"))
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from typing import Any, Dict, List, Optional

# --- device constants -------------------------------------------------------

DEFAULT_PORT = "COM3"
DEFAULT_EXE = os.path.join("build", "Release", "m8_nav.exe")

# Repo root, derived from this file's location (<root>/tools/m8drv/m8drv.py).
# The daemon MUST run with this as its working directory: m8_nav loads its edit
# gestures from the bare relative path "hw_buttons.json"
# (src/tools/main_nav.cpp:436) and has no flag to override it. Without them,
# editValue refuses every SET with "gestures not pinned".
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GESTURE_FILE = "hw_buttons.json"

# Key masks, pinned on firmware 6.5.2 and recorded in hw_buttons.json.
# `Key::` in src/tools/m8/M8Device.h is the authority.
KEYS = {
    "LEFT": 0x80, "UP": 0x40, "DOWN": 0x20, "SHIFT": 0x10,
    "PLAY": 0x08, "RIGHT": 0x04, "OPT": 0x02, "EDIT": 0x01,
}

# Per-verb timeouts in seconds. These are generous on purpose: editValue can
# step a byte up to 256 times at ~70ms per step plus a screen read each, and
# LOAD does a depth-first tree search over the SD card.
TIMEOUTS = {
    "PRESS": 20, "STATE": 20, "FIELDS": 20, "CURSOR": 60, "READ": 60,
    "GOTO": 90, "SET": 150, "NOTE": 60, "KEYJAZZ": 20, "HOME": 90,
    "LOAD": 180, "SCRIPT": 300, "CAPTURE": 60,
}
DEFAULT_TIMEOUT = 30

# --- what cannot be driven, and why ----------------------------------------

# M8_DRIVER_BUGS.md #20, status OPEN. This widget's navigation is not a pure
# function of (current field, key pressed) -- it depends on state the M8 keeps
# per column group, proven by a fully isolated hop-by-hop path producing a
# DIFFERENT result on identical re-test. A fixed key sequence cannot reach these
# reliably in any language over any transport, so refuse immediately rather than
# thrash. DJF_TYP could not be located on-device at all.
FENCED_FIELDS = {
    "MST_CHO": "MIXER compound widget (bug #20, OPEN)",
    "MST_DEL": "MIXER compound widget (bug #20, OPEN)",
    "MST_REV": "MIXER compound widget (bug #20, OPEN)",
    "MIX_VOL": "MIXER compound widget (bug #20, OPEN)",
    "LIM_VAL": "MIXER compound widget (bug #20, OPEN)",
    "DJF_FREQ": "MIXER compound widget (bug #20, OPEN)",
    "DJF_RES": "MIXER compound widget (bug #20, OPEN)",
    "DJF_TYP": "never located on-device (bug #20, OPEN)",
}

# M8_DRIVER_BUGS.md #21, status OPEN. TYPE's row also carries an unmapped
# LOAD/SAVE button pair at a higher column. If the cursor is ALREADY parked on
# that button, identifyCursorField's "col <= gridCol, greatest wins" rule reports
# "already on target" without checking the column, and the edit presses then
# operate on the wrong widget. It only bites from a dirty cursor position, so the
# fix is to force a known position first rather than to refuse the field.
HOME_FIRST_FIELDS = {"TYPE"}


class M8Error(RuntimeError):
    pass


class FencedField(M8Error):
    """Raised for a field that provably cannot be driven. Not retryable."""


class M8Driver:
    """A supervised `m8_nav --serve` session."""

    # 40ms is m8_nav's own CLI default for --hold-ms. The Primitives signatures
    # default to 15, but that is the value --load-file clamps to, not a general
    # one -- do not carry it over as this client's default.
    def __init__(self, port: str = DEFAULT_PORT, exe: str = DEFAULT_EXE,
                 hold_ms: int = 40, verbose: bool = False,
                 auto_recover: bool = True):
        self.port = port
        self.exe = exe
        self.hold_ms = hold_ms
        self.verbose = verbose
        self.auto_recover = auto_recover
        self.proc: Optional[subprocess.Popen] = None
        self._q: "queue.Queue[Dict[str, Any]]" = queue.Queue()
        self._noise: List[str] = []
        self._reader: Optional[threading.Thread] = None
        self.restarts = 0
        self.banner: Dict[str, str] = {}
        self.gestures_ready: Optional[bool] = None
        self.firmware: Optional[str] = None
        self._startup_error: Optional[str] = None
        self._ready = threading.Event()

    # -- lifecycle ----------------------------------------------------------

    def __enter__(self) -> "M8Driver":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    def _resolve_exe(self) -> str:
        for cand in (self.exe, os.path.join(REPO_ROOT, self.exe)):
            if os.path.isfile(cand):
                return os.path.abspath(cand)
        found = shutil.which(os.path.basename(self.exe))
        if found:
            return found
        raise M8Error(
            f"m8_nav not found at {self.exe!r}. Build it first:\n"
            f"  cmake --build build --config Release --target m8_nav")

    def start(self, wait_ready: float = 20.0) -> None:
        exe = self._resolve_exe()
        cmd = [exe, "--port", self.port, "--serve", "--hold-ms", str(self.hold_ms)]
        self._log(f"starting: {' '.join(cmd)} (cwd={REPO_ROOT})")
        self.banner = {}
        self.gestures_ready: Optional[bool] = None
        self.firmware: Optional[str] = None
        self._startup_error: Optional[str] = None
        self._ready = threading.Event()
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1,
            cwd=REPO_ROOT,   # so hw_buttons.json resolves -- see REPO_ROOT above
        )
        self._q = queue.Queue()
        self._noise = []
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

        # Fail fast instead of waiting out a command timeout. m8_nav prints its
        # banner and then either enters the daemon loop or exits with a
        # single M8NAV_RESULT line (e.g. NO_DATA when the device is powered off).
        deadline = time.time() + wait_ready
        while time.time() < deadline:
            if self._startup_error:
                err = self._startup_error
                self._kill()
                raise M8Error(f"m8_nav failed to start: {err}")
            if self._ready.is_set():
                break
            if self.proc.poll() is not None:
                self._kill()
                raise M8Error(
                    "m8_nav exited during startup: "
                    + (self._startup_error or "; ".join(self._noise[-4:]) or "no output"))
            time.sleep(0.05)

        if not os.path.isfile(os.path.join(REPO_ROOT, GESTURE_FILE)):
            self._log(f"WARNING: {GESTURE_FILE} missing from {REPO_ROOT}; SET will be refused")
        if self.gestures_ready is False:
            self._log("WARNING: gestures not pinned; SET/NOTE will be refused by editValue")

    def stop(self) -> None:
        if not self.proc:
            return
        try:
            if self.proc.poll() is None and self.proc.stdin:
                self.proc.stdin.write("QUIT\n")
                self.proc.stdin.flush()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
        except (OSError, ValueError):
            self.proc.kill()
        finally:
            self.proc = None

    def _kill(self) -> None:
        if self.proc:
            try:
                self.proc.kill()
                self.proc.wait(timeout=5)
            except Exception:
                pass
            self.proc = None

    # -- transport ----------------------------------------------------------

    def _read_loop(self) -> None:
        """Accumulate pretty-printed JSON objects off the daemon's stdout.

        The daemon writes multi-line indented JSON, so we cannot assume one
        reply per line. It can also emit non-JSON text (the SCRIPT verb's
        dump_screen prints a raw grid), so we only accumulate once a line opens
        an object and we park anything else in self._noise.
        """
        proc = self.proc
        if not proc or not proc.stdout:
            return
        buf = ""
        for line in proc.stdout:
            if not buf:
                if line.lstrip().startswith("{"):
                    buf = line
                else:
                    if line.strip():
                        self._noise.append(line.rstrip())
                        self._scan_banner(line.rstrip())
                    continue
            else:
                buf += line
            try:
                self._q.put(json.loads(buf))
                buf = ""
            except json.JSONDecodeError:
                # Incomplete object; keep accumulating. Guard against a runaway
                # buffer if the stream desynchronises.
                if len(buf) > 512 * 1024:
                    self._noise.append("[dropped oversized non-JSON buffer]")
                    buf = ""

    def _scan_banner(self, line: str) -> None:
        """Turn m8_nav's plain-text startup banner into structured facts.

        Worth doing rather than discarding: the firmware version is printed here
        and is NOT present in SemanticState's JSON, and the gestures line is the
        only signal that SET/NOTE are actually usable.

            serial: COM3 opened @115200
            gestures: loaded from hw_buttons.json (fw 6.5.2, populated=true)
            gestures: not loaded (file missing or no edit gestures pinned)
            device: hw_type=1  firmware=6.5.2  font_mode=0
        """
        if line.startswith("serial:"):
            self.banner["serial"] = line
        elif line.startswith("gestures:"):
            self.banner["gestures"] = line
            if "not loaded" in line:
                self.gestures_ready = False
            elif "populated=" in line:
                self.gestures_ready = "populated=true" in line
        elif line.startswith("device:"):
            self.banner["device"] = line
            for tok in line.split():
                if tok.startswith("firmware="):
                    self.firmware = tok.split("=", 1)[1]
            # The banner's last line before the daemon loop starts.
            self._ready.set()
        elif line.startswith("M8NAV_RESULT"):
            # Startup aborted (e.g. NO_DATA: port opened but nothing decoded).
            payload = line[len("M8NAV_RESULT"):].strip()
            try:
                obj = json.loads(payload)
                if not obj.get("ok", True):
                    self._startup_error = obj.get("message") or payload
            except json.JSONDecodeError:
                self._startup_error = payload

    def _write(self, line: str) -> None:
        if not self.proc or self.proc.poll() is not None:
            raise M8Error("daemon is not running")
        assert self.proc.stdin
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def send(self, verb: str, timeout: Optional[float] = None,
             _allow_recover: bool = True, **params: Any) -> Dict[str, Any]:
        """Send one command and return its parsed reply.

        On timeout, recovers (kill / restart / release keys / HOME) and raises.
        The caller decides whether to retry; we do not silently repeat a command
        that may have already had an effect on the device.
        """
        verb = verb.upper()
        timeout = timeout if timeout is not None else TIMEOUTS.get(verb, DEFAULT_TIMEOUT)
        parts = [verb] + [f"{k}={v}" for k, v in params.items() if v is not None]
        line = " ".join(str(p) for p in parts)

        # Drop any stale replies so we cannot mistake an old one for ours.
        while not self._q.empty():
            try:
                self._q.get_nowait()
            except queue.Empty:
                break

        self._log(f">> {line}")
        self._write(line)
        try:
            reply = self._q.get(timeout=timeout)
        except queue.Empty:
            self._log(f"!! timeout after {timeout}s on: {line}")
            if self.auto_recover and _allow_recover:
                self.recover()
            raise M8Error(f"timeout after {timeout}s on {line!r} "
                          f"(recovered={self.auto_recover and _allow_recover})")
        self._log(f"<< ok={reply.get('ok')} code={reply.get('code')}")
        return reply

    # -- recovery -----------------------------------------------------------

    def recover(self) -> Dict[str, Any]:
        """Get back to a known state with no human help.

        A timed-out daemon is stuck inside a C++ primitive and is therefore not
        reading stdin, so it cannot be talked down -- it has to be killed. That
        also releases COM3, which is what lets the restart re-handshake.
        """
        self.restarts += 1
        self._log(f"recovering (restart #{self.restarts})")
        self._kill()
        time.sleep(0.5)
        self.start()
        # A kill between 'C' <mask> and 'C' 0x00 leaves a key held down, which
        # then auto-repeats at ~150ms forever. Release before anything else.
        try:
            self.send("PRESS", key=0, timeout=20, _allow_recover=False)
        except M8Error:
            pass
        return self.send("HOME", timeout=TIMEOUTS["HOME"], _allow_recover=False)

    # -- high-level API -----------------------------------------------------

    def state(self) -> Dict[str, Any]:
        return self.send("STATE").get("state", {})

    def screen(self) -> str:
        return str(self.state().get("screen", "") or "")

    def home(self, confirm: bool = False) -> Dict[str, Any]:
        return self.send("HOME", confirm=1 if confirm else 0)

    def press(self, key: str | int, hold: Optional[int] = None) -> Dict[str, Any]:
        return self.send("PRESS", key=self.parse_key(key), hold=hold)

    def goto(self, screen: str) -> Dict[str, Any]:
        return self._checked(self.send("GOTO", screen=screen), f"goto {screen}")

    def cursor(self, field: str) -> Dict[str, Any]:
        self._guard(field)
        return self._checked(self.send("CURSOR", field=field), f"cursor {field}")

    def cursor_grid(self, step: int, col: int) -> Dict[str, Any]:
        """Move the cursor on a grid screen, which has no field names at all.

        SONG / CHAIN / PHRASE / GROOVE / TABLE / INST_POOL are grid screens:
        getFieldMap().isGrid is true and every field lookup returns nullopt
        (ScreenModel.h:564/593/636), so `cursor <name>` cannot address them.
        """
        return self._checked(self.send("MOVEGRID", step=step, col=col),
                             f"cursor_grid {step},{col}")

    def read_field(self, field: str) -> Optional[str]:
        self._guard(field)
        r = self._checked(self.send("READ", field=field), f"read {field}")
        return r.get("state", {}).get("cursor_value")

    def set_field(self, field: str, value: Any) -> Dict[str, Any]:
        self._guard(field)
        self._require_gestures("SET")
        return self._checked(self.send("SET", field=field, value=value),
                             f"set {field}={value}")

    def note(self, name: str, vel: Optional[int] = None) -> Dict[str, Any]:
        self._require_gestures("NOTE")
        return self._checked(self.send("NOTE", name=name, vel=vel), f"note {name}")

    def keyjazz(self, note: int, vel: int = 0x7F) -> Dict[str, Any]:
        return self._checked(self.send("KEYJAZZ", note=note, vel=vel),
                             f"keyjazz {note}")

    def load(self, name: str) -> Dict[str, Any]:
        return self._checked(self.send("LOAD", path=name), f"load {name}")

    def fields(self, screen: Optional[str] = None) -> List[str]:
        return self.send("FIELDS", screen=screen).get("fields", [])

    def capture(self, path: str) -> Dict[str, Any]:
        return self._checked(self.send("CAPTURE", path=path), f"capture {path}")

    def script(self, path: str) -> Dict[str, Any]:
        return self._checked(self.send("SCRIPT", path=path), f"script {path}")

    # -- helpers ------------------------------------------------------------

    def _guard(self, field: str) -> None:
        key = field.strip().upper()
        if key in FENCED_FIELDS:
            raise FencedField(
                f"{key} is not drivable: {FENCED_FIELDS[key]}. "
                f"Refusing up front rather than thrashing; see "
                f"specs/M8_DRIVER_BUGS.md.")
        if key in HOME_FIRST_FIELDS:
            # Bug #21 only bites from a dirty cursor position, so establish a
            # known one first.
            self._log(f"{key}: homing first (bug #21 guard)")
            self.send("HOME", confirm=0, _allow_recover=False)

    def _require_gestures(self, what: str) -> None:
        """editValue/enterNote hard-refuse without a pinned gesture table.
        Say so here, with the fix, rather than surfacing the C++ error cold."""
        if self.gestures_ready is False:
            raise M8Error(
                f"{what} needs pinned edit gestures. {self.banner.get('gestures', '')}\n"
                f"Expected {GESTURE_FILE} in {REPO_ROOT} with populated=true. "
                f"Pin them with: m8_nav --port {self.port} --pin-gestures <FIELD> "
                f"--allow-mutation")

    def _checked(self, reply: Dict[str, Any], what: str) -> Dict[str, Any]:
        if not reply.get("ok", False):
            raise M8Error(f"{what} failed (code={reply.get('code')}): "
                          f"{reply.get('error', 'no error text')}")
        return reply

    @staticmethod
    def parse_key(key: str | int) -> int:
        """'SHIFT+RIGHT' -> 0x14. Accepts an int or '0x14' unchanged.

        Worth being explicit: RIGHT (0x04) moves the cursor inside a screen,
        SHIFT+RIGHT (0x14) moves between screens. Mixing them up produces
        plausible-but-wrong behaviour rather than an error.
        """
        if isinstance(key, int):
            return key & 0xFF
        s = str(key).strip()
        if not s:
            raise M8Error("empty key")
        try:
            return int(s, 0) & 0xFF
        except ValueError:
            pass
        mask = 0
        for part in s.replace("|", "+").split("+"):
            name = part.strip().upper()
            if name not in KEYS:
                raise M8Error(f"unknown key {part!r}; known: {'/'.join(KEYS)}")
            mask |= KEYS[name]
        return mask

    def _log(self, msg: str) -> None:
        if self.verbose:
            print(f"[m8drv] {msg}", file=sys.stderr)

    # -- diagnostics --------------------------------------------------------

    def _snapshot(self) -> Dict[str, Any]:
        st = self.state()
        return {
            "cursor": (st.get("cursor_field"), st.get("cursor_row")),
            "grid": (st.get("grid_step", -1), st.get("grid_col", -1)),
            "rows": {r.get("y"): r.get("text") for r in (st.get("rows") or [])},
            "settled": st.get("settled"),
        }

    ACCENT = [0, 252, 248]   # M8 default theme accent, == ScreenGrid::cursorColor

    def _capture(self) -> Dict[str, Any]:
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".json", prefix="m8drv_cap_")
        os.close(fd)
        try:
            self.send("CAPTURE", path=path)
            with open(path, encoding="utf-8") as f:
                return json.load(f)
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def inspect(self, key: Optional[str | int] = None) -> Dict[str, Any]:
        """Show where the accent colour actually is, as foreground AND background.

        `ScreenGrid::isCursor()` (M8Device.cpp:66) tests the **foreground** only:

            return c.fg[0]==cursorColor[0] && c.fg[1]==... && c.fg[2]==...

        So a cursor drawn as inverse video -- accent as BACKGROUND with a dark
        foreground, which is the usual tracker-grid cursor -- is invisible to it,
        and to everything built on it (`cursorRowY`, `cursorField`,
        `moveCursorToGrid`).

        This reports accent-as-fg and accent-as-bg cells separately, plus rect
        fills, and with `key` shows which of them move. Whichever set follows the
        key is the real cursor.
        """
        def read(cap: Dict[str, Any]) -> Dict[str, Any]:
            pal = cap.get("palette") or []
            try:
                idx = pal.index(self.ACCENT)
            except ValueError:
                idx = None
            cells = cap.get("cells") or []
            return {
                "accent_index": idx,
                "fg": sorted((c["row"], c["col"], c.get("ch"))
                             for c in cells if c.get("fg") == idx),
                "bg": sorted((c["row"], c["col"], c.get("ch"))
                             for c in cells if c.get("bg") == idx),
                "rects": sorted((r["col"], r["row"], r["w_px"], r["h_px"])
                                for r in (cap.get("rects") or [])),
            }

        cap_a = self._capture()
        a = read(cap_a)
        out: Dict[str, Any] = {
            "screen": cap_a.get("screen"),
            "settled": cap_a.get("settled"),
            "pitch": [cap_a.get("pitch_x"), cap_a.get("pitch_y")],
            "accent_index": a["accent_index"],
            "accent_fg_cells": a["fg"],
            "accent_bg_cells": a["bg"],
            "rects": a["rects"],
        }
        if a["accent_index"] is None:
            out["warning"] = (f"accent {self.ACCENT} is not in this screen's palette; "
                              f"the device theme may differ from cursorColor.")
        if key is None:
            return out

        self.press(key)
        b = read(self._capture())
        out["after_key"] = str(key)
        out["accent_fg_after"] = b["fg"]
        out["accent_bg_after"] = b["bg"]
        out["rects_after"] = b["rects"]
        fg_moved = set(a["fg"]) != set(b["fg"])
        bg_moved = set(a["bg"]) != set(b["bg"])
        rect_moved = set(a["rects"]) != set(b["rects"])
        out["fg_moved"], out["bg_moved"], out["rect_moved"] = fg_moved, bg_moved, rect_moved

        if bg_moved and not fg_moved:
            out["verdict"] = (
                "ACCENT BACKGROUND moved, foreground did not. The grid cursor is "
                "inverse video, and isCursor() tests fg only (M8Device.cpp:66) -- "
                "so it is invisible to cursorRowY/cursorField/moveCursorToGrid by "
                "construction. Fix: match accent as bg too.")
        elif fg_moved and bg_moved:
            out["verdict"] = ("both fg and bg accent moved -- compare the row sets "
                              "to see which one is the cursor and which is the "
                              "header track indicator.")
        elif fg_moved:
            out["verdict"] = ("accent foreground moved -- the normal case. The "
                              "cursor is accent-coloured text, which isCursor() "
                              "can see. Compare accent_fg vs accent_fg_after: the "
                              "header row's cell marks the column, and the data "
                              "row's marks the row.")
        elif rect_moved:
            out["verdict"] = "only a rect moved -- the cursor is a rect fill."
        else:
            out["verdict"] = ("nothing moved at all: the press is not landing on "
                              "this screen.")
        return out

    def probe(self, key: str | int, times: int = 3,
              hold: Optional[int] = None) -> Dict[str, Any]:
        """Press a key repeatedly and report exactly what moved.

        This separates two failures that look identical from the outside:

          rows change but cursor_row does not -> the press works and CURSOR
              DETECTION is wrong (it is reading a static cyan cell rather than
              the real cursor -- the M8_DRIVER_BUGS.md #5/#6 family).
          nothing changes at all               -> the PRESS is not landing
              (hold too short, or the key does nothing on this screen).

        Only trustworthy on a screen with no live element, so it reports the
        no-press drift first. Check `baseline_drift` is 0 before reading anything
        else here.
        """
        a, b = self._snapshot(), self._snapshot()
        drift = [y for y in a["rows"] if a["rows"][y] != b["rows"].get(y)]

        out: Dict[str, Any] = {
            "key": key, "hold_ms": hold if hold is not None else self.hold_ms,
            "screen": self.screen(), "baseline_drift": len(drift), "steps": [],
        }
        prev = self._snapshot()
        for i in range(times):
            self.press(key, hold=hold)
            cur = self._snapshot()
            changed = sorted(y for y in set(prev["rows"]) | set(cur["rows"])
                             if prev["rows"].get(y) != cur["rows"].get(y))
            out["steps"].append({
                "press": i + 1,
                "cursor": cur["cursor"],
                "grid": cur["grid"],          # (step, col) -- the addressable pair
                "grid_moved": cur["grid"] != prev["grid"],
                "cursor_moved": cur["cursor"] != prev["cursor"],
                "rows_changed": len(changed),
                "rows_changed_at": changed[:8],
                "settled": cur["settled"],
            })
            prev = cur

        # grid_moved has to count here. A horizontal press changes the column
        # without changing the row, and `cursor` is (field-text, row) -- so on a
        # grid screen `cursor_moved` is legitimately false for LEFT/RIGHT, and
        # judging on it alone reported "NOTHING CHANGED" for a press that had
        # visibly moved the cursor three columns.
        moved_grid = any(s["grid_moved"] for s in out["steps"])
        moved_cursor = any(s["cursor_moved"] for s in out["steps"])
        moved_rows = any(s["rows_changed"] for s in out["steps"])
        if moved_cursor and moved_grid:
            out["verdict"] = "press lands; both cursor and grid coordinates track"
        elif moved_grid:
            out["verdict"] = ("press lands and grid coordinates track. `cursor` did "
                              "not change, which is expected for a horizontal move: "
                              "it carries (field text, row), neither of which a "
                              "column change alters.")
        elif moved_cursor:
            out["verdict"] = ("press lands, but the GRID COORDINATES did not follow. "
                              "cursor_row moved while grid_step/grid_col stood "
                              "still, so the grid position read is stale -- suspect "
                              "a ghost accent cell pinning it (M8_DRIVER_BUGS #24).")
        elif moved_rows:
            out["verdict"] = ("PRESS LANDS but cursor tracking is broken on this "
                              "screen -- the screen changed and cursor_row did "
                              "not. Cursor detection is reading something static.")
        else:
            out["verdict"] = ("NOTHING CHANGED -- the press is not landing, or "
                              "this key does nothing on this screen. Retry with "
                              "a longer --hold.")
        return out

    # -- self-check ---------------------------------------------------------

    def doctor(self) -> Dict[str, Any]:
        """Prove the whole loop works, end to end, without changing anything.

        Uses a key that cannot mutate state: PRESS key=0 is a bare release.
        Then a real UP press, which only moves a cursor.
        """
        out: Dict[str, Any] = {"port": self.port, "exe": self.exe, "cwd": REPO_ROOT}
        out["firmware"] = self.firmware          # from the banner, not SemanticState
        out["gestures_ready"] = self.gestures_ready
        out["set_usable"] = self.gestures_ready is not False
        st = self.state()
        out["screen"] = st.get("screen")
        out["is_modal"] = st.get("is_modal")
        out["settled"] = st.get("settled")
        out["cursor_field"] = st.get("cursor_field")
        out["decoded_rows"] = len(st.get("rows") or [])
        out["decoded_state"] = bool(st)

        r = self.send("PRESS", key=0)
        out["release_ok"] = bool(r.get("ok"))

        # How much of the screen changes on its own, with no key pressed? Any
        # live element (playhead, meters, a running clock) means `settled` never
        # goes true and that comparing whole-screen snapshots is meaningless --
        # so measure it rather than be misled by it.
        a, b = self.state(), self.state()
        rows_a = {r.get("y"): r.get("text") for r in (a.get("rows") or [])}
        rows_b = {r.get("y"): r.get("text") for r in (b.get("rows") or [])}
        drifting = [y for y in rows_a if rows_a[y] != rows_b.get(y)]
        out["self_animating_rows"] = len(drifting)
        out["screen_is_static"] = not drifting

        # Prove keys reach the device, comparing ONLY the cursor position.
        # DOWN then UP nets to no movement, so this is state-neutral -- unlike a
        # bare UP, which cannot move at all when the cursor is already on the top
        # row and would read as a false "keys not arriving".
        def cur() -> tuple:
            st = self.state()
            return (st.get("cursor_field"), st.get("cursor_row"))

        before = cur()
        self.press("DOWN")
        moved = cur()
        self.press("UP")
        back = cur()
        out["cursor_before"], out["cursor_after_down"], out["cursor_back"] = \
            before, moved, back
        out["keys_reach_device"] = before != moved
        out["cursor_returned"] = back == before
        if before == moved:
            out["note"] = ("DOWN did not move the cursor: either this screen has "
                           "one row, or keys are not arriving.")
        elif back != before:
            out["note"] = (f"DOWN moved the cursor {before} -> {moved} but UP "
                           f"returned {back}, not {before}. Cursor is off by one; "
                           f"suspect a dropped press or key auto-repeat.")
        else:
            out["note"] = "keys confirmed round-trip; cursor restored"
        out["banner"] = self.banner
        out["unparsed_stdout_lines"] = len(self._noise)
        out["restarts"] = self.restarts
        return out


# --- CLI --------------------------------------------------------------------

def _print_screen(d: M8Driver) -> None:
    st = d.state()
    print(f"screen  : {st.get('screen')}")
    print(f"cursor  : {st.get('cursor_field')} = {st.get('cursor_value')!r}"
          f"  (row {st.get('cursor_row')})")
    if st.get("grid_step", -1) >= 0:
        print(f"grid    : step {st.get('grid_step')} col {st.get('grid_col')}"
              f" of {st.get('grid_columns')} columns")
    print(f"settled : {st.get('settled')}   modal: {st.get('is_modal')}"
          f"   live: {st.get('is_live_mode')}")
    for r in st.get("rows") or []:
        print(f"  y={r.get('y'):<4} {r.get('text','')}")


def main(argv: Optional[List[str]] = None) -> int:
    p = argparse.ArgumentParser(
        prog="m8drv", description="Unattended driver for a real M8 headless.")
    p.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default {DEFAULT_PORT})")
    p.add_argument("--exe", default=DEFAULT_EXE, help="path to m8_nav.exe")
    p.add_argument("--hold-ms", type=int, default=40,
                   help="button hold per press (m8_nav's own default is 40)")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--no-recover", action="store_true",
                   help="do not kill/restart/HOME on timeout (debugging only)")

    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("doctor", help="prove the loop works end to end")
    sub.add_parser("dump", help="print decoded screen state")
    sub.add_parser("state", help="print raw semantic state JSON")
    sub.add_parser("repl", help="interactive line-by-line daemon session")

    q = sub.add_parser("home", help="run the watchdog recovery routine")
    q.add_argument("--confirm", action="store_true",
                   help="allow pressing EDIT on a modal that will not cancel")

    for name, args in [("goto", ["screen"]), ("cursor", ["field"]),
                       ("read", ["field"]), ("load", ["name"]),
                       ("capture", ["path"]), ("script", ["path"])]:
        sp = sub.add_parser(name)
        for a in args:
            sp.add_argument(a)

    sp = sub.add_parser("set"); sp.add_argument("field"); sp.add_argument("value")
    sp = sub.add_parser("press"); sp.add_argument("key")
    sp.add_argument("--hold", type=int, default=None)
    sp = sub.add_parser("note"); sp.add_argument("name")
    sp.add_argument("--vel", type=int, default=None)
    sp = sub.add_parser("keyjazz"); sp.add_argument("note", type=int)
    sp.add_argument("--vel", type=int, default=0x7F)
    sp = sub.add_parser("fields"); sp.add_argument("screen", nargs="?")
    sp = sub.add_parser("inspect",
                        help="show accent cells (fg and bg) and rects; --key to see which move")
    sp.add_argument("--key", default=None)
    sp = sub.add_parser("cursor-grid", help="move the cursor on a grid screen")
    sp.add_argument("step", type=int); sp.add_argument("col", type=int)
    sp = sub.add_parser("probe", help="press a key N times and report what moved")
    sp.add_argument("key")
    sp.add_argument("--times", type=int, default=3)
    sp.add_argument("--hold", type=int, default=None)

    a = p.parse_args(argv)

    try:
        with M8Driver(port=a.port, exe=a.exe, hold_ms=a.hold_ms,
                      verbose=a.verbose, auto_recover=not a.no_recover) as d:
            if a.cmd == "doctor":
                print(json.dumps(d.doctor(), indent=2))
            elif a.cmd == "dump":
                _print_screen(d)
            elif a.cmd == "state":
                print(json.dumps(d.state(), indent=2))
            elif a.cmd == "home":
                d.home(confirm=a.confirm); _print_screen(d)
            elif a.cmd == "goto":
                d.goto(a.screen); _print_screen(d)
            elif a.cmd == "cursor":
                d.cursor(a.field); _print_screen(d)
            elif a.cmd == "read":
                print(d.read_field(a.field))
            elif a.cmd == "set":
                d.set_field(a.field, a.value); _print_screen(d)
            elif a.cmd == "press":
                d.press(a.key, hold=a.hold); _print_screen(d)
            elif a.cmd == "note":
                d.note(a.name, vel=a.vel)
            elif a.cmd == "keyjazz":
                d.keyjazz(a.note, vel=a.vel)
            elif a.cmd == "load":
                d.load(a.name); _print_screen(d)
            elif a.cmd == "capture":
                d.capture(a.path); print(f"wrote {a.path}")
            elif a.cmd == "script":
                d.script(a.path)
            elif a.cmd == "fields":
                for f in d.fields(a.screen):
                    print(f)
            elif a.cmd == "inspect":
                print(json.dumps(d.inspect(a.key), indent=2))
            elif a.cmd == "cursor-grid":
                d.cursor_grid(a.step, a.col); _print_screen(d)
            elif a.cmd == "probe":
                print(json.dumps(d.probe(a.key, times=a.times, hold=a.hold), indent=2))
            elif a.cmd == "repl":
                print("verbs: PRESS GOTO CURSOR READ SET NOTE KEYJAZZ HOME "
                      "LOAD SCRIPT CAPTURE STATE FIELDS QUIT")
                for line in sys.stdin:
                    line = line.strip()
                    if not line:
                        continue
                    if line.upper() in ("QUIT", "EXIT"):
                        break
                    verb, *rest = line.split()
                    kv = dict(x.split("=", 1) for x in rest if "=" in x)
                    try:
                        print(json.dumps(d.send(verb, **kv), indent=2))
                    except M8Error as e:
                        print(f"error: {e}", file=sys.stderr)
    except FencedField as e:
        print(f"refused: {e}", file=sys.stderr)
        return 3
    except M8Error as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
