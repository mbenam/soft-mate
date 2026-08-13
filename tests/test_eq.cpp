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
