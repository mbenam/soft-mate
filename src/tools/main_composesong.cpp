// -----------------------------------------------------------------------------
// m8_composesong -- authors the app's startup songs and writes them to songs/
// (data, not hard-coded into the app).
//
// Two songs live here, selected with --song:
//
//   neondusk  (default)  NEON DUSK, 112 BPM, Dm - Gm - Bb - Am. The startup
//                        song. Uses every synth engine: samplers, WavSynth,
//                        FMSynth, HyperSynth and MacroSynth.
//   sunrise              SUNRISE, 128 BPM, Am - F - C - G. The previous startup
//                        song, kept reproducible because tests load
//                        songs/sunrise.m8s by name (test_bundle_export,
//                        test_render_screen, several tests/ui scripts).
//
// Both are exported via io::saveNewSong(); the app loads the .m8s at startup, so
// nothing about either song lives in the app binary.
//
// Regenerate:  cmake --build build --config Release --target m8_composesong
//              build\Release\m8_composesong.exe                  (NEON DUSK)
//              build\Release\m8_composesong.exe --song sunrise   (SUNRISE)
// Play:        build\Release\m8_render.exe --load songs/neondusk.m8s \
//                  --sample-root songs --song --seconds 40 --out neondusk
// -----------------------------------------------------------------------------
#include "io/SongIO.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <filesystem>

using namespace m8;
using engine::InstType;

