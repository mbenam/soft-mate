// 3-band parametric EQ DSP (EQ_SPEC.md step 3).
//
// These measure the filters rather than trusting them: push a sine through at a
// known frequency and compare the output level to the input. That turns each
// filter type's defining property into an assertion -- a low cut attenuates
// lows, a bell lifts its centre, an allpass changes nothing about the
// magnitude.
//
// The one thing NOT asserted to a tight tolerance is the exact Q curve. The
// byte-to-Q mapping was read off the device's response display by eye
// (EQ_SPEC.md §8), so it is checked for the right ordering and rough magnitude,
// not to two decimal places.

#include <catch2/catch_test_macros.hpp>
#include "engine/EqFilter.h"
#include <cmath>
#include <vector>

using namespace m8::engine;

namespace {

constexpr float kSr = 48000.0f;

// Magnitude response at one frequency: run a sine through, discard the settling
// transient, and compare output RMS to input RMS. Returns a linear gain.
float responseAt(EqProcessor& eq, float freqHz, float seconds = 0.25f) {
    const int total = static_cast<int>(kSr * seconds);
    const int settle = total / 2;              // biquads settle in far less
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 0; i < total; ++i) {
        const float x = std::sin(6.28318530718f * freqHz * float(i) / kSr);
        float l = x, r = x;
        eq.process(l, r);
        if (i >= settle) {
            sumIn  += double(x) * x;
            sumOut += double(l) * l;
        }
    }
    if (sumIn <= 0.0) return 0.0f;
    return static_cast<float>(std::sqrt(sumOut / sumIn));
}

float toDb(float linear) { return 20.0f * std::log10(std::max(linear, 1e-9f)); }

// A bank with one configured band and the other two forced flat, so a
// measurement only ever sees the band under test.
EqBank oneBand(int type, int mode, int freq, int gainHundredths, int qByte) {
    EqBank b;
    b.low  = EqBand{ 2, 0, 1000, 0, 50, 0x02 };   // bell at 0 dB = inert
    b.mid  = EqBand{ type, mode, freq, gainHundredths, qByte, 0x02 };
    b.high = EqBand{ 2, 0, 1000, 0, 50, 0x02 };
    return b;
}

EqProcessor makeEq(const EqBank& bank) {
    EqProcessor eq;
    eq.configure(bank, kSr);
    return eq;
}

} // namespace

TEST_CASE("EQ1 a flat EQ is a true bypass", "[eq]") {
    // The factory default is three bands at 0 dB. Adding an EQ to a signal path
    // must not alter one sample of an untouched song, so this has to be exact
    // equality, not "close".
    EqProcessor eq = makeEq(EqBank{});
    REQUIRE(eq.isBypass());

    bool identical = true;
    for (int i = 0; i < 4096; ++i) {
        const float x = std::sin(6.28318530718f * 440.0f * float(i) / kSr);
        float l = x, r = x * 0.5f;
        eq.process(l, r);
        if (l != x || r != x * 0.5f) { identical = false; break; }
    }
    REQUIRE(identical);
}

TEST_CASE("EQ2 a bell lifts its centre and leaves the rest alone", "[eq]") {
    EqProcessor eq = makeEq(oneBand(2 /*BELL*/, 0, 1000, 1200 /*+12dB*/, 50));
    REQUIRE_FALSE(eq.isBypass());

    const float atCentre = toDb(responseAt(eq, 1000.0f));
    eq.reset();
    const float wayBelow = toDb(responseAt(eq, 50.0f));
    eq.reset();
    const float wayAbove = toDb(responseAt(eq, 15000.0f));

    REQUIRE(atCentre > 11.0f);
    REQUIRE(atCentre < 13.0f);
    REQUIRE(std::fabs(wayBelow) < 1.0f);
    REQUIRE(std::fabs(wayAbove) < 1.0f);
}

