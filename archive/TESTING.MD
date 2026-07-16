Good instinct to stop and validate before piling on features. You've got 92 automated tests, but they all run against the *engine* — the offline render path. Almost nothing has exercised the actual application: the UI, the file dialogs, the hardware round-trip. That's the gap.

Let me split this into what only you can do, what the agent can do, and the probe question specifically.

## Manual testing — things only a human at the app can check

The automated suite never opens a window. These are the gaps.

**UI navigation & editing**
- Every screen reachable via Shift+arrows; minimap tracks correctly
- Enter notes/instruments/FX in the phrase grid; verify hex edit (X+arrows) increments and wraps
- Chain and song editing; transpose values
- Groove editing; hear the swing change
- Undo/redo if it exists; if not, note it
- Cursor never navigates into a cell that crashes or does nothing

**The playhead** (you found the desync bug by eye once already)
- Playhead tracks in all three views during playback
- All 8 tracks stay locked to the same song row
- Playhead is correct after stop/restart, and after loading a song mid-playback

**File operations — the highest-risk area, least tested**
- Load each of the four example `.m8s` files
- Load a real song off an SD card
- Save a new song, close the app, reopen, reload it — is it identical?
- Save over an existing song
- Save with no filename (the text-input prompt)
- Try to save a pre-4.0 song — does it refuse cleanly, not crash?
- Load a song with missing samples — does the overlay list them, do the others still play?
- Point SAMPLE ROOT at the wrong folder, then the right one
- Load a song, edit it, save, reload — edits preserved?

**Audio in the actual app** (not offline render)
- Does it sound identical to the `m8_render` output of the same song? (If not, the live path and offline path diverge — important to know.)
- Latency acceptable? Xruns/dropouts under load?
- Change tempo live — does it respond smoothly?
- Leave it playing for 10 minutes — any drift, buildup, crash?

**Stress / edge**
- Rapidly mash keys, hold arrows with key-repeat during playback
- Load song → play → load a different song while playing
- Fill a phrase completely, every track, every FX slot
- Resize the window (if resizable)
- Unplug/replug audio device mid-playback

## Agent-based testing — what to automate next

The agent can extend coverage without a human, but it needs the right targets:

**Broaden the fuzz.** You fuzz the sequencer walker. Add fuzzing of the *command stream*: random valid `EngineCommand`s pushed during render, asserting no crash, no NaN, no allocation. This exercises the whole ring/apply path, not just the walker.

**Round-trip every example file through the engine.** For each `.m8s` in the repo: load → render 30s → `m8_analyze` → assert hard checks pass. Automated, and it catches regressions in the loader as you add features. Make it a `[song]` test tag.

**Property tests on the conversion layer.** Generate random valid songs with `m8_makeprobe`-style code, round-trip through POD and back, assert byte-identical. You have L4 for two files; make it a property over hundreds of generated songs.

**`m8_analyze` in CI on the demo render.** Every build renders the demo and runs the hard checks. DC, crest, clipping regressions become build failures.

**A "soak" render.** Render 10 minutes of the demo offline, assert no drift, no NaN, no growing DC. Catches slow accumulation bugs the 30–40s renders miss.

**Snapshot the event stream.** Render the demo, dump the `EngineEvent` CSV, commit it as a golden file. Future renders diff against it — any change in note timing or count is flagged. This would have caught the NOTE_OFF regression instantly.

## Testing the probe specifically

This is the one that's genuinely unproven, and it's worth being careful because the whole Macrosyn parity plan rests on it. Three levels, cheapest first:

**Level 1 — does the probe round-trip in software?** Already claimed passing: `m8_makeprobe` writes a file, reads it back, params match. Good, but that only proves your writer and reader agree — not that the file is *correct*.

**Level 2 — does the probe load on real hardware?** This is the real test, and it's the one thing that catches a malformed header. Generate a probe, copy it to the SD card, load it on the headless. If it loads and plays a note, your `.m8s` is genuinely valid — not just self-consistent. **Do this before trusting `m8_capture` for anything.** The `M8VERSION` header fix the agent mentioned is exactly the kind of thing that passes software round-trip but gets rejected by the device.

**Level 3 — does capture produce a usable WAV?** With a probe loaded on the device:
- Run `m8_capture`, get a WAV
- `m8_analyze` it — is there a clean sustained tone, onset near sample 0, ≥1.5s sustain, no clipping?
- Eyeball it: does the trim start at the note, or clip the attack / include silence?
- Play it back — does it sound like what the headless is producing?

**Level 4 — the loop closes.** Render the *same probe* through `m8_render --load --note`, and compare durations/pitch between the capture and the render. They should be the same note at the same pitch. If the pitch differs, either your engine's tuning or the probe's note encoding is wrong — better to find that now than mid-Macrosyn.

The key insight on the probe: **software round-trip (Level 1) proves nothing about hardware validity.** The only test that matters is Level 2 — put it on the device. Do that one thing before the new chat, because if probes don't load on hardware, the entire Macrosyn parity plan needs rethinking, and you want to know that now.

Want me to write these up as a `TESTING.md` for the repo, and a prompt for the agent-automatable ones?