// SUNRISE -- the original startup song, unchanged. Reuses the demo's tuned
// instrument patches (the saw and the drum kit) but every note, chain, tempo and
// key is its own.
static void composeSunrise(engine::Sequencer& seq, engine::EngineState& st) {
    // --- Instruments: keep the demo patches, adjust for SUNRISE --------------
    // Demo layout: 0 KICK,1 SNARE,2 HAT,3 BASS,4 PAD,5 ARP,6 LEAD,7 CLAP.
    // Drums (samplers) must NOT follow the chord transpose -> TRANSP OFF.
    for (int i : {0, 1, 2, 7}) st.instruments[i].sampler.transp = 0;
    // Melodic (macrosyn) follow the progression -> TRANSP ON (already the default).
    for (int i : {3, 4, 5, 6}) st.instruments[i].macrosyn.transp = 1;

    // Repoint sampler paths at the committed WAVs (M8-absolute; resolved under
    // --sample-root at load).
    struct Remap { const char* from; const char* to; };
    const Remap remap[] = {
        {"demo_kick.wav",  "/samples/kick.wav"},
        {"demo_snare.wav", "/samples/snare.wav"},
        {"demo_hat.wav",   "/samples/hat.wav"},
        {"demo_clap.wav",  "/samples/clap.wav"},
    };
    for (auto& inst : st.instruments) {
        if (inst.type != InstType::INST_SAMPLER) continue;
        for (const auto& rm : remap)
            if (std::strcmp(inst.sampler.samplePath, rm.from) == 0) {
                std::strncpy(inst.sampler.samplePath, rm.to, sizeof(inst.sampler.samplePath) - 1);
                inst.sampler.samplePath[sizeof(inst.sampler.samplePath) - 1] = '\0';
            }
    }

    // --- Arrangement ---------------------------------------------------------
    seq.clear();  // all phrases/chains/song empty

    auto S = [&](int p, int r, int note, int vol, int instr) {
        seq.phrases[p][r] = engine::Step{ (uint8_t)note, (uint8_t)vol, (uint8_t)instr, {} };
    };

    // Instrument indices used per lane.
    enum { I_KICK = 0, I_SNARE = 1, I_HAT = 2, I_BASS = 3, I_PAD = 4, I_ARP = 5, I_LEAD = 6, I_CLAP = 7 };
    // Phrase ids.
    enum { P_KICK = 0, P_CLAP = 1, P_HAT = 2, P_BASS = 3, P_PAD = 4, P_ARP = 5, P_LEAD1 = 6, P_LEAD2 = 7, P_SNARE = 8 };
    // Notes (C-4 = 60). A-minor material; the progression transpose does the rest.
    const int A2 = 45, A3 = 57, E4 = 64, G4 = 67, A4 = 69, C5 = 72, D5 = 74, E5 = 76;

    // Drums (16 steps = 1 bar, 4 steps/beat).
    for (int r : {0, 4, 8, 12})  S(P_KICK,  r, 60, 0x70, I_KICK);   // four-on-the-floor
    for (int r : {4, 12})        S(P_CLAP,  r, 60, 0x60, I_CLAP);   // backbeat 2 & 4
    for (int r : {4, 12})        S(P_SNARE, r, 60, 0x4C, I_SNARE);  // snare layered under the clap
    for (int r : {2, 6, 10, 14}) S(P_HAT,   r, 60, 0x48, I_HAT);    // offbeat hats
    for (int r : {0, 4, 8, 12})  S(P_HAT,   r, 60, 0x22, I_HAT);    // quiet on-beat drive

    // Bass: syncopated, octave-hopping root (A). vol pulled a touch under kick.
    S(P_BASS, 0, A2, 0x62, I_BASS);  S(P_BASS, 3, A2, 0x5A, I_BASS);
    S(P_BASS, 6, A3, 0x56, I_BASS);  S(P_BASS, 8, A2, 0x62, I_BASS);
    S(P_BASS, 11, A2, 0x5A, I_BASS); S(P_BASS, 14, A3, 0x56, I_BASS);

    // Pad: one sustained root per bar (the long amp env holds it).
    S(P_PAD, 0, A3, 0x46, I_PAD);

    // Arp: root / fifth / octave, eighth notes — quality-neutral so it stays
    // consonant under every chord in the transposed progression.
    { const int arp[8] = {A3, E4, A4, E4, A3, E4, A4, E4};
      for (int k = 0; k < 8; ++k) S(P_ARP, k * 2, arp[k], 0x40, I_ARP); }

    // Lead: A-minor-pentatonic melody (fixed, no transpose) — consonant over the
    // whole progression. Two phrases for variety.
    S(P_LEAD1, 0, A4, 0x52, I_LEAD); S(P_LEAD1, 4, C5, 0x52, I_LEAD);
    S(P_LEAD1, 7, E5, 0x4E, I_LEAD); S(P_LEAD1, 10, D5, 0x4E, I_LEAD);
    S(P_LEAD2, 0, E5, 0x52, I_LEAD); S(P_LEAD2, 3, D5, 0x4E, I_LEAD);
    S(P_LEAD2, 6, C5, 0x4E, I_LEAD); S(P_LEAD2, 8, A4, 0x52, I_LEAD);
    S(P_LEAD2, 12, G4, 0x4E, I_LEAD);

    // Chains: one per LANE (= chain index), NOT per instrument. The build is done
    // at the SONG level (resting a lane = an empty song cell), so every chain is a
    // simple 4-bar cycle. The Am-F-C-G progression is chain transpose on the
    // melodic lanes; drums (TRANSP off) ignore it.
    enum { L_KICK = 0, L_CLAP = 1, L_HAT = 2, L_BASS = 3, L_PAD = 4, L_ARP = 5, L_LEAD = 6, L_SNARE = 7 };
    const int prog[4] = {0, -4, +3, -2};   // A -> F -> C -> G, in semitones
    auto CH = [&](int lane, int slot, int phrase, int tsp) {
        seq.chains[lane][slot] = engine::ChainStep{ (uint8_t)phrase, (int8_t)tsp };
    };
    // Each chain is ONE 4-bar Am-F-C-G cycle (slots 0-3); slot 4 is left empty so
    // the chain ends after 4 bars and the song row advances. The melodic lanes
    // carry the progression as chain transpose; drums stay at 0.
    for (int b = 0; b < 4; ++b) {
        int t = prog[b];
        CH(L_KICK,  b, P_KICK, 0);
        CH(L_CLAP,  b, P_CLAP, 0);
        CH(L_SNARE, b, P_SNARE, 0);
        CH(L_HAT,   b, P_HAT,  0);
        CH(L_BASS,  b, P_BASS, t);
        CH(L_PAD,   b, P_PAD,  t);
        CH(L_ARP,   b, P_ARP,  t);
        CH(L_LEAD,  b, (b % 2) ? P_LEAD2 : P_LEAD1, 0);
    }

    // Song: a 16-bar arrangement over 4 rows (each row = one 4-bar cycle). The
    // build is expressed as song rows -- empty cells (--) rest that lane. Row 4
    // is left empty, so the engine loops back to row 0. Every active chain is the
    // same length (4 bars), so all lanes advance together (no drag). Lane 7 is the
    // snare, layered under the clap once the backbeat kicks in.
    const uint8_t X = 0xFF;   // empty cell = lane rests this section
    auto SR = [&](int row, uint8_t k, uint8_t c, uint8_t h, uint8_t b,
                  uint8_t p, uint8_t a, uint8_t l, uint8_t sn) {
        auto& s = seq.song[row].tracks;
        s[0]=k; s[1]=c; s[2]=h; s[3]=b; s[4]=p; s[5]=a; s[6]=l; s[7]=sn;
    };
    SR(0, L_KICK, X,      L_HAT, L_BASS, L_PAD, X,      X,      X);        // intro: drums + bass + pad
    SR(1, L_KICK, L_CLAP, L_HAT, L_BASS, L_PAD, L_ARP,  X,      L_SNARE);  // + clap + arp + snare
    SR(2, L_KICK, L_CLAP, L_HAT, L_BASS, L_PAD, L_ARP,  L_LEAD, L_SNARE);  // full + lead
    SR(3, L_KICK, L_CLAP, L_HAT, L_BASS, L_PAD, L_ARP,  L_LEAD, L_SNARE);  // full

    // Tempo / name / straight timing.
    st.bpm = 128; st.bpm_frac = 0;
    st.project.groove = 0;                 // straight (Night Drive used swing)
    engine::setName(st.project.name, "SUNRISE");

}