TEST_CASE("EQ3 a cut band removes its side of the spectrum", "[eq]") {
    SECTION("LOWCUT keeps highs, loses lows") {
        EqProcessor eq = makeEq(oneBand(0 /*LOWCUT*/, 0, 1000, 0, 50));
        const float low = toDb(responseAt(eq, 60.0f));
        eq.reset();
        const float high = toDb(responseAt(eq, 10000.0f));
        REQUIRE(low < -20.0f);
        REQUIRE(std::fabs(high) < 1.0f);
    }
    SECTION("HICUT keeps lows, loses highs") {
        EqProcessor eq = makeEq(oneBand(5 /*HICUT*/, 0, 1000, 0, 50));
        const float low = toDb(responseAt(eq, 60.0f));
        eq.reset();
        const float high = toDb(responseAt(eq, 12000.0f));
        REQUIRE(std::fabs(low) < 1.0f);
        REQUIRE(high < -20.0f);
    }
}

TEST_CASE("EQ4 shelves tilt one end of the spectrum", "[eq]") {
    SECTION("LOWSHELF") {
        EqProcessor eq = makeEq(oneBand(1 /*LOWSHELF*/, 0, 1000, 1200, 50));
        const float low = toDb(responseAt(eq, 60.0f));
        eq.reset();
        const float high = toDb(responseAt(eq, 15000.0f));
        REQUIRE(low > 10.0f);            // shelf reaches its full boost
        REQUIRE(std::fabs(high) < 1.0f); // and leaves the other end alone
    }
    SECTION("HI.SHELF") {
        EqProcessor eq = makeEq(oneBand(4 /*HISHELF*/, 0, 1000, 1200, 50));
        const float low = toDb(responseAt(eq, 60.0f));
        eq.reset();
        const float high = toDb(responseAt(eq, 15000.0f));
        REQUIRE(std::fabs(low) < 1.0f);
        REQUIRE(high > 10.0f);
    }
}

TEST_CASE("EQ5 bandpass passes its centre only", "[eq]") {
    EqProcessor eq = makeEq(oneBand(3 /*BANDPASS*/, 0, 1000, 0, 70));
    const float centre = toDb(responseAt(eq, 1000.0f));
    eq.reset();
    const float below = toDb(responseAt(eq, 100.0f));
    eq.reset();
    const float above = toDb(responseAt(eq, 10000.0f));

    REQUIRE(std::fabs(centre) < 1.0f);   // 0 dB at the peak, by construction
    REQUIRE(below < -15.0f);
    REQUIRE(above < -15.0f);
}

TEST_CASE("EQ6 allpass leaves every magnitude untouched", "[eq]") {
    // Its whole point: the magnitude response is flat everywhere and only the
    // phase moves. If this fails the coefficients are wrong.
    EqProcessor eq = makeEq(oneBand(6 /*ALLPASS*/, 0, 1000, 0, 50));
    REQUIRE_FALSE(eq.isBypass());

    bool flat = true;
    for (float f : {100.0f, 500.0f, 1000.0f, 2000.0f, 8000.0f}) {
        eq.reset();
        if (std::fabs(toDb(responseAt(eq, f))) > 0.5f) { flat = false; break; }
    }
    REQUIRE(flat);
}

TEST_CASE("EQ7 Q byte widens and narrows the bell", "[eq]") {
    // The mapping is approximate (read off the device by eye), so this asserts
    // the ordering and rough scale rather than exact bandwidths: a low byte is
    // a broad boost that still lifts a distant frequency, a high byte is narrow
    // enough to leave it alone.
    EqProcessor wide = makeEq(oneBand(2, 0, 1000, 1200, 10));
    EqProcessor mid  = makeEq(oneBand(2, 0, 1000, 1200, 50));
    EqProcessor narrow = makeEq(oneBand(2, 0, 1000, 1200, 90));

    const float wideAt200   = toDb(responseAt(wide, 200.0f));
    const float midAt200    = toDb(responseAt(mid, 200.0f));
    const float narrowAt200 = toDb(responseAt(narrow, 200.0f));

    REQUIRE(wideAt200 > midAt200);
    REQUIRE(midAt200 > narrowAt200);
    REQUIRE(narrowAt200 < 1.0f);     // narrow bell doesn't reach 200 Hz
    REQUIRE(wideAt200 > 5.0f);       // wide one very much does

    // And all three still hit full gain at the centre.
    for (EqProcessor* p : { &wide, &mid, &narrow }) {
        p->reset();
        const float c = toDb(responseAt(*p, 1000.0f));
        REQUIRE(c > 11.0f);
        REQUIRE(c < 13.0f);
    }
}

