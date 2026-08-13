# Mixer — Rebuild Spec

> **COMPLETE / ARCHIVED 2026-08-12.** Steps 1-8 implemented and verified: build clean,
> full suite green (238 cases, 893,012 assertions). §UI-4a, §UI-4c and §UI-4e closed in
> `docs/tools/hw_findings.md`. Deferred work (main EQ + its editor, the Limiter/Mix Scope
> view and with it the DJF type control, the top-of-screen scope, the playing-note list)
> is recorded in §2 and §8 here and summarised in `status.md`; it is not lost, just not
> scheduled.

Rebuild the MIXER screen and the master bus behind it. Decided 2026-08-12 from the M8
manual's mixer section, a photo of the device's mixer screen, and the `.m8s` file format.

**This is not a parity exercise.** `docs/ui_screen_spec.md` parks MIXER parity (`hw_findings.md`
§UI-4) and that stays parked — we are not chasing the device's geometry, colours, or cell
counts. We are building a mixer you can actually mix a song with. Where the device's *content*
tells us what a control means, we follow it; where its *layout* differs, we don't care.

---

## 1. What we are building

The mixer shows and adjusts:

- the 8 track volumes, each with a live stereo level meter
- the 3 send-effect returns (MX = ModFX/chorus, DE = delay, RE = reverb), with meters
- the master strip: MIX, LIM, DJF, OTT, with a tall master meter. **EQ is drawn as a label
  only, not a cursor stop** (changed during step 4): the EQ and its editor view aren't built,
  and a control that does nothing when you press it is worse than no control. It goes back in
  the cursor set when there is an EQ behind it.
- SPEAKER VOL — the application's own output level

## 2. What we are deliberately not building

| Not building | Why |
|---|---|
| INPUT (analog) and USB rows | soft-mate is not a device. No analog input; "what the USB host is sending us" has no meaning for a desktop app. |
| Dual-mono input cell (`--`) | Goes with the inputs. |
| EQ Editor view | The EQ itself doesn't exist yet; a screen that edits nothing is a lie. |
| Limiter / Mix Scope view | Needs the same live-audio feed as the meters. Cheap to add *after* that exists, expensive alongside it. |
| The scope (waveform) across the top | Out this round. Same feed as the meters; revisit after. |
| Per-track playing-note list (right side) | Out this round. |

**Accepted consequence of skipping the Scope view:** the DJ filter's *type* (LP/HP, LP/BS,
BS/HP) is only reachable there on hardware, so it gets no UI. Songs loaded from a real M8 keep
whatever type they were saved with; anything we create uses the default. The filter still works
— you just can't switch its flavour. Do not invent a temporary home for it on the mixer screen;
the device doesn't have one and we'd have to remove it later.

---

## 3. Corrections to the data model

Four things in `MixerState` are wrong. They are wrong together, because they came from guessing
at a screen we couldn't read. The manual plus the device photo plus the file format settle all
four.

The file format (`third_party/m8-files-cxx/src/types.hpp`) has exactly one master gain:

```cpp
struct MixerSettings {
    uint8_t master_volume, master_limit;
    std::array<uint8_t, 8> track_volume;
    uint8_t chorus_volume, delay_volume, reverb_volume;
    AnalogInputSettings analog_input;
    InputMixerSettings usb_input;
    uint8_t dj_filter, dj_peak, dj_filter_type;
};
```

The device screen shows two volume-ish controls (SPEAKER VOL at the top, MIX in the master
column) and a master column reading **MIX / LIM / DJF / OTT** — no RES row, no TYP row.

| File field | Screen | `MixerState` today | Should be |
|---|---|---|---|
| `master_volume` | MIX | `out_vol` | `mix_vol` |
| `master_limit` | LIM | `lim_val` | unchanged |
| `dj_filter` | DJF | `djf_freq` | unchanged |
| `dj_peak` | **OTT** | `djf_res` | `ott` |
| `dj_filter_type` | *(Scope view)* | `djf_typ` | unchanged, but not a row on this screen |
| *(none)* | SPEAKER VOL | shown as "OUTPUT VOL" | `out_vol`, app-level, never persisted |

Consequences worth stating outright:

- **`djf_res` is OTT.** Over The Top is a parallel multiband compressor, not a filter
  resonance. This closes `hw_findings.md` §UI-4a, which had been waiting on device FX
  enumeration.
- **We load the song's master volume into the wrong field.** `SongIO` does
  `state.mixer.out_vol = song.mixer_settings.master_volume`, and we draw `out_vol` at the top as
  "OUTPUT VOL". The song's master volume is MIX. This is why §UI-4c found `mix_vol` "never
  loaded, always the compile-time default" — the value was arriving, just on the wrong control.
