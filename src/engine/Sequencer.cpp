#include "Sequencer.h"
#include <cmath>
#include <cstring>

namespace m8::engine {

Sequencer::Sequencer() {
    for (int i = 0; i < 16; ++i) grooves[0].steps[i] = 6;
    grooves[0].length = 16;

    // Groove 1: light swing — long-short pairs. Same bar length (6+6 = 7+5).
    for (int i = 0; i < 16; ++i) grooves[1].steps[i] = (i % 2 == 0) ? 7 : 5;
    grooves[1].length = 16;
}

void Sequencer::clear() {
    *this = Sequencer();
}

// ---------------------------------------------------------------------------
// Demo song — "NIGHT DRIVE", 16 bars in C minor.
//
// The engine is monophonic per track (one voice each), so the pad is a
// sustained single-note root movement, not a chord stack. Dynamics come from
// the volume column: only DEL / KIL / HOP are implemented in the FX chain.
//
// Root motion:  Cm  -  Ab  -  Eb  -  Bb   (one bar each, repeating)
//
// Tracks         Instrument
//   0  KICK       00  sampler, generated kick
//   1  SNARE      01  sampler, generated snare
//   2  HAT        02  sampler, generated hat
//   3  BASS       03  macrosyn, LP + resonance
//   4  PAD        04  macrosyn, slow attack, chorus + reverb
//   5  ARP        05  macrosyn, short env, delay
//   6  LEAD       06  macrosyn, LFO vibrato, delay
//   7  PERC       07  sampler, generated clap
// ---------------------------------------------------------------------------

namespace {

// MIDI note helpers. C-4 = 60 (the sampler root).
constexpr uint8_t C2 = 36, D2 = 38, Eb2 = 39, F2 = 41, G2 = 43, Ab2 = 44, Bb2 = 46;
constexpr uint8_t C3 = 48, Eb3 = 51, F3 = 53, G3 = 55, Ab3 = 56, Bb3 = 58;
constexpr uint8_t C4 = 60, D4 = 62, Eb4 = 63, F4 = 65, G4 = 67, Ab4 = 68, Bb4 = 70;
constexpr uint8_t C5 = 72, D5 = 74, Eb5 = 75, F5 = 77, G5 = 79, Ab5 = 80, Bb5 = 82;
constexpr uint8_t C6 = 84;

} // namespace

void Sequencer::loadDemoSong() {
    auto N = [&](int ph, int row, uint8_t note, uint8_t vol, uint8_t inst) {
        phrases[ph][row].note  = note;
        phrases[ph][row].vol   = vol;
        phrases[ph][row].instr = inst;
    };
    auto FX = [&](int ph, int row, int slot, FxCmd c, uint8_t v) {
        phrases[ph][row].fx[slot] = { c, v };
    };

    // =====================================================================
    // DRUMS
    // =====================================================================

    // 0x00 — kick, four on the floor, with a pushed 16th before the 4
    for (int r = 0; r < 16; r += 4) N(0x00, r, C4, (r == 0) ? 0x7F : 0x72, 0);
    N(0x00, 14, C4, 0x48, 0);                       // ghost push into the next bar

    // 0x01 — kick, same but the last beat becomes a double
    for (int r = 0; r < 12; r += 4) N(0x01, r, C4, (r == 0) ? 0x7F : 0x72, 0);
    N(0x01, 12, C4, 0x72, 0);
    N(0x01, 14, C4, 0x6A, 0);
    N(0x01, 15, C4, 0x50, 0);

    // 0x02 — kick, breakdown: only the downbeat
    N(0x02, 0, C4, 0x7F, 0);
    N(0x02, 8, C4, 0x60, 0);

    // 0x04 — snare, backbeat on 2 and 4 + ghosts
    N(0x04, 4,  C4, 0x78, 1);
    N(0x04, 12, C4, 0x78, 1);
    N(0x04, 7,  C4, 0x28, 1);                       // ghost
    N(0x04, 15, C4, 0x30, 1);                       // ghost
    FX(0x04, 15, 0, FxCmd::DEL, 3);                 // ...pushed late, off the grid

    // 0x05 — snare fill: 16th roll into the turnaround
    N(0x05, 4,  C4, 0x78, 1);
    N(0x05, 10, C4, 0x40, 1);
    N(0x05, 11, C4, 0x50, 1);
    N(0x05, 12, C4, 0x60, 1);
    N(0x05, 13, C4, 0x68, 1);
    N(0x05, 14, C4, 0x70, 1);
    N(0x05, 15, C4, 0x7F, 1);

    // 0x08 — hats, straight 8ths, accented on the beat, all choked short
    for (int r = 0; r < 16; r += 2) {
        N(0x08, r, C4, (r % 4 == 0) ? 0x50 : 0x30, 2);
        FX(0x08, r, 0, FxCmd::KIL, 2);              // cut after 2 ticks = tight
    }

    // 0x09 — hats, 16ths with a shuffle feel in the volumes
    for (int r = 0; r < 16; ++r) {
        uint8_t v = (r % 4 == 0) ? 0x52 : (r % 2 == 0) ? 0x34 : 0x22;
        N(0x09, r, C4, v, 2);
        FX(0x09, r, 0, FxCmd::KIL, 1);              // even tighter
    }

    // ---------------------------------------------------------------------
    // INTRO variants — the same parts, played quieter and sparser. This is how
    // a tracker builds an arrangement: not with a mixer fade, but by writing a
    // held-back version of the part and swapping the chain.
    // ---------------------------------------------------------------------

    // 0x03 — kick, intro: same pattern, backed off, no ghost push
    for (int r = 0; r < 16; r += 4) N(0x03, r, C4, (r == 0) ? 0x60 : 0x54, 0);

    // 0x06 — snare, intro: backbeat only, no ghosts
    N(0x06, 4,  C4, 0x54, 1);
    N(0x06, 12, C4, 0x54, 1);

    // 0x0A — hats, intro: quarter notes only, very quiet
    for (int r = 0; r < 16; r += 4) {
        N(0x0A, r, C4, 0x26, 2);
        FX(0x0A, r, 0, FxCmd::KIL, 2);
    }

    // 0x0B — bass, intro: root only, no octave jumps
    N(0x0B, 0,  C2, 0x58, 3);
    N(0x0B, 8,  C2, 0x50, 3);
    N(0x0B, 14, G2, 0x48, 3);

    // =====================================================================
    // BASS  — one phrase per bar, following the root motion
    // =====================================================================

    // 0x0C — Cm
    N(0x0C, 0,  C2,  0x70, 3);
    N(0x0C, 3,  C2,  0x50, 3);
    N(0x0C, 6,  C3,  0x60, 3);                      // octave jump
    N(0x0C, 8,  C2,  0x68, 3);
    N(0x0C, 11, Eb2, 0x58, 3);
    N(0x0C, 14, G2,  0x60, 3);

    // 0x0D — Ab
    N(0x0D, 0,  Ab2, 0x70, 3);
    N(0x0D, 3,  Ab2, 0x50, 3);
    N(0x0D, 6,  Ab3, 0x60, 3);
    N(0x0D, 8,  Ab2, 0x68, 3);
    N(0x0D, 11, C3,  0x58, 3);
    N(0x0D, 14, Eb3, 0x60, 3);

    // 0x0E — Eb
    N(0x0E, 0,  Eb2, 0x70, 3);
    N(0x0E, 3,  Eb2, 0x50, 3);
    N(0x0E, 6,  Eb3, 0x60, 3);
    N(0x0E, 8,  Eb2, 0x68, 3);
    N(0x0E, 11, G2,  0x58, 3);
    N(0x0E, 14, Bb2, 0x60, 3);

    // 0x0F — Bb, with a walk-up back to C
    N(0x0F, 0,  Bb2, 0x70, 3);
    N(0x0F, 3,  Bb2, 0x50, 3);
    N(0x0F, 6,  Bb3, 0x60, 3);
    N(0x0F, 8,  Bb2, 0x68, 3);
    N(0x0F, 10, C3,  0x58, 3);
    N(0x0F, 12, D2,  0x58, 3);
    N(0x0F, 14, Eb2, 0x68, 3);

    // =====================================================================
    // PAD  — sustained roots. One note per bar; the voice holds until the
    //        next note-on, which is what gives it the long swell.
    // =====================================================================
    N(0x10, 0, C4,  0x48, 4);                       // Cm
    N(0x11, 0, Ab3, 0x48, 4);                       // Ab
    N(0x12, 0, Eb4, 0x48, 4);                       // Eb
    N(0x13, 0, Bb3, 0x48, 4);                       // Bb

    // =====================================================================
    // ARP  — Cm7 shapes, 16ths, following the chord
    // =====================================================================
    const uint8_t arpCm[4] = { C5,  Eb5, G5,  Bb5 };
    const uint8_t arpAb[4] = { Ab4, C5,  Eb5, G5  };
    const uint8_t arpEb[4] = { Eb5, G5,  Bb5, D5  };
    const uint8_t arpBb[4] = { Bb4, D5,  F5,  Ab5 };

    auto arp = [&](int ph, const uint8_t* set) {
        for (int r = 0; r < 16; ++r) {
            uint8_t v = (r % 4 == 0) ? 0x40 : 0x2C;
            N(ph, r, set[r % 4], v, 5);
        }
    };
    arp(0x14, arpCm);
    arp(0x15, arpAb);
    arp(0x16, arpEb);
    arp(0x17, arpBb);

    // =====================================================================
    // LEAD  — a melody, not a scale run. Long notes, breathing room.
    // =====================================================================

    // 0x18 — phrase 1
    N(0x18, 0,  G4,  0x5A, 6);
    N(0x18, 6,  Bb4, 0x50, 6);
    N(0x18, 8,  C5,  0x60, 6);
    N(0x18, 14, Bb4, 0x48, 6);

    // 0x19 — phrase 2 (answers it, lands lower)
    N(0x19, 0,  Ab4, 0x5A, 6);
    N(0x19, 4,  G4,  0x50, 6);
    N(0x19, 8,  Eb4, 0x58, 6);
    N(0x19, 12, F4,  0x4A, 6);
    FX(0x19, 12, 0, FxCmd::DEL, 3);                 // dragged, behind the beat

    // 0x1A — phrase 3 (climbs)
    N(0x1A, 0,  G4,  0x5A, 6);
    N(0x1A, 4,  Bb4, 0x54, 6);
    N(0x1A, 8,  C5,  0x60, 6);
    N(0x1A, 10, Eb5, 0x64, 6);
    N(0x1A, 14, D5,  0x50, 6);

    // 0x1B — phrase 4 (resolves)
    N(0x1B, 0,  C5,  0x64, 6);
    N(0x1B, 6,  Bb4, 0x52, 6);
    N(0x1B, 8,  G4,  0x56, 6);
    N(0x1B, 12, C4,  0x60, 6);

    // =====================================================================
    // PERC  — claps on the backbeat, plus a build in the turnaround bar
    // =====================================================================
    N(0x1C, 4,  C4, 0x58, 7);
    N(0x1C, 12, C4, 0x58, 7);

    // 0x1D — turnaround: accelerating claps (a fill without a fill track)
    N(0x1D, 4,  C4, 0x58, 7);
    N(0x1D, 8,  C4, 0x40, 7);
    N(0x1D, 12, C4, 0x60, 7);
    N(0x1D, 14, C4, 0x50, 7);
    N(0x1D, 15, C4, 0x70, 7);

    // =====================================================================
    // CHAINS — 4 bars each, one per track per section
    // =====================================================================
    auto CH = [&](int ch, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        chains[ch][0].phrase = a;
        chains[ch][1].phrase = b;
        chains[ch][2].phrase = c;
        chains[ch][3].phrase = d;
    };

    CH(0x00, 0x00, 0x00, 0x00, 0x01);   // kick   — normal, fill on bar 4
    CH(0x01, 0x02, 0x02, 0x00, 0x01);   // kick   — breakdown then back in
    CH(0x02, 0x04, 0x04, 0x04, 0x05);   // snare  — backbeat, fill on bar 4
    CH(0x03, 0x08, 0x09, 0x08, 0x09);   // hats   — 8ths / 16ths alternating
    CH(0x04, 0x0C, 0x0D, 0x0E, 0x0F);   // bass   — Cm Ab Eb Bb
    CH(0x05, 0x10, 0x11, 0x12, 0x13);   // pad    — same roots, sustained
    CH(0x06, 0x14, 0x15, 0x16, 0x17);   // arp    — follows the chords
    CH(0x07, 0x18, 0x19, 0x1A, 0x1B);   // lead   — 4-phrase melody
    CH(0x08, 0x1C, 0x1C, 0x1C, 0x1D);   // perc   — claps, build on bar 4

    // Intro chains — held back, and resolving into the full pattern on bar 4 so
    // the transition into section B lands rather than lurching.
    CH(0x09, 0x03, 0x03, 0x03, 0x00);   // kick   intro
    CH(0x0A, 0x06, 0x06, 0x06, 0x04);   // snare  intro
    CH(0x0B, 0x0A, 0x0A, 0x08, 0x08);   // hats   intro
    CH(0x0C, 0x0B, 0x0B, 0x0C, 0x0C);   // bass   intro

    // Transpose the last chain repeat up a fourth for a lift.
    chains[0x04][3].tsp = 0;            // bass stays put (it walks instead)

    // =====================================================================
    // SONG — 4 sections x 4 bars = 16 bars, arranged to build
    // =====================================================================
    //          KICK  SNARE  HAT   BASS  PAD   ARP   LEAD  PERC
    // row 0    intro — the same parts, held back
    song[0].tracks[0] = 0x09;
    song[0].tracks[1] = 0x0A;
    song[0].tracks[2] = 0x0B;
    song[0].tracks[3] = 0x0C;

    // row 1    + pad, arp, perc
    song[1].tracks[0] = 0x00;
    song[1].tracks[1] = 0x02;
    song[1].tracks[2] = 0x03;
    song[1].tracks[3] = 0x04;
    song[1].tracks[4] = 0x05;
    song[1].tracks[5] = 0x06;
    song[1].tracks[7] = 0x08;

    // row 2    everything — the lead enters
    song[2].tracks[0] = 0x00;
    song[2].tracks[1] = 0x02;
    song[2].tracks[2] = 0x03;
    song[2].tracks[3] = 0x04;
    song[2].tracks[4] = 0x05;
    song[2].tracks[5] = 0x06;
    song[2].tracks[6] = 0x07;
    song[2].tracks[7] = 0x08;

    // row 3    breakdown for two bars, then back in for the turnaround
    song[3].tracks[0] = 0x01;           // kick drops out then returns
    song[3].tracks[1] = 0x02;
    song[3].tracks[2] = 0x03;
    song[3].tracks[3] = 0x04;
    song[3].tracks[4] = 0x05;
    song[3].tracks[5] = 0x06;
    song[3].tracks[6] = 0x07;
    song[3].tracks[7] = 0x08;
    // ...then the song wraps back to row 0.
}

} // namespace m8::engine
