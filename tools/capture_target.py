#!/usr/bin/env python3
"""
capture_target.py -- Deterministic screen navigation and audio capture tool for M8 tracker.

Navigates the M8 hierarchy closed-loop (Song -> Chain -> Phrase or Chain -> Phrase)
using m8drv (talking to m8_nav --serve), verifies each transition and cell value,
and invokes m8_capture.exe to capture the resulting audio.

MODES
-----
1. Song-relative:
   python tools/capture_target.py --song-row 0 --track 2 --phrase 12 --out cap.wav

2. Chain-direct:
   python tools/capture_target.py --chain 0E --phrase 12 --out cap.wav

3. Offline self-test:
   python tools/capture_target.py --self-test
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "m8drv"))

try:
    from m8drv import M8Driver, M8Error
except ImportError:
    M8Driver = None
    M8Error = Exception

DEFAULT_CAPTURE_EXE = os.path.join(REPO_ROOT, "build", "Release", "m8_capture.exe")


def parse_hex_or_int(val: str) -> int:
    """Parse string as hex or decimal int (e.g. '0E', '0x0E', '14')."""
    s = val.strip()
    if s.startswith(("0x", "0X")):
        return int(s, 16)
    try:
        return int(s, 16)
    except ValueError:
        return int(s, 10)


def fmt_hex(val: int, width: int = 2) -> str:
    """Format integer as zero-padded uppercase hex."""
    return f"{val:0{width}X}"


def get_cell_text_from_state(state: Dict[str, Any], step: int, col: int) -> Optional[str]:
    """
    Extract or infer cell text at (step, col) from semantic state rows or cursor.
    If the cursor is currently at (step, col), cursor_value holds the clean value.
    Otherwise, inspects state['rows'].
    """
    if state.get("grid_step") == step and state.get("grid_col") == col:
        cv = state.get("cursor_value")
        if cv:
            return cv.strip()

    # Fallback to inspecting text in rows:
    rows = state.get("rows", [])
    step_hex_2 = fmt_hex(step, 2)
    step_hex_1 = f"{step:X}"
    for r in rows:
        text = r.get("text", "").strip()
        tokens = text.split()
        if not tokens:
            continue
        first = tokens[0].upper()
        if first == step_hex_2 or first == step_hex_1:
            if len(tokens) > col + 1:
                return tokens[col + 1]
    return None


class NavigationError(Exception):
    """Raised when navigation or screen assertion fails."""
    pass


class TargetNavigator:
    """Encapsulates deterministic navigation and verification on the M8."""

    def __init__(self, driver: Any, verbose: bool = False):
        self.d = driver
        self.verbose = verbose

    def _log(self, msg: str) -> None:
        if self.verbose:
            print(f"[capture_target] {msg}", file=sys.stderr)

    def verify_screen(self, expected_screen: str, expected_header_prefix: Optional[str] = None) -> Dict[str, Any]:
        st = self.d.state()
        cur_screen = st.get("screen", "")
        if expected_screen not in cur_screen:
            raise NavigationError(
                f"Screen mismatch: expected {expected_screen}, got screen={cur_screen} "
                f"(header={st.get('screen')!r})"
            )
        if expected_header_prefix:
            prefix_upper = expected_header_prefix.upper().replace(" ", "")
            screen_clean = cur_screen.upper().replace(" ", "")
            if not screen_clean.startswith(prefix_upper):
                raise NavigationError(
                    f"Header mismatch: expected prefix {expected_header_prefix!r}, "
                    f"got {cur_screen!r}"
                )
        self._log(f"Verified screen {cur_screen} (matches {expected_screen})")
        return st

    def stop_transport_if_playing(self) -> bool:
        """Check if playing from SONG screen; stop if active."""
        self.d.goto("SONG")
        st = self.d.state()
        if st.get("playhead_observable") and st.get("is_playing"):
            self._log("Transport is playing; pressing PLAY to stop.")
            self.d.press("PLAY")
            time.sleep(0.1)
            st_after = self.d.state()
            if st_after.get("is_playing"):
                raise NavigationError("Failed to stop playing transport.")
            return True
        return False

    def navigate_song_to_chain(self, song_row: int, track: int) -> Tuple[int, str]:
        """
        Navigate to SONG -> (song_row, track), read chain hex, dive SHIFT+RIGHT.
        Returns (chain_id, chain_hex_str).
        """
        self._log(f"Navigating to SONG row {song_row} track {track}")
        self.d.goto("SONG")
        self.verify_screen("SONG")

        # Move cursor on grid
        self.d.cursor_grid(song_row, track)
        st = self.d.state()
        if st.get("grid_step") != song_row or st.get("grid_col") != track:
            raise NavigationError(
                f"Cursor failed to land on song step {song_row}, col {track}. "
                f"Landed on step {st.get('grid_step')}, col {st.get('grid_col')}"
            )

        # Read chain value under cursor
        cell_val = st.get("cursor_value", "").strip()
        if not cell_val:
            cell_val = get_cell_text_from_state(st, song_row, track) or ""

        self._log(f"Song cell ({song_row}, {track}) value: {cell_val!r}")
        if not cell_val or cell_val == "--":
            raise NavigationError(
                f"Song cell at row {fmt_hex(song_row)}, track {track} is empty ('--'). "
                f"No chain to dive into."
            )

        try:
            chain_id = int(cell_val, 16)
        except ValueError:
            raise NavigationError(
                f"Unrecognized chain hex value at song row {song_row}, track {track}: {cell_val!r}"
            )

        chain_hex = fmt_hex(chain_id)
        self._log(f"Diving SHIFT+RIGHT into CHAIN {chain_hex}")
        self.d.press("SHIFT+RIGHT")
        time.sleep(0.1)

        self.verify_screen("CHAIN", f"CHAIN {chain_hex}")
        return chain_id, chain_hex

    def navigate_direct_chain(self, chain_id: int) -> str:
        """
        Directly navigate to a specific chain screen.
        If already on CHAIN, checks header.
        Otherwise dives from SONG or sets scratch.
        """
        chain_hex = fmt_hex(chain_id)
        self._log(f"Navigating directly to CHAIN {chain_hex}")
        self.d.goto("CHAIN")
        st = self.verify_screen("CHAIN")
        cur_header = st.get("screen", "")
        if chain_hex in cur_header:
            self._log(f"Already on target CHAIN {chain_hex}")
            return chain_hex

        # If on a different chain, navigate via SONG
        self.d.goto("SONG")
        # Check if any visible song cell already references this chain
        found_pos = None
        st_song = self.d.state()
        for step in range(16):
            for col in range(8):
                val = get_cell_text_from_state(st_song, step, col)
                if val and val.upper() == chain_hex:
                    found_pos = (step, col)
                    break
            if found_pos:
                break

        if found_pos:
            self._log(f"Found existing chain {chain_hex} at SONG step {found_pos[0]}, col {found_pos[1]}")
            self.d.cursor_grid(found_pos[0], found_pos[1])
        else:
            self.d.cursor_grid(0, 0)
            st_cell = self.d.state()
            cur_cell = st_cell.get("cursor_value", "").strip()
            if cur_cell != chain_hex:
                self._log(f"Setting SONG (0,0) to {chain_hex} to dive")
                if hasattr(self.d, "set_field"):
                    self.d.set_field("00", chain_hex)
                else:
                    self.d.send("SET", field="00", value=chain_hex)

        self.d.press("SHIFT+RIGHT")
        time.sleep(0.1)
        self.verify_screen("CHAIN", f"CHAIN {chain_hex}")
        return chain_hex

    def find_phrase_in_chain(self, target_phrase: int) -> int:
        """
        Scan chain steps 0..15 for target_phrase.
        Returns the step index (0..15).
        """
        target_hex = fmt_hex(target_phrase)
        self._log(f"Scanning CHAIN steps for phrase {target_hex}")

        st = self.d.state()

        # First check visible rows in semantic state
        for step in range(16):
            val = get_cell_text_from_state(st, step, 0)
            if val and val.upper() == target_hex:
                self._log(f"Found phrase {target_hex} at chain step {step} from state scan")
                return step

        # If not found in static text, step through chain
        for step in range(16):
            self.d.cursor_grid(step, 0)
            st = self.d.state()
            cv = (st.get("cursor_value") or "").strip().upper()
            if cv == target_hex:
                self._log(f"Found phrase {target_hex} at chain step {step} via cursor")
                return step

        raise NavigationError(f"Phrase {target_hex} not found in current CHAIN.")

    def dive_chain_to_phrase(self, step: int, phrase_id: int) -> str:
        """
        From CHAIN at step, press SHIFT+RIGHT to dive into PHRASE.
        Verifies PHRASE header.
        """
        phrase_hex = fmt_hex(phrase_id)
        self._log(f"Moving to CHAIN step {step}, col 0 and diving to PHRASE {phrase_hex}")
        self.d.cursor_grid(step, 0)
        self.d.press("SHIFT+RIGHT")
        time.sleep(0.1)

        self.verify_screen("PHRASE", f"PHRASE {phrase_hex}")
        self._log(f"Successfully landed on PHRASE {phrase_hex}")
        return phrase_hex


def execute_capture(
    port: str,
    out_wav: str,
    seconds: float,
    audio_device: str = "M8",
    pre_roll_ms: float = 5.0,
    min_peak: float = 0.01,
    capture_exe: str = DEFAULT_CAPTURE_EXE,
) -> bool:
    """Invoke m8_capture.exe to record audio."""
    if not os.path.exists(capture_exe):
        raise FileNotFoundError(f"m8_capture executable not found: {capture_exe}")

    cmd = [
        capture_exe,
        "--port", port,
        "--audio", audio_device,
        "--seconds", str(seconds),
        "--pre-roll", str(pre_roll_ms),
        "--out", out_wav,
    ]
    print(f"[capture_target] Running capture: {' '.join(cmd)}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[capture_target] Capture failed (code {res.returncode}):", file=sys.stderr)
        print(res.stderr, file=sys.stderr)
        return False

    if not os.path.exists(out_wav) or os.path.getsize(out_wav) == 0:
        print(f"[capture_target] Capture produced no output file: {out_wav}", file=sys.stderr)
        return False

    print(f"[capture_target] Audio successfully captured to {out_wav}")
    return True


def run_target_capture(
    port: str = "COM3",
    song_row: Optional[int] = None,
    track: Optional[int] = None,
    chain_id: Optional[int] = None,
    phrase_id: Optional[int] = None,
    scope: str = "phrase",
    out_wav: Optional[str] = None,
    seconds: float = 3.0,
    audio_device: str = "M8",
    dry_run: bool = False,
    verbose: bool = False,
    driver_instance: Any = None,
) -> bool:
    """
    Main orchestration routine.
    If driver_instance is provided, uses it (useful for testing);
    otherwise creates an M8Driver session.
    """
    if M8Driver is None and driver_instance is None:
        raise RuntimeError("m8drv is not available and no mock driver was provided.")

    def run_with_driver(d: Any) -> bool:
        nav = TargetNavigator(d, verbose=verbose)

        # 1. Stop any pre-existing transport
        nav.stop_transport_if_playing()

        target_phrase_id = phrase_id

        # 2. Mode resolution
        if song_row is not None and track is not None:
            c_id, c_hex = nav.navigate_song_to_chain(song_row, track)
            if target_phrase_id is not None:
                step = nav.find_phrase_in_chain(target_phrase_id)
            else:
                step = 0
            if scope == "phrase":
                nav.dive_chain_to_phrase(step, target_phrase_id if target_phrase_id is not None else 0)
            elif scope == "chain":
                d.cursor_grid(step, 0)
            elif scope == "song":
                d.goto("SONG")
                d.cursor_grid(song_row, track)

        elif chain_id is not None:
            c_hex = nav.navigate_direct_chain(chain_id)
            if target_phrase_id is not None:
                step = nav.find_phrase_in_chain(target_phrase_id)
            else:
                step = 0
            if scope == "phrase":
                nav.dive_chain_to_phrase(step, target_phrase_id if target_phrase_id is not None else 0)
            elif scope == "chain":
                d.cursor_grid(step, 0)
            elif scope == "song":
                raise NavigationError("Scope 'song' requires --song-row and --track.")
        else:
            raise NavigationError("Must specify either (--song-row and --track) or --chain.")

        print(f"[capture_target] Target position verified. Screen is parked for scope '{scope}'.")
        return True

    # Run navigation phase
    if driver_instance:
        ok = run_with_driver(driver_instance)
    else:
        with M8Driver(port=port) as d:
            ok = run_with_driver(d)

    if not ok or dry_run:
        if dry_run:
            print("[capture_target] Dry-run complete. Skipping audio capture.")
        return ok

    if not out_wav:
        print("[capture_target] No output WAV specified (--out). Skipping capture.")
        return True

    # COM3 is now released by M8Driver context exit
    return execute_capture(
        port=port,
        out_wav=out_wav,
        seconds=seconds,
        audio_device=audio_device,
    )


# ---- Offline Self-Test Implementation ---------------------------------------

class MockM8Driver:
    """Mock M8Driver implementing the semantic state machine for self-testing."""

    def __init__(self):
        self._screen = "SONG"
        self._header = "SONG"
        self._grid_step = 0
        self._grid_col = 0
        self._is_playing = False
        self._rows: List[Dict[str, Any]] = []

        # Mock Song grid (row, col) -> chain_hex
        self.song_grid: Dict[Tuple[int, int], str] = {
            (0, 0): "00",
            (0, 2): "0E",
            (1, 1): "01",
        }
        # Mock Chains: chain_hex -> list of phrase_hex (16 steps)
        self.chains: Dict[str, List[str]] = {
            "0E": ["10", "11", "12", "--", "--", "--", "--", "--",
                   "--", "--", "--", "--", "--", "--", "--", "--"],
            "00": ["00", "01", "--", "--", "--", "--", "--", "--",
                   "--", "--", "--", "--", "--", "--", "--", "--"],
        }
        self.active_chain = "00"
        self.active_phrase = "00"

    def state(self) -> Dict[str, Any]:
        cursor_val = ""
        if self._screen == "SONG":
            cursor_val = self.song_grid.get((self._grid_step, self._grid_col), "--")
            screen_name = "SONG"
        elif self._screen == "CHAIN":
            chain_list = self.chains.get(self.active_chain, ["--"] * 16)
            cursor_val = chain_list[self._grid_step] if self._grid_step < 16 else "--"
            screen_name = f"CHAIN {self.active_chain}"
        elif self._screen == "PHRASE":
            cursor_val = "C-4"
            screen_name = f"PHRASE {self.active_phrase}"
        else:
            screen_name = self._screen

        rows = []
        if self._screen == "SONG":
            for step in range(16):
                line_cells = [fmt_hex(step, 2)]
                for col in range(8):
                    line_cells.append(self.song_grid.get((step, col), "--"))
                rows.append({"y": 30 + step * 10, "text": " ".join(line_cells)})
        elif self._screen == "CHAIN":
            chain_list = self.chains.get(self.active_chain, ["--"] * 16)
            for step in range(16):
                p_val = chain_list[step] if step < len(chain_list) else "--"
                rows.append({"y": 30 + step * 10, "text": f"{step:02X} {p_val} 00"})
        else:
            rows = [{"y": 30 + i * 10, "text": f"{i:02X} {cursor_val}"} for i in range(16)]

        return {
            "screen": screen_name,
            "header": screen_name,
            "grid_step": self._grid_step,
            "grid_col": self._grid_col,
            "cursor_value": cursor_val,
            "is_playing": self._is_playing,
            "playhead_observable": self._screen in ("SONG", "CHAIN", "PHRASE"),
            "rows": rows,
        }

    def goto(self, screen: str) -> Dict[str, Any]:
        self._screen = screen.upper()
        return {"ok": True}

    def set_field(self, field: str, value: str) -> Dict[str, Any]:
        if self._screen == "SONG":
            self.song_grid[(self._grid_step, self._grid_col)] = value.upper()
        return {"ok": True}

    def cursor_grid(self, step: int, col: int) -> Dict[str, Any]:
        self._grid_step = step
        self._grid_col = col
        return {"ok": True}

    def press(self, key: str) -> Dict[str, Any]:
        k = key.upper()
        if k == "SHIFT+RIGHT":
            if self._screen == "SONG":
                val = self.song_grid.get((self._grid_step, self._grid_col), "--")
                if val != "--":
                    self._screen = "CHAIN"
                    self.active_chain = val
                    self._grid_step = 0
                    self._grid_col = 0
            elif self._screen == "CHAIN":
                chain_list = self.chains.get(self.active_chain, ["--"] * 16)
                p_val = chain_list[self._grid_step]
                if p_val != "--":
                    self._screen = "PHRASE"
                    self.active_phrase = p_val
                    self._grid_step = 0
                    self._grid_col = 0
        elif k == "PLAY":
            self._is_playing = not self._is_playing
        return {"ok": True}

    def send(self, verb: str, **kwargs) -> Dict[str, Any]:
        if verb == "STATE":
            return {"ok": True, "state": self.state()}
        elif verb == "GOTO":
            return self.goto(kwargs.get("screen", "SONG"))
        elif verb == "MOVEGRID":
            return self.cursor_grid(int(kwargs.get("step", 0)), int(kwargs.get("col", 0)))
        elif verb == "PRESS":
            return self.press(kwargs.get("key", ""))
        return {"ok": True}


def run_self_test() -> int:
    """Run full suite of offline unit assertions against MockM8Driver."""
    print("=== Running capture_target self-tests ===")
    mock = MockM8Driver()

    # Test 1: Song Row 0, Track 2 -> Chain 0E -> Phrase 12 (Success)
    print("Test 1: Song (Row 0, Track 2) -> Chain 0E -> Phrase 12 ... ", end="")
    ok = run_target_capture(
        song_row=0,
        track=2,
        phrase_id=0x12,
        scope="phrase",
        dry_run=True,
        driver_instance=mock,
    )
    assert ok is True
    assert mock._screen == "PHRASE"
    assert mock.active_phrase == "12"
    print("PASS")

    # Test 2: Song Row 0, Track 1 (Empty cell '--' -> NavigationError)
    print("Test 2: Empty song cell error handling ... ", end="")
    try:
        run_target_capture(
            song_row=0,
            track=1,
            phrase_id=0x12,
            scope="phrase",
            dry_run=True,
            driver_instance=mock,
        )
        print("FAIL (expected NavigationError)")
        return 1
    except NavigationError as e:
        assert "is empty" in str(e)
        print("PASS")

    # Test 3: Chain 0E -> Missing Phrase 99 (NavigationError)
    print("Test 3: Missing phrase in chain error handling ... ", end="")
    try:
        run_target_capture(
            chain_id=0x0E,
            phrase_id=0x99,
            scope="phrase",
            dry_run=True,
            driver_instance=mock,
        )
        print("FAIL (expected NavigationError)")
        return 1
    except NavigationError as e:
        assert "not found in current CHAIN" in str(e)
        print("PASS")

    # Test 4: Direct Chain 0E -> Phrase 11 -> Scope Chain
    print("Test 4: Direct Chain 0E -> Phrase 11 (Scope: Chain) ... ", end="")
    mock.goto("SONG")
    ok = run_target_capture(
        chain_id=0x0E,
        phrase_id=0x11,
        scope="chain",
        dry_run=True,
        driver_instance=mock,
    )
    assert ok is True
    assert mock._screen == "CHAIN"
    assert mock._grid_step == 1  # Phrase 11 is at step 1 in Chain 0E
    print("PASS")

    # Test 5: Transport Stop on Entry
    print("Test 5: Transport stop if playing ... ", end="")
    mock._is_playing = True
    ok = run_target_capture(
        song_row=0,
        track=0,
        scope="song",
        dry_run=True,
        driver_instance=mock,
    )
    assert ok is True
    assert mock._is_playing is False
    print("PASS")

    print("\nAll 5 self-tests passed cleanly.")
    return 0


# ---- CLI Parser -------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Deterministic M8 screen navigation and audio capture tool."
    )
    parser.add_argument("--song-row", type=parse_hex_or_int, default=None,
                        help="Song grid row (hex or dec, 0-255)")
    parser.add_argument("--track", type=int, default=None,
                        help="Song grid track/column (0-7)")
    parser.add_argument("--chain", type=parse_hex_or_int, default=None,
                        help="Chain ID (hex or dec, 00-FF)")
    parser.add_argument("--phrase", type=parse_hex_or_int, default=None,
                        help="Phrase ID (hex or dec, 00-FF)")
    parser.add_argument("--scope", choices=["phrase", "chain", "song"], default="phrase",
                        help="Playback scope (default: phrase)")
    parser.add_argument("--out", dest="out_wav", default=None,
                        help="Destination WAV path for capture")
    parser.add_argument("--port", default="COM3", help="Serial port (default: COM3)")
    parser.add_argument("--audio", default="M8", help="Audio device filter (default: M8)")
    parser.add_argument("--seconds", type=float, default=3.0,
                        help="Recording duration in seconds (default: 3.0)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Navigate and verify only; do not capture audio")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print verbose diagnostics")
    parser.add_argument("--self-test", action="store_true",
                        help="Run offline self-test suite")

    args = parser.parse_args()

    if args.self_test:
        sys.exit(run_self_test())

    if args.song_row is None and args.chain is None:
        parser.error("Specify either (--song-row and --track) or --chain.")
    if args.song_row is not None and args.track is None:
        parser.error("--song-row requires --track.")

    try:
        success = run_target_capture(
            port=args.port,
            song_row=args.song_row,
            track=args.track,
            chain_id=args.chain,
            phrase_id=args.phrase,
            scope=args.scope,
            out_wav=args.out_wav,
            seconds=args.seconds,
            audio_device=args.audio,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        sys.exit(0 if success else 1)
    except NavigationError as e:
        print(f"[capture_target] REFUSED: {e}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"[capture_target] ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