TEST_CASE("EQ8 the default Q byte is Q = 1.0", "[eq]") {
    // The measured mapping, and the thing that makes it credible: the factory
    // default lands on a round number.
    REQUIRE(std::fabs(eqQFromByte(50) - 1.0f) < 0.001f);
    REQUIRE(eqQFromByte(0) < 0.15f);
    REQUIRE(eqQFromByte(99) > 9.0f);
    REQUIRE(eqQFromByte(25) < eqQFromByte(50));
    REQUIRE(eqQFromByte(75) > eqQFromByte(50));
}

TEST_CASE("EQ9 stereo modes touch only what they should", "[eq]") {
    auto runOne = [](int mode, float inL, float inR, float& outL, float& outR) {
        // A big low shelf so the effect is unmistakable, fed DC-ish low content.
        EqProcessor eq = makeEq(oneBand(1 /*LOWSHELF*/, mode, 1000, 1200, 50));
        outL = inL; outR = inR;
        for (int i = 0; i < 2000; ++i) {
            float l = inL, r = inR;
            eq.process(l, r);
            outL = l; outR = r;
        }
    };

    float l, r;
    SECTION("LEFT leaves the right channel exactly as it was") {
        runOne(3 /*LEFT*/, 0.5f, 0.5f, l, r);
        REQUIRE(r == 0.5f);
        REQUIRE(l != 0.5f);
    }
    SECTION("RIGHT leaves the left channel exactly as it was") {
        runOne(4 /*RIGHT*/, 0.5f, 0.5f, l, r);
        REQUIRE(l == 0.5f);
        REQUIRE(r != 0.5f);
    }
    SECTION("MID moves a centred signal") {
        // Identical channels are pure mid, no side content at all.
        runOne(1 /*MID*/, 0.5f, 0.5f, l, r);
        REQUIRE(l != 0.5f);
        REQUIRE(std::fabs(l - r) < 1e-6f);   // stays centred
    }
    SECTION("SIDE ignores a centred signal") {
        // Pure mid content has no side component, so a side-only band has
        // nothing to work on and the signal must come through untouched.
        runOne(2 /*SIDE*/, 0.5f, 0.5f, l, r);
        REQUIRE(std::fabs(l - 0.5f) < 1e-6f);
        REQUIRE(std::fabs(r - 0.5f) < 1e-6f);
    }
}

TEST_CASE("EQ10 coefficients are recomputed only when the bank changes", "[eq]") {
    EqProcessor eq;
    EqBank bank = oneBand(2, 0, 1000, 1200, 50);
    eq.configure(bank, kSr);
    const float before = toDb(responseAt(eq, 1000.0f));

    // Same bank again: must be a no-op, and must not disturb the response.
    eq.configure(bank, kSr);
    eq.reset();
    const float after = toDb(responseAt(eq, 1000.0f));
    REQUIRE(std::fabs(before - after) < 0.01f);

    // A real change must take effect.
    bank.mid.gain = 0;
    eq.configure(bank, kSr);
    REQUIRE(eq.isBypass());
}

// ---------------------------------------------------------------------------
// Instrument EQ wired into the voice path (EQ_SPEC.md step 4). These need the
// engine, not just the filters.
// ---------------------------------------------------------------------------

#include "support/OfflineHost.h"
#include "io/SongIO.h"
#include <atomic>
#include <fstream>
#include <iterator>
#include <cstdio>

extern std::atomic<int> g_allocCount;

using namespace m8::test;

namespace {

// Render one macrosynth note and return the audio. `bank` is the EQ bank index
// to assign to the instrument -- 0 means none.
std::vector<float> renderWithInstrumentEq(int bank, const EqBank* bankContents) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    state.instruments[0].type = InstType::INST_MACROSYN;
    auto& m = state.instruments[0].macrosyn;
    m.shape = 0x00;
    m.timbre = 0xC0;
    m.color = 0xC0;
    m.amp = 0x20;
    m.lim = 0;
    m.filter_type = 0;
    m.dry = 0xFF;
    m.pan = 0x80;
    m.eq = bank;

    if (bankContents && bank > 0 && bank < kMaxEqBanks)
        state.eqs[bank] = *bankContents;

    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(6000);
    return host.audio();
}

} // namespace

