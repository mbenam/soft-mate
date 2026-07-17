# Songs

## opening.m8s — "Night Drive"

The project's opening/demo song: 16 bars, C minor, 124 BPM, swing groove, building
dynamics. Sampler drums (kick/snare/hat/clap, in `samples/`) + MacroSynth melodics
(bass/pad/lead/arp — currently the placeholder saw; see `status.md`).

This is a real, reloadable `.m8s` — it round-trips to essentially the in-code demo
(`Engine::loadDemoSong`), within 16-bit sample quantization.

Play it:
```
build\Release\m8_render.exe --load songs/opening.m8s --sample-root songs --song --seconds 30 --out opening
```

Regenerate it (from the in-code demo — rewrites `opening.m8s` and `samples/*.wav`):
```
cmake --build build --config Release --target m8_makesong
build\Release\m8_makesong.exe
```

The generator (`src/tools/main_makesong.cpp`) exports engine state via
`m8::io::saveNewSong()`, which authors a full song — instrument types, envelopes,
names, sample paths — with no source file to overlay.
