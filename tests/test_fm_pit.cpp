// ===========================================================================
// test_fm_pit.cpp — the FM per-operator PIT destination really is wired up.
//
// FMSYNTH_IMPLEMENTATION.md listed "PIT per-operator destination: not fully
// implemented in the decode function -- marked as a TODO in the code. Needs
// semitone-to-frequency conversion." Checked on 2026-08-24: there is no TODO
// left in SynthVoice.cpp, and the decode does exactly that conversion --
// `case 3` accumulates a semitone offset which `opFreq` then raises through
// pow(2, offset/12).
//
// That makes it the fourth stale "not implemented" note found in one day,
// alongside SLICE, the ARP/RET/RND family and the WavSynth wavetables. So this
// pins the behaviour rather than the claim: if PIT is ever quietly disconnected
// the test fails, and the document cannot drift back into being right by
// accident.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

namespace {

// Dominant frequency by zero-crossing rate over a steady window.
double pitchOf(const std::vector<float>& a, int sr) {
    const size_t n = a.size() / 2;
    const size_t from = std::min<size_t>(n / 4, 4000), to = std::min<size_t>(n, from + 16000);
    if (to <= from + 100) return 0.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((a[(i - 1) * 2] < 0.0f) != (a[i * 2] < 0.0f)) ++crossings;
    return (crossings / 2.0) / (double(to - from) / double(sr));
}

// modByte packs destination in bits 3:0 and mod source in bits 7:4.
// dest 3 = PIT, source 0 = mod macro 1.
constexpr int kDestPit = 3;

std::vector<float> render(int modByte, int mod1Value) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    auto& inst = state.instruments[0];
    inst.type = InstType::INST_FMSYNTH;

    auto& fm = inst.fm;
    fm.algo   = 11;                 // all four operators straight to the output
    fm.volume = 0x40;
    fm.dry    = 0xFF;
    fm.pan    = 0x80;
    fm.lim    = 0;
    fm.filter_type = 0;

    for (int i = 0; i < 4; ++i) {
        fm.ops[i].shape       = 0;   // sine
        fm.ops[i].ratio       = 1;
        fm.ops[i].ratio_fine  = 0;
        fm.ops[i].level       = (i == 0) ? 0xFF : 0x00;   // only op 0 sounds
        fm.ops[i].feedback    = 0;
        fm.ops[i].mod_a       = 0;
        fm.ops[i].mod_b       = 0;
    }
    fm.ops[0].mod_a = modByte;
    fm.mod1 = mod1Value;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(48000);
    return host.audio();
}

} // namespace

TEST_CASE("FM PIT shifts an operator's pitch, in semitones", "[fmsynth][fmpit]") {
    // mod1 is read as bipolar around 128, so 128 is no shift and 255 is full
    // positive. The decode scales that by 24 semitones, so a full-positive mod
    // should land the operator about two octaves up.
    const auto flat  = render(0, 128);                       // no destination
    const auto up    = render((0 << 4) | kDestPit, 255);     // PIT from mod1, max

    const double fFlat = pitchOf(flat, 48000);
    const double fUp   = pitchOf(up, 48000);
    INFO("flat = " << fFlat << " Hz, PIT up = " << fUp << " Hz");

    // There has to be a tone at all, or this compares two silences -- the
    // failure mode that let an app-vs-render comparison pass on silence once.
    REQUIRE(fFlat > 20.0);
    REQUIRE(fUp > 20.0);

    // Up, and by a long way. The exact factor depends on the 24-semitone scale
    // constant, which is a CHOICE and not hardware-measured (see
    // FMSYNTH_IMPLEMENTATION.md), so this asserts the direction and an order of
    // magnitude rather than a number that would pin an unverified constant.
    CHECK(fUp > fFlat * 2.0);
}

TEST_CASE("FM PIT with a centred modulator changes nothing", "[fmsynth][fmpit]") {
    // 128 is the bipolar centre. A destination that shifted pitch at centre
    // would detune every FM patch that merely names PIT without meaning to.
    const auto none    = render(0, 128);
    const auto centred = render((0 << 4) | kDestPit, 128);

    REQUIRE(none.size() == centred.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < none.size(); ++i)
        maxDiff = std::max(maxDiff, std::fabs(none[i] - centred[i]));
    INFO("max diff at centre = " << maxDiff);
    CHECK(maxDiff < 1e-6f);
}

TEST_CASE("FM PIT can shift downward too", "[fmsynth][fmpit]") {
    // The mod is bipolar, so below centre must lower the operator. A one-sided
    // implementation would look correct in the test above and be wrong for half
    // the control range -- which is exactly how the scale OFFSET encoding was
    // wrong for months.
    const auto flat = render(0, 128);
    const auto down = render((0 << 4) | kDestPit, 0);

    const double fFlat = pitchOf(flat, 48000);
    const double fDown = pitchOf(down, 48000);
    INFO("flat = " << fFlat << " Hz, PIT down = " << fDown << " Hz");
    REQUIRE(fFlat > 20.0);
    CHECK(fDown < fFlat);
}