// -----------------------------------------------------------------------------
// NEON DUSK — the startup song. Authored here, written to songs/neondusk.m8s.
//
// SUNRISE (below) predates FMSynth, WavSynth and HyperSynth, so it is entirely
// MacroSynth and samplers. NEON DUSK exists to put every synth engine on screen
// the moment the app opens:
//
//   00 KICK   Sampler     /samples/kick.wav
//   01 SNARE  Sampler     /samples/snare.wav
//   02 HAT    WavSynth    shape 8 NOISE -- a hat with no sample behind it
//   03 BASS   FMSynth     2-op through the 4-op chain, C modulating carrier D
//   04 PAD    HyperSynth  fifth-stack chord bank, swarm + width
//   05 ARP    WavSynth    shape 16 WT-OSC:LIQUID, MULT/WARP for movement
//   06 LEAD   MacroSynth  shape 28 PLUCKED
//   07 CLAP   Sampler     /samples/clap.wav
//
// 112 BPM against SUNRISE's 128, straight, D minor: Dm - Gm - Bb - Am, carried
// as chain transpose {0, -7, -4, -5}. All four moves descend, which keeps the
// bass in its register instead of climbing away over the cycle.
//
// Chord quality is the trap in a transpose-driven progression: the pad's chord
// bank is a fixed set of intervals, so a minor voicing transposed onto a major
// chord fights it. Both the pad and the arp are therefore quality-neutral --
// roots, fifths and octaves, no third anywhere -- and the lead is D-minor
// pentatonic with TRANSP OFF, so it sits still while the harmony moves under
// it. SUNRISE solved the same problem the same way; it is not a coincidence,
// it is what transpose-as-harmony costs.
static void composeNeonDusk(engine::Sequencer& seq, engine::EngineState& st) {
    using engine::InstType;

    auto amp = [&](int i, int atk, int hold, int dec) {
        auto& m = st.instruments[i].mods[0];
        m.type = 0;                 // AHD ENV
        m.dest = 1;                 // VOLUME
        m.amt  = 0xFF;
        m.p1 = atk; m.p2 = hold; m.p3 = dec;
    };

    // --- Instruments ---------------------------------------------------------
    // Indices double as lane numbers throughout.
    enum { I_KICK = 0, I_SNARE = 1, I_HAT = 2, I_BASS = 3,
           I_PAD = 4, I_ARP = 5, I_LEAD = 6, I_CLAP = 7 };

    for (auto& in : st.instruments) in.type = InstType::INST_NONE;

    // 00 KICK -- sampler, driven into SIN limiting for weight.
    {
        auto& in = st.instruments[I_KICK];
        in.type = InstType::INST_SAMPLER;
        engine::setName(in.name, "KICK");
        std::strncpy(in.sampler.samplePath, "/samples/kick.wav",
                     sizeof(in.sampler.samplePath) - 1);
        in.sampler.transp      = 0;      // drums never follow the progression
        in.sampler.play        = 0;      // FWD one-shot
        in.sampler.filter_type = 1;      // LP
        in.sampler.cutoff      = 0x58;
        in.sampler.res         = 0x14;
        in.sampler.amp         = 0x34;
        in.sampler.lim         = 1;      // SIN
        in.sampler.dry         = 0xF0;
        in.sampler.rev         = 0x06;
        amp(I_KICK, 0, 0x40, 0x20);
    }

    // 01 SNARE -- high-passed clear of the kick, sat back in reverb.
    {
        auto& in = st.instruments[I_SNARE];
        in.type = InstType::INST_SAMPLER;
        engine::setName(in.name, "SNARE");
        std::strncpy(in.sampler.samplePath, "/samples/snare.wav",
                     sizeof(in.sampler.samplePath) - 1);
        in.sampler.transp      = 0;
        in.sampler.play        = 0;
        in.sampler.filter_type = 2;      // HP
        in.sampler.cutoff      = 0x2C;
        in.sampler.amp         = 0x1C;
        in.sampler.dry         = 0xC8;
        in.sampler.del         = 0x20;
        in.sampler.rev         = 0x54;
        in.sampler.pan         = 0x8A;
        amp(I_SNARE, 0, 0x30, 0x18);
    }

    // 02 HAT -- WavSynth NOISE. The whole point of putting a synth here is that
    // it needs no sample: shape 8 is the deterministic LFSR noise, high-passed
    // hard and gated to a click by the envelope.
    {
        auto& in = st.instruments[I_HAT];
        in.type = InstType::INST_WAVSYNTH;
        engine::setName(in.name, "HAT");
        in.wav.transp      = 0;
        in.wav.shape       = 8;          // NOISE
        in.wav.size        = 0x40;
        in.wav.mult        = 0x00;
        in.wav.warp        = 0x00;
        in.wav.filter_type = 2;          // HP
        in.wav.cutoff      = 0xB4;
        in.wav.res         = 0x18;
        in.wav.amp         = 0x18;
        in.wav.dry         = 0xC0;
        in.wav.rev         = 0x14;
        in.wav.pan         = 0x72;       // opposite the snare
        amp(I_HAT, 0, 0x06, 0x08);       // very short -- a tick, not a wash
    }

    // 03 BASS -- FMSynth. Algorithm 0 is the straight chain A>B>C>D with D as
    // the carrier, so leaving A and B at level 0 reduces it to classic two-
    // operator FM: C at ratio 1 modulating D at ratio 1 gives a round bass with
    // a bite that a filtered saw does not have.
    {
        auto& in = st.instruments[I_BASS];
        in.type = InstType::INST_FMSYNTH;
        engine::setName(in.name, "BASS");
        in.fm.transp = 1;
        in.fm.algo   = 0;                // A > B > C > D
        in.fm.ops[0].level = 0x00;
        in.fm.ops[1].level = 0x00;
        in.fm.ops[2].shape = 0;          // SIN, the modulator
        in.fm.ops[2].ratio = 1;
        in.fm.ops[2].level = 0x5C;
        in.fm.ops[3].shape = 0;          // SIN, the carrier
        in.fm.ops[3].ratio = 1;
        in.fm.ops[3].level = 0xFF;
        in.fm.filter_type = 1;           // LP
        in.fm.cutoff      = 0x64;
        in.fm.res         = 0x30;
        in.fm.amp         = 0x24;
        in.fm.lim         = 1;           // SIN
        in.fm.dry         = 0xE8;
        amp(I_BASS, 0, 0x24, 0x30);
    }

    // 04 PAD -- HyperSynth. chords[0] is the active bank: root, fifth, octave,
    // twelfth, double octave and a doubled octave on top. No third, so it stays
    // consonant whether the bar underneath is minor or major.
    {
        auto& in = st.instruments[I_PAD];
        in.type = InstType::INST_HYPERSYN;
        engine::setName(in.name, "PAD");
        in.hyper.transp     = 1;
        in.hyper.chord_bank = 0;
        const int voicing[6] = {0, 7, 12, 19, 24, 12};
        for (int n = 0; n < 6; ++n) in.hyper.chords[0][n] = voicing[n];
        in.hyper.shift       = 0x80;     // even balance across the two triads
        in.hyper.swarm       = 0x4C;     // supersaw detune
        in.hyper.width       = 0xD0;     // wide
        in.hyper.subosc      = 0x28;
        in.hyper.filter_type = 1;        // LP
        in.hyper.cutoff      = 0x62;
        in.hyper.res         = 0x10;
        in.hyper.amp         = 0x14;
        in.hyper.dry         = 0xA0;
        in.hyper.cho         = 0x40;
        in.hyper.rev         = 0x70;
        amp(I_PAD, 0x50, 0xC0, 0x90);    // slow swell, long tail
    }

    // 05 ARP -- WavSynth wavetable. MULT and WARP are what make a wavetable
    // sound like something other than an oscillator; SCAN walks the table so
    // the repeated figure is not identical bar to bar.
    {
        auto& in = st.instruments[I_ARP];
        in.type = InstType::INST_WAVSYNTH;
        engine::setName(in.name, "ARP");
        in.wav.transp      = 1;
        in.wav.shape       = 16;         // WT-OSC:LIQUID
        in.wav.size        = 0x80;
        in.wav.mult        = 0x30;
        in.wav.warp        = 0x58;
        in.wav.scan        = 0x40;
        in.wav.filter_type = 1;          // LP
        in.wav.cutoff      = 0x8C;
        in.wav.res         = 0x28;
        in.wav.amp         = 0x18;
        in.wav.pan         = 0x94;
        in.wav.dry         = 0xB0;
        in.wav.del         = 0x48;
        in.wav.rev         = 0x38;
        amp(I_ARP, 0x02, 0x14, 0x2C);
    }

    // 06 LEAD -- MacroSynth PLUCKED, TRANSP OFF so the pentatonic line holds
    // still while the chords move beneath it.
    {
        auto& in = st.instruments[I_LEAD];
        in.type = InstType::INST_MACROSYN;
        engine::setName(in.name, "LEAD");
        in.macrosyn.transp      = 0;
        in.macrosyn.shape       = 28;    // PLUCKED
        in.macrosyn.timbre      = 0x70;
        in.macrosyn.color       = 0x90;
        in.macrosyn.filter_type = 1;     // LP
        in.macrosyn.cutoff      = 0xB0;
        in.macrosyn.res         = 0x1C;
        in.macrosyn.amp         = 0x1C;
        in.macrosyn.pan         = 0x6C;
        in.macrosyn.dry         = 0xB8;
        in.macrosyn.del         = 0x58;
        in.macrosyn.rev         = 0x48;
        amp(I_LEAD, 0x00, 0x28, 0x44);
    }

    // 07 CLAP -- one per bar on the 4, wide and wet. Sparser than SUNRISE's
    // 2-and-4 so the bar has somewhere to breathe.
    {
        auto& in = st.instruments[I_CLAP];
        in.type = InstType::INST_SAMPLER;
        engine::setName(in.name, "CLAP");
        std::strncpy(in.sampler.samplePath, "/samples/clap.wav",
                     sizeof(in.sampler.samplePath) - 1);
        in.sampler.transp      = 0;
        in.sampler.play        = 0;
        in.sampler.filter_type = 2;      // HP
        in.sampler.cutoff      = 0x24;
        in.sampler.amp         = 0x20;
        in.sampler.dry         = 0xC0;
        in.sampler.rev         = 0x60;
        in.sampler.pan         = 0x78;
        amp(I_CLAP, 0, 0x28, 0x20);
    }

    // --- Arrangement ---------------------------------------------------------
    seq.clear();

    auto S = [&](int p, int r, int note, int vol, int instr) {
        seq.phrases[p][r] = engine::Step{ (uint8_t)note, (uint8_t)vol, (uint8_t)instr, {} };
    };

    enum { P_KICK = 0, P_SNARE = 1, P_HAT = 2, P_BASS = 3, P_PAD = 4,
           P_ARP1 = 5, P_ARP2 = 6, P_LEAD1 = 7, P_LEAD2 = 8, P_CLAP = 9 };

    // C-4 = 60. D minor material; chain transpose supplies the harmony.
    const int D2 = 38, A2 = 45, D3 = 50, F3 = 53, A3 = 57,
              D4 = 62, F4 = 65, G4 = 67, A4 = 69, C5 = 72, D5 = 74;

    // Drums. 16 steps to the bar, 4 to the beat.
    for (int r : {0, 4, 8, 12}) S(P_KICK, r, 60, 0x72, I_KICK);
    S(P_KICK, 14, 60, 0x3E, I_KICK);            // ghost push into the next bar
    for (int r : {4, 12})       S(P_SNARE, r, 60, 0x50, I_SNARE);
    S(P_CLAP, 12, 60, 0x62, I_CLAP);            // once a bar, on the 4
    for (int r = 0; r < 16; r += 2)
        S(P_HAT, r, 60, (r % 4 == 0) ? 0x2A : 0x44, I_HAT);   // offbeats accented

    // Bass: root-driven, syncopated, with an octave lift late in the bar.
    S(P_BASS, 0,  D2, 0x68, I_BASS);  S(P_BASS, 3,  D2, 0x54, I_BASS);
    S(P_BASS, 6,  D3, 0x50, I_BASS);  S(P_BASS, 8,  D2, 0x64, I_BASS);
    S(P_BASS, 11, D2, 0x54, I_BASS);  S(P_BASS, 14, A2, 0x50, I_BASS);

    // Pad: one held note a bar; the long release carries it over the bar line.
    S(P_PAD, 0, D3, 0x48, I_PAD);

    // Arp: two bars of eighths, fifths and octaves only.
    { const int a1[8] = {D4, A4, D5, A4, D4, A4, D5, A4};
      for (int k = 0; k < 8; ++k) S(P_ARP1, k * 2, a1[k], 0x42, I_ARP); }
    { const int a2[8] = {A3, D4, A4, D5, A4, D4, A3, D4};
      for (int k = 0; k < 8; ++k) S(P_ARP2, k * 2, a2[k], 0x42, I_ARP); }

    // Lead: D-minor pentatonic (D F G A C), fixed against the moving chords.
    S(P_LEAD1, 0,  D4, 0x54, I_LEAD);  S(P_LEAD1, 4,  F4, 0x50, I_LEAD);
    S(P_LEAD1, 7,  A4, 0x4E, I_LEAD);  S(P_LEAD1, 10, G4, 0x4C, I_LEAD);
    S(P_LEAD1, 14, F4, 0x48, I_LEAD);
    S(P_LEAD2, 0,  A4, 0x54, I_LEAD);  S(P_LEAD2, 3,  C5, 0x50, I_LEAD);
    S(P_LEAD2, 6,  D5, 0x52, I_LEAD);  S(P_LEAD2, 9,  A4, 0x4C, I_LEAD);
    S(P_LEAD2, 12, F4, 0x4A, I_LEAD);  S(P_LEAD2, 15, D4, 0x46, I_LEAD);

    // --- Chains --------------------------------------------------------------
    // One chain per lane, four slots = the four-bar Dm-Gm-Bb-Am cycle. Every
    // chain is the same length, so all lanes turn over together and the song row
    // advances cleanly. Drums carry transpose 0 and have TRANSP OFF besides.
    enum { L_KICK = 0, L_SNARE = 1, L_HAT = 2, L_BASS = 3,
           L_PAD = 4, L_ARP = 5, L_LEAD = 6, L_CLAP = 7 };
    const int prog[4] = {0, -7, -4, -5};    // Dm -> Gm -> Bb -> Am, all descending

    auto CH = [&](int lane, int slot, int phrase, int tsp) {
        seq.chains[lane][slot] = engine::ChainStep{ (uint8_t)phrase, (int8_t)tsp };
    };
    for (int b = 0; b < 4; ++b) {
        const int t = prog[b];
        CH(L_KICK,  b, P_KICK,  0);
        CH(L_SNARE, b, P_SNARE, 0);
        CH(L_HAT,   b, P_HAT,   0);
        CH(L_CLAP,  b, P_CLAP,  0);
        CH(L_BASS,  b, P_BASS,  t);
        CH(L_PAD,   b, P_PAD,   t);
        CH(L_ARP,   b, (b % 2) ? P_ARP2  : P_ARP1,  t);
        CH(L_LEAD,  b, (b % 2) ? P_LEAD2 : P_LEAD1, 0);   // TRANSP OFF anyway
    }

    // --- Song ----------------------------------------------------------------
    // Four rows, one four-bar cycle each. The build is expressed as empty song
    // cells rather than as extra chains: an empty cell rests that lane for the
    // section. Row 4 is left empty so the engine wraps to row 0.
    //
    // It opens on pad and arp alone -- no drums for four bars -- which is the
    // clearest way to hear that the pad is a HyperSynth and not a saw.
    const uint8_t X = 0xFF;
    auto SR = [&](int row, uint8_t k, uint8_t sn, uint8_t h, uint8_t b,
                  uint8_t p, uint8_t a, uint8_t l, uint8_t cl) {
        auto& s = seq.song[row].tracks;
        s[0]=k; s[1]=sn; s[2]=h; s[3]=b; s[4]=p; s[5]=a; s[6]=l; s[7]=cl;
    };
    SR(0, X,      X,       X,     X,      L_PAD, L_ARP, X,      X);        // pad + arp
    SR(1, L_KICK, X,       L_HAT, L_BASS, L_PAD, L_ARP, X,      X);        // + kick, hat, bass
    SR(2, L_KICK, L_SNARE, L_HAT, L_BASS, L_PAD, L_ARP, L_LEAD, L_CLAP);   // full
    SR(3, L_KICK, L_SNARE, L_HAT, L_BASS, L_PAD, L_ARP, X,      L_CLAP);   // lead drops out

    st.bpm = 112; st.bpm_frac = 0;
    st.project.groove = 0;                  // straight
    engine::setName(st.project.name, "NEON DUSK");
}