TEST_CASE("EQ11 an unassigned instrument EQ changes nothing", "[eq]") {
    // eq == 0 is "no EQ" on both instrument screens. The factory-default bank
    // is three flat bands, so either way the render must be untouched -- this
    // is what lets EQ ship without altering any existing song.
    const auto none = renderWithInstrumentEq(0, nullptr);
    EqBank flat;                       // factory defaults
    const auto flatBank = renderWithInstrumentEq(4, &flat);

    REQUIRE(none.size() == flatBank.size());
    bool identical = true;
    for (size_t i = 0; i < none.size(); ++i)
        if (none[i] != flatBank[i]) { identical = false; break; }
    REQUIRE(identical);
}

TEST_CASE("EQ12 an assigned instrument EQ shapes that track", "[eq]") {
    EqBank cut;
    // Kill everything below 2 kHz on the low band; the source is a bright saw,
    // so this has to remove real energy.
    cut.low = EqBand{ 0 /*LOWCUT*/, 0, 2000, 0, 70, 0x00 };

    const auto dry = renderWithInstrumentEq(0, nullptr);
    const auto eqd = renderWithInstrumentEq(7, &cut);

    REQUIRE(dry.size() == eqd.size());
    double dryEnergy = 0.0, eqEnergy = 0.0;
    for (size_t i = 0; i < dry.size(); ++i) {
        dryEnergy += double(dry[i]) * dry[i];
        eqEnergy  += double(eqd[i]) * eqd[i];
    }
    REQUIRE(dryEnergy > 0.0);
    REQUIRE(eqEnergy < dryEnergy * 0.9);
}

TEST_CASE("EQ13 instrument EQ allocates nothing on the audio thread", "[eq]") {
    EqBank bank;
    bank.mid = EqBand{ 2 /*BELL*/, 1 /*MID mode*/, 1200, 900, 80, 0x22 };

    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.dry = 0xFF;
    state.instruments[0].macrosyn.pan = 0x80;
    state.instruments[0].macrosyn.eq = 5;
    state.eqs[5] = bank;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(8000);

    REQUIRE(g_allocCount == 0);
}

// ---------------------------------------------------------------------------
// EQ editor screen (EQ_SPEC.md step 6). Input handling and the response
// evaluation the curve is drawn from. Rendering itself is exercised by the
// screen's future .m8script; these cover the logic underneath it.
// ---------------------------------------------------------------------------

#include "ui/screens/eq/EqScreen.h"
#include "ui/UiCommands.h"
#include "engine/CommandRing.h"

using namespace m8::ui;

namespace {

struct TestEqContext {
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};
    EngineState state;
    m8::ui::eq::EqScreenState st;
    bool arrowPressedDuringEdit = false;

    bool send(SDL_Keycode key, bool editHeld) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = key;
        return m8::ui::eq::HandleEqInput(ev, editHeld, arrowPressedDuringEdit, state, st, sink);
    }
};

} // namespace

TEST_CASE("EQ14 the cursor walks the 5x3 grid and stops at the edges", "[eq]") {
    TestEqContext ctx;
    REQUIRE(ctx.st.param == 0);
    REQUIRE(ctx.st.band == 0);

    ctx.send(SDLK_UP, false);          // already at the top
    REQUIRE(ctx.st.param == 0);
    ctx.send(SDLK_LEFT, false);        // already at the left
    REQUIRE(ctx.st.band == 0);

    for (int i = 0; i < 10; ++i) ctx.send(SDLK_DOWN, false);
    REQUIRE(ctx.st.param == m8::ui::eq::P_MODE);   // clamps at the last row

    for (int i = 0; i < 10; ++i) ctx.send(SDLK_RIGHT, false);
    REQUIRE(ctx.st.band == 2);                     // clamps at HIGH
}

TEST_CASE("EQ15 edits reach the engine, not just the mirror", "[eq]") {
    // Every screen must push the same mutation it applies locally
    // (ARCHITECTURE.md invariant 4). If this only wrote the mirror, the ring
    // would be empty and the audio thread would never see the change.
    TestEqContext ctx;
    ctx.st.bank = 3;
    ctx.st.band = 1;          // MID
    ctx.st.param = m8::ui::eq::P_GAIN;

    const int before = ctx.state.eqs[3].mid.gain;
    ctx.send(SDLK_RIGHT, true);

    REQUIRE(ctx.state.eqs[3].mid.gain != before);   // mirror updated
    REQUIRE(ctx.arrowPressedDuringEdit);

    EngineCommand cmd{};
    REQUIRE(ctx.ring.pop(cmd));                     // and a command was queued
    REQUIRE(cmd.type == CommandType::UPDATE_PARAM);
    REQUIRE(cmd.paramId == ParamID::EQ_GAIN);
    REQUIRE(cmd.targetId == 3);
    REQUIRE(cmd.row == 1);
}

