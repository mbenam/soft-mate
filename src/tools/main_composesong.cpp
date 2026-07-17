// -----------------------------------------------------------------------------
// m8_composesong — authors the startup song "SUNRISE" and writes it to
// songs/sunrise.m8s (data, not hard-coded into the app).
//
// SUNRISE is a fresh composition, distinct from the "Night Drive" demo:
//   - 128 BPM, four-on-the-floor house feel, straight timing
//   - A-minor progression Am - F - C - G (a bright, uplifting loop)
//   - sampler drums (the committed /samples/*.wav kit) + MacroSynth saw for
//     bass / pad / arp / lead
//   - a 16-bar loop with a build: drums+bass+pad from the top, clap+arp enter at
//     bar 5, lead at bar 9.
//
// It reuses the demo's *tuned instrument patches* (envelopes/filters) — the saw
// and the drum kit — but every note, chain, tempo and key is new. The chord
// progression is driven by chain transpose: drum instruments have TRANSP OFF so
// they never pitch-shift, the melodic instruments have TRANSP ON and follow the
// per-bar transpose. The song is exported via io::saveNewSong(); the app loads
// the .m8s at startup, so nothing about the song lives in the app binary.
//
// Regenerate:  cmake --build build --config Release --target m8_composesong
//              build\Release\m8_composesong.exe
// Play:        build\Release\m8_render.exe --load songs/sunrise.m8s \
//                  --sample-root songs --song --seconds 30 --out sunrise
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

int main(int argc, char** argv) {
    std::string outSong = "songs/sunrise.m8s";
    std::string tmpl    = "third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]{ return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if      (a == "--out")      outSong = next();
        else if (a == "--template") tmpl    = next();
    }
    std::filesystem::create_directories(std::filesystem::path(outSong).parent_path());

    // Engine on the heap (large inline DSP buffers). loadDemoSong seeds the tuned
    // instrument patches (drum kit + saw); we then replace the whole arrangement.
    engine::CommandRing<engine::EngineCommand, 1024> ring;
    auto engPtr = std::make_unique<engine::Engine>(ring);
    engine::Engine& eng = *engPtr;
    eng.loadDemoSong();
    auto& st  = eng.getStateForInit();
    auto& seq = eng.getSequencerForInit();

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

    // --- Export --------------------------------------------------------------
    std::string err;
    if (!io::saveNewSong(outSong, tmpl, seq, st, err)) {
        std::fprintf(stderr, "FAIL: saveNewSong: %s\n", err.c_str());
        return 1;
    }
    std::printf("  wrote %s  (SUNRISE, 128 BPM, Am-F-C-G)\n", outSong.c_str());

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
    std::printf("done. Play: m8_render --load %s --sample-root songs --song --seconds 30 --out sunrise\n",
                outSong.c_str());
    return 0;
}
