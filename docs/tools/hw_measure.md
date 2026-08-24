# hw_measure

**Source:** [`tools/hw_measure.py`](../../tools/hw_measure.py) (Python 3, stdlib only).
**Build target:** none. It drives [`m8drv`](m8drv.md) and [`m8_capture`](m8_capture.md),
both of which must already work.
**Category:** measurement harness. Not a driver and not a capture tool — the thin
layer that makes a capture *trustworthy*.

## What it does

Sets instrument fields, **verifies every one by reading it back**, captures audio,
then **verifies them again afterwards**. If any field will not take, it refuses to
capture. If any field drifted during the capture, it renames the WAV to
`<out>.DRIFTED` and exits non-zero.

> **Before-and-after is a workaround for not being able to look during**, and that is no longer
> the only option: [`m8_watchcapture`](m8_watchcapture.md) samples the guarded rows every 10 ms
> for the whole window. It detects the same drift this does, and additionally reports *when* it
> moved — which is the evidence `M8_DRIVER_BUGS.md` #34 says it is missing. This tool remains
> the one that sets the fields; the two are complementary, not replacements.

## Why it exists

A hardware measurement is worth exactly the state it was taken in.

On 2026-08-19, during an AMP curve sweep: `LIM` was stepped to `04` and read back
as `04POST`; `set AMP 00` read back as `00`; a capture was taken. Then `set AMP FF`
left `AMP` at `FD` **and moved `LIM` to `08 POST:W3`** — with no `LIM` command
issued anywhere ([#34](../../specs/M8_DRIVER_BUGS.md)). The resulting capture looked
like a perfectly good data point. It was only discarded because a later read
happened to notice, and the whole sweep had to be thrown out.

`editValue` now aborts when it detects that drift, but that makes the failure
*loud*, not impossible. This tool is the other half: never believe a field you set,
and never believe state you only checked beforehand.

**Verifying afterwards is the part that is easy to skip and shouldn't be.** The
capture itself presses PLAY, and a press is precisely what is suspected of moving
the cursor in #34 — so state confirmed before the capture proves nothing about the
audio that was just recorded.

## Usage

```powershell
python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=00 --set LIM=04
python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=FF --require TYPE=WAVSYNTH
python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=40 --keyjazz 60
```

| Flag | Meaning |
|---|---|
| `--out <path>` | Output WAV. Required. |
| `--set FIELD=VALUE` | Set the field, then verify it. Repeatable. |
| `--require FIELD=VALUE` | Verify only — for state you depend on but did not set (e.g. `TYPE=WAVSYNTH`, the loaded instrument). Repeatable. |
| `--seconds <n>` | Capture duration (default 3). |
| `--keyjazz <note>` | Capture via a live note instead of the PLAY toggle. |
| `--keyjazz-vel <n>` | Velocity for keyjazz (default `0x40`). |

| Exit | Meaning |
|---|---|
| 0 | Captured, and every field verified both before and after. |
| 2 | A field would not take, or drifted during the capture. **No usable WAV** — a drifted one is renamed `.DRIFTED`. |
| 3 | `m8_capture` itself failed. |

## Gotchas

- **Enum fields read back as value+text.** `LIM` set to `04` reads `04POST`, and
  `08` reads `08POST:W3`. The comparison is a prefix match on the alphanumerics, so
  `--set LIM=04` is satisfied by `04POST` but *not* by `08POST:W3`. Don't pass the
  text half.
- **A refusal is the point, not an error to work around.** A wrong number is worse
  than a missing one; that is the entire reason this exists. If a field will not
  take, fix that before capturing, don't retry blind.
- **It reads fields from the INSTRUMENT screen** (`GOTO screen=INSTRUMENT` before
  each read). Fields on other screens are not addressable this way yet.
- **It does not check `LIM` unless you ask it to.** `--require` costs one serial
  read; list every field the measurement's *interpretation* depends on, not just
  the one being swept. The #34 data point was lost precisely because `LIM` was set
  once and never re-checked.
- **`set` on some enum fields does not converge** — `set LIM value=04` was observed
  spinning past its timeout while stepping the field directly with the pinned
  gestures worked. Where that bites, step with `m8drv press` and use `--require`
  here to confirm.