int main(int argc, char** argv) {
    std::string song    = "neondusk";
    std::string outSong;
    std::string tmpl    = "third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]{ return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if      (a == "--song")     song    = next();
        else if (a == "--out")      outSong = next();
        else if (a == "--template") tmpl    = next();
    }
    if (song != "neondusk" && song != "sunrise") {
        std::fprintf(stderr, "FAIL: --song must be neondusk or sunrise (got '%s')\n",
                     song.c_str());
        return 2;
    }
    if (outSong.empty()) outSong = "songs/" + song + ".m8s";
    std::filesystem::create_directories(std::filesystem::path(outSong).parent_path());

    // Engine on the heap (large inline DSP buffers). loadDemoSong seeds the tuned
    // instrument patches and, for the samplers, the loaded sample handles that
    // SUNRISE builds on; NEON DUSK writes its instruments from scratch and needs
    // only the sample *paths*, which is all saveNewSong persists anyway.
    engine::CommandRing<engine::EngineCommand, 1024> ring;
    auto engPtr = std::make_unique<engine::Engine>(ring);
    engine::Engine& eng = *engPtr;
    eng.loadDemoSong();
    auto& st  = eng.getStateForInit();
    auto& seq = eng.getSequencerForInit();

    if (song == "sunrise") composeSunrise(seq, st);
    else                   composeNeonDusk(seq, st);

    // --- Export --------------------------------------------------------------
    std::string err;
    if (!io::saveNewSong(outSong, tmpl, seq, st, err)) {
        std::fprintf(stderr, "FAIL: saveNewSong: %s\n", err.c_str());
        return 1;
    }
    std::printf("  wrote %s  (%.12s, %d BPM)\n", outSong.c_str(), st.project.name, st.bpm);

    // Verify it reloads.
    auto rt = io::loadSong(outSong, "songs");
    if (!rt.ok) { std::fprintf(stderr, "FAIL: reload: %s\n", rt.error.c_str()); return 1; }
    std::printf("  reload OK: bpm=%d name=%.12s, %zu sample path(s), %zu missing\n",
                rt.state.bpm, rt.state.project.name,
                rt.samplePaths.size(), rt.missing.size());
    if (!rt.missing.empty()) {
        std::fprintf(stderr, "WARN: missing samples (check songs/samples/*.wav):\n");
        for (auto& m : rt.missing) std::fprintf(stderr, "    %s\n", m.c_str());
    }
    std::printf("done. Play: m8_render --load %s --sample-root songs --song --seconds 40 --out %s\n",
                outSong.c_str(), song.c_str());
    return 0;
}