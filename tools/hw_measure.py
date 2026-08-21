#!/usr/bin/env python3
"""Capture audio from the device with every dependent field verified by read-back.

Why this exists
---------------
A hardware measurement is only worth the state it was taken in, and on
2026-08-19 an AMP sweep produced a data point taken at the wrong LIM setting:
`set AMP FF` silently moved LIM from 04 to 08 (M8_DRIVER_BUGS.md #34). Nothing
in the run said so. The number looked exactly like a real measurement and had to
be thrown away only because a later read happened to notice.

The lesson is not "fix the driver" -- that is #34's guard, and it makes drift
loud rather than impossible. The lesson is that a measurement harness must
re-read every field its result depends on, immediately before AND after the
capture, and refuse the capture if either read disagrees.

Three guards, each bought with a wrong published number
-------------------------------------------------------
1. Fields are verified BEFORE and AFTER. The capture presses PLAY, and a press
   is exactly the thing suspected of moving the cursor in #34.
2. The transport is STOPPED before capturing. PLAY is a toggle. If the device is
   already playing, the capture tool's start press stops it and its stop press
   starts it again, so the window holds the silence between notes -- and every
   capture after it is inverted too. On 2026-08-20 that latched after three good
   captures and poisoned the remaining seventeen. It is also what produced
   hwtest_out/fit/PFF.wav, the empty window published as "AMP is -23 dB at
   LIM 08". is_playing is observable only on a GRID screen (M8_DRIVER_BUGS.md
   #28), so the check goes via SONG.
3. The capture must CONTAIN a note. A peak below --min-peak, or a window trimmed
   well short of the request, is refused and kept as `.NONOTE`. Verifying device
   state says nothing about whether any audio was recorded.

Usage
-----
    python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=00 --set LIM=04
    python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=FF --require TYPE=WAVSYNTH

`--set F=V` sets and then verifies. `--require F=V` only verifies -- for state
you did not set but are relying on, such as the loaded instrument type.

Exit codes: 0 captured and verified, 2 a field would not take or drifted (no
capture kept), 3 the capture tool itself failed, 4 the capture holds no note
(kept as `.NONOTE` so it cannot be read as data) -- retry that one.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CAPTURE = os.path.join(ROOT, "build", "Release", "m8_capture.exe")
sys.path.insert(0, os.path.join(ROOT, "tools", "m8drv"))
from m8drv import M8Driver, M8Error  # noqa: E402


def norm(v):
    """Compare case- and padding-insensitively; the device pads its labels."""
    return (v or "").strip().upper().replace(" ", "")


def matches(got, want):
    """The device shows enums as VALUE+TEXT, e.g. '04POST' for a wanted '04'."""
    g, w = norm(got), norm(want)
    return g == w or g.startswith(w)


def read_all(d, names):
    """Every field's value, through the connection already open.

    A field that cannot be read comes back None, never the neighbouring row's
    text: a failed READ still echoes the row it was last on, and reading that as
    a value turned "HyperSynth draws no AMP row" into an AMP of 'HYPERSYN' that
    matched a wanted '00' by prefix (2026-08-20).
    """
    out = {}
    for n in names:
        try:
            out[n] = d.read_field(n)
        except M8Error:
            out[n] = None
    return out


def stop_transport(d):
    """Leave the device stopped, parked at song 00/00, and back on INSTRUMENT.

    Returns (was_playing, now_stopped).

    Three separate things, all load-bearing:

    * The playhead is drawn only in a grid screen's row-label gutter, so
      `is_playing` has to be read from SONG -- on INSTRUMENT it is false
      whatever the transport is actually doing (M8_DRIVER_BUGS.md #28).
    * PLAY starts from the SONG cursor, so the cursor IS the play position.
      Leaving it wherever it happened to be means capturing whatever row it
      happened to be on; parking it at 00/00 makes the capture repeatable.
    * The screen is put back to INSTRUMENT before the port is released. Every
      capture that has ever worked was taken with the device on INSTRUMENT.
    """
    d.goto("SONG")
    st = d.state()
    playing = st.get("is_playing") if st.get("playhead_observable") else None
    if playing:
        d.press("PLAY")
        playing_after = d.state().get("is_playing", False)
    else:
        playing_after = False
    try:
        d.cursor_grid(0, 0)
    except M8Error as e:
        print("warning: could not park the song cursor at 00/00 -- %s" % e)
    d.goto("INSTRUMENT")
    if playing is None:
        return None, None
    return bool(playing), not playing_after


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--set", action="append", default=[], metavar="FIELD=VALUE")
    ap.add_argument("--require", action="append", default=[], metavar="FIELD=VALUE")
    ap.add_argument("--seconds", default="3")
    ap.add_argument("--keyjazz", default=None, help="MIDI note; omit to use PLAY")
    ap.add_argument("--keyjazz-vel", default="0x40")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--min-peak", type=float, default=0.05,
                    help="a capture peaking below this recorded no note; refuse it")
    a = ap.parse_args()

    def split(pairs):
        out = []
        for p in pairs:
            if "=" not in p:
                print("bad pair %r, want FIELD=VALUE" % p, file=sys.stderr)
                sys.exit(2)
            f, v = p.split("=", 1)
            out.append((f.strip(), v.strip()))
        return out

    to_set = split(a.set)
    to_req = split(a.require)
    names = [f for f, _ in to_set + to_req]

    # 1-2. Set, verify, and stop the transport -- all through ONE connection.
    #      The capture tool needs COM3 to itself, so the daemon is released
    #      before the capture and restarted afterwards for the drift check.
    try:
        with M8Driver(port=a.port) as d:
            d.goto("INSTRUMENT")
            for f, v in to_set:
                d.set_field(f, v)
            before = read_all(d, names)
            transport = stop_transport(d) if a.keyjazz is None else (False, True)
    except M8Error as e:
        print("REFUSED: the device would not take the setup -- %s" % e, file=sys.stderr)
        sys.exit(2)

    for f, v in to_set + to_req:
        if not matches(before.get(f), v):
            print("REFUSED: %s reads %r, wanted %r -- not capturing"
                  % (f, before.get(f), v), file=sys.stderr)
            sys.exit(2)
    was, stopped = transport
    if was is None:
        print("warning: could not tell whether the device is playing "
              "(no observable playhead) -- capturing anyway")
    if was:
        print("note: the device was PLAYING; stopped it so the capture's start "
              "press starts rather than stops")
    if was and not stopped:
        print("REFUSED: could not stop the transport -- every capture from here "
              "would record the gap between notes", file=sys.stderr)
        sys.exit(2)

    # 3. Capture.
    cmd = [CAPTURE, "--port", a.port, "--out", a.out, "--seconds", a.seconds]
    if a.keyjazz is not None:
        cmd += ["--keyjazz", a.keyjazz, "--keyjazz-vel", a.keyjazz_vel]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=180)
    sys.stdout.write("".join(l + "\n" for l in p.stdout.splitlines()
                             if "capture peak" in l or "wrote" in l))
    if p.returncode != 0:
        print("capture failed rc=%d" % p.returncode, file=sys.stderr)
        sys.exit(3)

    # 3b. Did we actually record the note?
    peak, dur = None, None
    for line in p.stdout.splitlines():
        if "capture peak:" in line:
            try:
                peak = float(line.split("capture peak:", 1)[1].split("(")[0])
            except ValueError:
                pass
        if ".wav" in line and line.rstrip().endswith(" s"):
            try:
                dur = float(line.rsplit(None, 2)[-2])
            except (ValueError, IndexError):
                pass
    want = float(a.seconds)
    bad = None
    if peak is not None and peak < a.min_peak:
        bad = ("peaked at %.5f, below --min-peak %.3f -- no note in the window"
               % (peak, a.min_peak))
    elif dur is not None and abs(dur - want) > 0.5:
        bad = ("ran %.2f s against a %.2f s request -- the window was trimmed"
               % (dur, want))
    if bad:
        empty = a.out + ".NONOTE"
        try:
            os.replace(a.out, empty)
        except OSError:
            empty = a.out
        print("REFUSED: the capture " + bad, file=sys.stderr)
        print("  renamed to %s so it cannot be mistaken for data" % empty,
              file=sys.stderr)
        sys.exit(4)

    # 4. Verify again, and leave the transport stopped for whoever runs next.
    try:
        with M8Driver(port=a.port) as d:
            if a.keyjazz is None:
                left, _ = stop_transport(d)
                if left:
                    print("note: the capture left the transport RUNNING -- its "
                          "stop press did not land; stopped it")
            d.goto("INSTRUMENT")
            after = read_all(d, names)
    except M8Error as e:
        print("REFUSED: could not re-read the device after capturing -- %s" % e,
              file=sys.stderr)
        sys.exit(2)

    drifted = ["%s: %r -> %r (wanted %r)" % (f, before.get(f), after.get(f), v)
               for f, v in to_set + to_req if not matches(after.get(f), v)]
    if drifted:
        bad = a.out + ".DRIFTED"
        try:
            os.replace(a.out, bad)
        except OSError:
            bad = a.out
        print("REFUSED: state drifted during capture -- " + "; ".join(drifted),
              file=sys.stderr)
        print("  the wav was renamed to %s so it cannot be mistaken for data" % bad,
              file=sys.stderr)
        sys.exit(2)

    print("verified: " + ", ".join("%s=%s" % (f, before.get(f))
                                   for f, _ in to_set + to_req))
    return 0


if __name__ == "__main__":
    sys.exit(main())