- **SPEAKER VOL is not in the song file** and must not be written to one. It resets to `FF`
  (full) on launch *and* on song load, because `LOAD_SONG` replaces the whole `MixerState`.
  That is accepted for now; if it becomes annoying, the fix is to exclude it from the
  `LOAD_SONG` copy, not to persist it.

### Inputs: keep the data, drop the UI

`in_*` and `usb_*` stay in `MixerState` and keep loading from the file. **Stop writing them in
`convertEngineToSong`.** Save-by-overlay then leaves the original bytes untouched, which also
fixes §UI-4e — today we rebuild `analog_input` from engine fields that have no right channel, so
saving a hardware song silently discards its right-channel input bytes. Not writing them is
strictly better than writing them badly.

---

## 4. Signal chain

The manual is explicit: everything sums, then **OTT → main EQ → limiter → DJ filter → main song
volume**.

Today `Engine::render` does:

```
sum(tracks · dry · pan) + chorus/delay/reverb returns
  → out_vol → DC block → tanh → clamp
```

Target:

```
sum(tracks · dry · pan) + chorus/delay/reverb returns
  → OTT  → [EQ, deferred] → LIM → DJF → MIX → SPEAKER VOL
  → DC block → clamp
```

Notes:

- **Track volume already scales that track's sends** (`vSamp *= tVol` runs before the sends are
  taken), which is what the manual describes. Leave it alone.
- **Keep the `tanh` until the limiter lands.** It is the only thing preventing a hot mix from
  clipping. Once LIM exists, removing it is a real audio change and needs a before/after render
  comparison, not a guess.
- **DJF:** `80` is off. Below `80` engages the low-pass (or band-stop, per type); above `80` the
  high-pass. We have `ZdfFilter.h` already.
- **OTT:** from `00` to `80` it mixes in; from `80` up, the main mix volume fades out. It is a
  *parallel* effect — the dry mix continues past it.
- **LIM:** engaged only when its value is above `00`.

---

## 5. Meters

### 5.1 Drawn as font characters, not rectangles

Bars become characters: seven partial-fill glyphs plus a blank, giving **8 levels per cell**.
Stack cells for taller bars — an 8-cell meter has 64 steps.

- Reserve characters **0x01–0x07** for fill levels 1/7 … 7/7. A blank cell is a space.
- Append them to `font[]` in `src/ui/font.h` (`struct Font { char letter; char code[7][6]; }`).
  `Renderer::drawChar` finds glyphs by linear search and *silently draws nothing* for an unknown
  character — including skipping the shadow-grid stamp — so a missing glyph fails invisibly.
- **`dumpScreenText` must translate 0x01–0x07 to `1`–`7`.** It writes raw `ch` bytes today,
  which would emit control codes into every dump. With the translation, a meter reads as a
  column of digits in headless dumps and can be asserted like any other text.
- Colour is per cell and already recorded in the shadow grid, so the teal → yellow → red
  gradient is just choosing a colour as the stack is drawn. The top cell goes red on clip.

This replaces `DrawVerticalBar`'s `fillRectPixel` calls for **both** the live meters and the
volume bars. Two things then get deleted: the "background covers only the unfilled portion,
adjacent not overlapping" arithmetic that exists purely to dodge the overlap checker, and the
`assert_no_overlap` exception for MIXER in `nav_all_screens.m8script`.

Accepted: glyphs are 5×7 in an 8×8 cell, so stacked cells show a 1px seam and neighbouring bars
a 3px gap. The device's meters band the same way. Making bars solid would mean widening the
`Font` struct to 8×8 and padding all 89 existing glyphs — only if the seams actually look wrong
on screen.

### 5.2 Getting levels out of the engine

The UI must never read engine state (`AGENTS.md` §6), so levels travel the same way playheads
do: **packed atomics, published by the audio thread, read with acquire by the UI.**

- Per track: post-pan peak L and peak R, as bytes, packed into one `std::atomic<uint32_t>`
  alongside a clip flag. Eight of them, mirroring `m_playheadState[8]`.
- One more for the master bus.
- Accumulate the per-track peak inside `Engine::render`'s existing per-frame loop — the post-pan
  values `vSamp * dry * panL/panR` are already computed there — and publish **once per `render`
  call**, not per frame.
- Peak decay/ballistics live on the engine side so the UI stays a dumb reader.
- No allocation, no locks, nothing new on the audio thread beyond a few float maxes.

---

## 6. Screen layout

Exact cells live in `MixerScreenLayout.h`; this is the structure:

