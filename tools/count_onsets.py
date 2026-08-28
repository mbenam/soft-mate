"""Count note attacks in a capture.

Written for the NTH and RND measurements: a percussive patch retriggered once per
phrase loop makes each loop a distinct blip, so counting blips counts triggers.

An onset is a rising crossing of `--thresh` after the envelope has spent at least
`--gap` seconds below it. The gap is what stops one decaying note being counted
several times as its tail wobbles across the threshold.

    python tools/count_onsets.py hwtest_out/nth01.wav
"""
import argparse
import struct
import sys
import wave


def envelope(path, win_ms=5.0):
    """Peak amplitude per short window, mono-summed, 0..1."""
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: expected 16-bit, got {w.getsampwidth() * 8}-bit")
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)

    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
    win = max(1, int(sr * win_ms / 1000.0))
    env, frames = [], len(samples) // ch
    for start in range(0, frames, win):
        peak = 0
        for f in range(start, min(start + win, frames)):
            for c in range(ch):
                v = abs(samples[f * ch + c])
                if v > peak:
                    peak = v
        env.append(peak / 32768.0)
    return env, sr, win


def onsets(env, sr, win, thresh, gap_s):
    """Rising crossings of `thresh`, each needing `gap_s` of quiet before it."""
    gap_wins = max(1, int(gap_s * sr / win))
    out, quiet = [], gap_wins          # start armed, so a note at t=0 counts
    for i, v in enumerate(env):
        if v < thresh:
            quiet += 1
        else:
            if quiet >= gap_wins:
                out.append(i * win / sr)
            quiet = 0
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--thresh", type=float, default=0.02)
    ap.add_argument("--gap", type=float, default=0.20,
                    help="seconds below threshold required before a new onset")
    a = ap.parse_args()

    for path in a.wav:
        env, sr, win = envelope(path)
        hits = onsets(env, sr, win, a.thresh, a.gap)
        dur = len(env) * win / sr
        deltas = [round(hits[i + 1] - hits[i], 3) for i in range(len(hits) - 1)]
        print(f"{path}: {len(hits)} onsets in {dur:.2f}s  peak_env={max(env):.4f}")
        print(f"  at: {[round(t, 3) for t in hits]}")
        if deltas:
            print(f"  gaps: {deltas}")


if __name__ == "__main__":
    main()