TEST_CASE("EQ16 each parameter edits with a sensible step and range", "[eq]") {
    TestEqContext ctx;
    ctx.st.bank = 1;
    ctx.st.band = 0;

    SECTION("gain steps a quarter dB, or a whole dB held vertically") {
        ctx.st.param = m8::ui::eq::P_GAIN;
        ctx.send(SDLK_RIGHT, true);
        REQUIRE(ctx.state.eqs[1].low.gain == 25);
        ctx.send(SDLK_UP, true);
        REQUIRE(ctx.state.eqs[1].low.gain == 125);
    }
    SECTION("Q stays inside 0..99") {
        ctx.st.param = m8::ui::eq::P_Q;
        for (int i = 0; i < 200; ++i) ctx.send(SDLK_UP, true);
        REQUIRE(ctx.state.eqs[1].low.q == 99);
        for (int i = 0; i < 200; ++i) ctx.send(SDLK_DOWN, true);
        REQUIRE(ctx.state.eqs[1].low.q == 0);
    }
    SECTION("type wraps through all seven") {
        ctx.st.param = m8::ui::eq::P_TYPE;
        const int start = ctx.state.eqs[1].low.type;
        for (int i = 0; i < 7; ++i) ctx.send(SDLK_RIGHT, true);
        REQUIRE(ctx.state.eqs[1].low.type == start);   // full cycle
        ctx.send(SDLK_RIGHT, true);
        REQUIRE(ctx.state.eqs[1].low.type != start);
    }
    SECTION("mode wraps through all five") {
        ctx.st.param = m8::ui::eq::P_MODE;
        for (int i = 0; i < 5; ++i) ctx.send(SDLK_RIGHT, true);
        REQUIRE(ctx.state.eqs[1].low.mode == 0);
    }
}

TEST_CASE("EQ17 OPTION asks to close the editor", "[eq]") {
    TestEqContext ctx;
    REQUIRE(ctx.send(SDLK_Z, false));       // Z is OPTION
    REQUIRE_FALSE(ctx.send(SDLK_DOWN, false));
}

TEST_CASE("EQ18 the drawn curve follows the bands", "[eq]") {
    // The editor plots responseDbAt() per column, so that function is what the
    // picture is. A bell at 1 kHz must read as a peak there and as nothing an
    // octave-and-a-half away.
    EqBank bank;
    bank.mid = EqBand{ 2 /*BELL*/, 0, 1000, 1200, 60, 0x02 };
    EqProcessor eq;
    eq.configure(bank, 48000.0f);

    const float atPeak = eq.responseDbAt(1000.0f, 48000.0f);
    const float wayOff = eq.responseDbAt(60.0f, 48000.0f);
    REQUIRE(atPeak > 11.0f);
    REQUIRE(atPeak < 13.0f);
    REQUIRE(std::fabs(wayOff) < 1.0f);

    // A flat bank must plot as a flat line, or the editor would show a curve
    // for an EQ that does nothing.
    EqProcessor flat;
    flat.configure(EqBank{}, 48000.0f);
    REQUIRE(flat.responseDbAt(100.0f, 48000.0f) == 0.0f);
    REQUIRE(flat.responseDbAt(5000.0f, 48000.0f) == 0.0f);
}

// ---------------------------------------------------------------------------
// The four EQs the file library doesn't model (EQ_SPEC.md step 8): main mix
// plus ModFX/Delay/Reverb, living past the instrument banks.
// ---------------------------------------------------------------------------