```
MIXER                                    T>128
SPEAKER VOL  FF

  [8 stereo track meters, 2 cells each]        [tall master meter]
  E0 E0 E0 E0 DB DB E0 E0                            EQ

  [3 send meters]                          MIX  E0
  E0 E0 B0                                 LIM  50
  MX DE RE                                 DJF  80
                                           OTT  80
```

The space freed by dropping INPUT and USB is left empty rather than filled with something
invented. Navigation is the existing per-screen `enum class CursorId` + `NavNode` map; the
cursor set becomes: SPEAKER_VOL, TRK_VOL_0..7, MST_CHO/MST_DEL/MST_REV, EQ, MIX_VOL, LIM_VAL,
DJF, OTT. `IN_*`, `USB_*`, `DJF_RES`, `DJF_TYP` come out.

---

## 7. Steps

1. [x] **Model + persistence.** Rename `djf_res` → `ott`; move `master_volume` to `mix_vol`;
       make `out_vol` the unsaved SPEAKER VOL; stop writing `analog_input`/`usb_input` on save.
       Update `ParamID` and `EngineStateUpdater` to match.
2. [x] **Font glyphs.** Seven fill levels at 0x01–0x07, plus the `dumpScreenText` translation.
3. [x] **Bar primitive.** One function that draws a value as a stack of glyphs with a colour
       ramp, used by both meters and volume bars. Delete `DrawVerticalBar`.
4. [x] **Screen rebuild.** New layout, new cursor set, new nav map. Static values only.
5. [x] **Live levels.** Per-track and master peak atomics; meters animate.
6. [x] **DJ filter.** `80` = off, sweeps both ways, default type only.
7. [x] **Limiter.** Engaged above `00`; re-evaluate the master `tanh` with a render comparison.
8. [x] **OTT.** Parallel multiband, mixing in to `80`, fading the dry mix above it.
9. [x] **Fold findings into reference docs, then archive this spec.** Close §UI-4a and §UI-4c in
       `hw_findings.md`, update `status.md`'s mixer entries, and record the meter rule change in
       `docs/ui_screen_spec.md`. Cannot be ticked while any step above is unticked
       (`AGENTS.md` §9).

Deferred, not scheduled: main EQ and its editor view, the Scope view (and with it the DJF type
control), the top-of-screen scope, the playing-note list, input/USB monitoring.

---

## 8. Known limitations to carry forward

- SPEAKER VOL resets to full on every launch and on every song load.
- The DJ filter's type cannot be changed from the UI; only the default LP/HP mode is modeled.
- Meters are quantised to 8 levels per cell; a slowly-moving level will step visibly.
- The voice path is mono, so a track's stereo meter differs L/R only by panning.
- **The send-return bars show their setting, not a live level.** Per-send metering would mean
  metering the effect returns separately, which the mix loop doesn't currently split out.
- **None of the three DSP curves is hardware-verified**, in the same sense as the FM and
  WavSynth engines (`status.md` Placeholders). No capture exists for any of them:
  - *Limiter* — feed-forward peak limiter, one shared gain across both channels, instant
    attack and slow release. The threshold falls from unity to half scale across the value
    range, chosen deliberately gentle so the default `0x40` doesn't squash existing songs.
    The real knee, ratio and timing are unknown.
  - *DJ filter* — one ZDF SVF per channel, exponential sweep over 30 Hz–18 kHz either side of
    `0x80`, fixed resonance. The real sweep range and resonance behaviour are unknown.
  - *OTT* — three bands split by one-pole crossovers at ~200 Hz and ~2 kHz, each pushed toward
    a fixed target level, mixed per the manual's `00`→`80`→`FF` rule. The real per-band
    ratios and times are unknown. This is the loosest approximation of the three.
- **The master `tanh` is still in place** after the limiter. The spec's step 7 said to
  re-evaluate it once LIM existed; it stays because LIM is off at value `00` and nothing should
  be able to emit a hard-clipped buffer just because the user turned the limiter off. Removing
  it is an audio change wanting a before/after render comparison.
- **LIM and OTT default to off**, so the master bus is transparent unless a song's own stored
  values engage it. This was not the first attempt: OTT initially inherited `0x80` from
  `djf_res`, the field it replaced. For a filter resonance `0x80` means "centred"; for OTT the
  manual says `00`..`80` *mixes it in*, so `0x80` is fully wet — every song silently got a
  full-strength multiband compressor. The suite caught it immediately (`[audio]` crest fell
  10.8 → 8.5 dB, and a `[tables]` volume comparison inverted because the compressor pulled
  loud and quiet renders to the same level). Do not "restore" these defaults to something
  non-zero without a hardware measurement to justify it.
- DJF is the exception to that rule: `0x80` genuinely is its off position, because it is
  bipolar — below sweeps the low-pass down, above sweeps the high-pass up.
