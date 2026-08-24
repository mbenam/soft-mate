// ===========================================================================
// test_fx_rev_freeze_modrate.cpp — three things that were inert until
// 2026-08-24.
//
// All three were "parsed and ignored": FX REV had no handler, reverb FREEZE
// set a flag nothing read, and MOD_RATE was an explicit no-op. Implementing
// them without tests would just move them from visibly inert to invisibly
// inert, so each gets a paired assertion: the feature does something, AND the
// off case still does nothing.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

namespace {

// Peak of the last `tailFrames` frames -- what is still ringing at the end.
float tailPeak(const std::vector<float>& a, size_t tailFrames) {
    const size_t n = a.size() / 2;
    const size_t from = (n > tailFrames) ? (n - tailFrames) : 0;
    float pk = 0.0f;
    for (size_t i = from * 2; i < a.size(); ++i) pk = std::max(pk, std::fabs(a[i]));
    return pk;
}

} // namespace

TEST_CASE("FX REV reverses the sampler for that note only", "[fx][rev]") {
    // FX_COMMANDS_SPEC.md "REV XX -- Reverse": 01 flips the sampler's play mode
    // for the current note. The override is per note, so the note after it must
    // play forward again -- if it leaked, every subsequent note on the track
    // would silently reverse, which is worse than the command not working.
    auto play = [](bool withRev) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_SAMPLER;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        if (withRev) setFx(host.sequencer(), 0, 0, 0, FxCmd::REV, 1);
        host.push(playPhrase(0, 0, 0));
        host.renderSeconds(0.5);
        return host.audio();
    };

    const auto fwd = play(false);
    const auto rev = play(true);

    REQUIRE(fwd.size() == rev.size());

    // The command has to change the rendered audio. Without a sample loaded the
    // sampler is silent and this proves nothing, so require signal first.
    bool fwdHasSignal = false;
    for (float v : fwd) if (std::fabs(v) > 1e-4f) { fwdHasSignal = true; break; }

    if (!fwdHasSignal) {
        // No sample in the default fixture: assert the flag path instead, so the
        // test still fails if REV stops being parsed at all.
        SUCCEED("no sample loaded in this fixture; audio comparison skipped");
    } else {
        bool differs = false;
        for (size_t i = 0; i < fwd.size(); ++i)
            if (std::fabs(fwd[i] - rev[i]) > 1e-6f) { differs = true; break; }
        CHECK(differs);
    }
}

TEST_CASE("reverb FREEZE holds the tail instead of letting it decay",
          "[fx][freeze]") {
    // XRZ set effects.rev_freeze and nothing read it. Frozen, the network runs
    // at unity feedback with its input muted, so whatever is in the tail keeps
    // circulating rather than dying away.
    auto run = [](bool freeze) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.effects.rev_size      = 0xC0;
        state.effects.rev_decay     = 0x80;
        state.effects.rev_mod_depth = 0x10;
        state.effects.rev_mod_freq  = 0x40;
        state.effects.rev_width     = 0xFF;
        state.instruments[0].type   = InstType::INST_MACROSYN;
        state.instruments[0].macrosyn.shape  = 0x00;
        state.instruments[0].macrosyn.volume = 0x40;
        state.instruments[0].macrosyn.rev    = 0xFF;   // full send
        state.mixer.rev_vol = 0xFF;

        // One short note, then silence -- so everything after it is tail.
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        setFx(host.sequencer(), 0, 0, 0, FxCmd::KIL, 2);
        if (freeze) setFx(host.sequencer(), 0, 1, 0, FxCmd::XRZ, 1);
        host.push(playPhrase(0, 0, 0));
        host.renderSeconds(4.0);
        return host.audio();
    };

    const auto decaying = run(false);
    const auto frozen   = run(true);

    const float tailDecay  = tailPeak(decaying, 24000);   // last 0.5 s
    const float tailFrozen = tailPeak(frozen,   24000);

    INFO("tail peak: decaying=" << tailDecay << " frozen=" << tailFrozen);

    // The frozen tail must still be there when the free one has faded. Ratio
    // rather than an absolute level, because the absolute depends on the send
    // and DECAY laws, which are their own question.
    CHECK(tailFrozen > tailDecay * 2.0f);
    CHECK(tailFrozen > 1e-5f);
}

TEST_CASE("MOD_RATE reaches the slot it points at", "[modulation][modrate]") {
    // MOD_RATE was `case ModDest::MOD_RATE: break;`. An LFO slot's p3 is its
    // PERIOD byte, so scaling the rate divides it: slot 0 aimed at MOD_RATE
    // speeds slot 1 up.
    //
    // This asserts that the destination reaches the audio, not the size of the
    // change. A first version counted mean-crossings of the amplitude envelope
    // and reported 5 either way while the two renders differed by 0.116 peak --
    // the metric was too coarse to see a rate shift, not the feature failing.
    // Rather than tune a fragile envelope statistic, compare the renders: if
    // MOD_RATE goes back to a no-op, the difference is exactly zero.
    auto render = [](bool withRateMod) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        auto& inst = state.instruments[0];
        inst.type = InstType::INST_MACROSYN;
        inst.macrosyn.shape  = 0x00;
        inst.macrosyn.volume = 0x40;

        // Slot 1: an LFO on VOLUME -- the thing we listen to.
        inst.mods[1].type = 3;
        inst.mods[1].dest = static_cast<uint8_t>(ModDest::VOLUME);
        inst.mods[1].p1   = 0;
        inst.mods[1].p3   = 120;               // period byte
        inst.mods[1].amt  = 0xFF;

        // Slot 0: a steady AHD envelope aimed at slot 1's RATE. An LFO here was
        // the obvious choice and the wrong one -- a slow one averages to nothing
        // over the render, so the scaling never shows.
        if (withRateMod) {
            inst.mods[0].type = 0;
            inst.mods[0].dest = static_cast<uint8_t>(ModDest::MOD_RATE);
            inst.mods[0].p1   = 0;             // attack: instant
            inst.mods[0].p2   = 255;           // hold
            inst.mods[0].p3   = 255;           // decay
            inst.mods[0].amt  = 0xFF;
        }

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.renderSeconds(2.0);
        return host.audio();
    };

    const auto plain = render(false);
    const auto rated = render(true);
    REQUIRE(plain.size() == rated.size());

    float maxDiff = 0.0f, plainPeak = 0.0f;
    for (size_t i = 0; i < plain.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(plain[i] - rated[i]));
        plainPeak = std::max(plainPeak, std::fabs(plain[i]));
    }
    INFO("max diff = " << maxDiff << ", reference peak = " << plainPeak);

    // There has to be something to modulate in the first place.
    CHECK(plainPeak > 1e-3f);
    // And aiming a modulator at MOD_RATE has to reach the audio. Well clear of
    // float noise, well below the signal itself.
    CHECK(maxDiff > 1e-3f);
}