TEST_CASE("EQ19 the bus EQs load from their own part of the file", "[eq]") {
    auto result = m8::io::loadSong(
        std::string(THIRD_PARTY_DIR) + "/m8-files-cxx/examples/songs/V4-1EMPTY.m8s", "");
    REQUIRE(result.ok);

    // Factory defaults for the four, read off real files (EQ_SPEC.md §4c). They
    // differ from each other, which is what identifies which block is which.
    REQUIRE(result.state.eqs[kEqMix].low.type == 1);      // LOWSHELF 100
    REQUIRE(result.state.eqs[kEqMix].low.freq == 100);
    REQUIRE(result.state.eqs[kEqModFx].low.type == 0);    // LOWCUT 100
    REQUIRE(result.state.eqs[kEqModFx].high.freq == 5000);
    REQUIRE(result.state.eqs[kEqDelay].low.freq == 500);  // LOWCUT 500 / HI.CUT 10k
    REQUIRE(result.state.eqs[kEqDelay].high.freq == 10000);
    REQUIRE(result.state.eqs[kEqReverb].low.freq == 200); // LOWCUT 200 / HI.CUT 8.8k
    REQUIRE(result.state.eqs[kEqReverb].high.freq == 8800);
}

TEST_CASE("EQ20 an edited mix EQ survives save and reload", "[eq]") {
    const std::string src = std::string(THIRD_PARTY_DIR)
                          + "/m8-files-cxx/examples/songs/V4-1EMPTY.m8s";
    auto result = m8::io::loadSong(src, "");
    REQUIRE(result.ok);

    // The same distinctive values the hardware experiment used, so a failure
    // here is directly comparable to the bytes recorded in the spec.
    auto edited = result.state;
    edited.eqs[kEqMix].low = EqBand{ 1, 0, 137, 1200, 3, 0x01 };
    edited.eqs[kEqDelay].mid = EqBand{ 3, 2, 4353, -900, 7, 0x42 };

    std::string err;
    REQUIRE(m8::io::saveSong("buseq_rt.m8s", result, result.sequencer, edited, err));
    auto again = m8::io::loadSong("buseq_rt.m8s", "");
    REQUIRE(again.ok);

    REQUIRE(again.state.eqs[kEqMix].low.freq == 137);
    REQUIRE(again.state.eqs[kEqMix].low.gain == 1200);
    REQUIRE(again.state.eqs[kEqMix].low.q == 3);
    REQUIRE(again.state.eqs[kEqDelay].mid.type == 3);
    REQUIRE(again.state.eqs[kEqDelay].mid.mode == 2);
    REQUIRE(again.state.eqs[kEqDelay].mid.gain == -900);
    std::remove("buseq_rt.m8s");
}

TEST_CASE("EQ21 untouched bus EQs round-trip byte-identically", "[eq]") {
    // These bytes are outside everything the file library knows about, so if
    // the patch-in is wrong it corrupts a region nothing else would catch.
    const std::string src = std::string(THIRD_PARTY_DIR)
                          + "/m8-files-cxx/examples/songs/V4-1EMPTY.m8s";
    auto result = m8::io::loadSong(src, "");
    REQUIRE(result.ok);

    std::string err;
    REQUIRE(m8::io::saveSong("buseq_id.m8s", result, result.sequencer, result.state, err));

    std::ifstream a(src, std::ios::binary), b("buseq_id.m8s", std::ios::binary);
    std::vector<char> av((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    std::vector<char> bv((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    a.close(); b.close();
    REQUIRE(av.size() == bv.size());

    size_t diffs = 0;
    for (size_t i = 0; i < av.size(); ++i) if (av[i] != bv[i]) ++diffs;
    REQUIRE(diffs == 0);
    std::remove("buseq_id.m8s");
}

TEST_CASE("EQ22 the mix EQ is applied to the master bus", "[eq]") {
    auto render = [](bool withEq) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        state.instruments[0].macrosyn.dry = 0xFF;
        state.instruments[0].macrosyn.pan = 0x80;
        state.instruments[0].macrosyn.amp = 0x20;
        if (withEq)  // cut everything below 2 kHz on the whole mix
            state.eqs[kEqMix].low = EqBand{ 0 /*LOWCUT*/, 0, 2000, 0, 70, 0x00 };

        setStep(host.sequencer(), 0, 0, 60, 127, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(6000);
        double energy = 0.0;
        for (float v : host.audio()) energy += double(v) * v;
        return energy;
    };

    const double dry = render(false);
    const double cut = render(true);
    REQUIRE(dry > 0.0);
    REQUIRE(cut < dry * 0.9);
}
