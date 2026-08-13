// Master bus stages and mixer meters (MIXER_SPEC.md §4, §5.2).
//
// The DSP curves here are not hardware-verified -- no capture exists for the
// M8's limiter knee, DJ filter sweep or OTT ratios -- so these tests pin
// *wiring and semantics*, which are documented, rather than exact output:
//
//   MB1  DJF is off at 0x80 and sweeps both ways from there
//   MB2  LIM does nothing at 00 and reduces peaks above it
//   MB3  OTT does nothing at 00 and changes the mix above it
//   MB4  meters report level and decay to silence
//   MB5  none of the above allocates on the audio thread

#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

namespace {

// A loud, harmonically rich note so filters and compressors have something to
// bite on. Returns peak and total energy of the render.
struct Rendered {
    float peak = 0.0f;
    float energy = 0.0f;
};

Rendered renderWithMixer(void (*tweak)(MixerState&), int frames = 6000) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    state.instruments[0].type = InstType::INST_MACROSYN;
    auto& m = state.instruments[0].macrosyn;
    m.shape = 0x00;
    m.timbre = 0xC0;
    m.color = 0xC0;
    m.amp = 0x40;
    m.lim = 0;
    m.filter_type = 0;
    m.dry = 0xFF;
    m.pan = 0x80;

    // Neutral master by default; each case overrides what it is testing.
    state.mixer.mix_vol = 0xFF;
    state.mixer.out_vol = 0xFF;
    state.mixer.lim_val = 0x00;
    state.mixer.djf_freq = 0x80;
    state.mixer.ott = 0x00;
    if (tweak) tweak(state.mixer);

    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(frames);

    Rendered r;
    for (float v : host.audio()) {
        const float a = std::fabs(v);
        if (a > r.peak) r.peak = a;
        r.energy += a;
    }
    return r;
}

} // namespace

TEST_CASE("MB1 DJ filter is off at 0x80 and sweeps both ways", "[mixer]") {
    const Rendered off  = renderWithMixer([](MixerState& mx) { mx.djf_freq = 0x80; });
    const Rendered low  = renderWithMixer([](MixerState& mx) { mx.djf_freq = 0x20; });
    const Rendered high = renderWithMixer([](MixerState& mx) { mx.djf_freq = 0xE0; });

    REQUIRE(off.energy > 0.0f);
    // Well below 0x80 is a low-pass with a low cutoff, well above is a
    // high-pass with a high one. Either way most of a bright saw goes away.
    REQUIRE(low.energy < off.energy);
    REQUIRE(high.energy < off.energy);
    // And the two halves are not the same filter.
    REQUIRE(low.energy != high.energy);
}

TEST_CASE("MB2 limiter is off at 00 and pulls peaks down above it", "[mixer]") {
    const Rendered off  = renderWithMixer([](MixerState& mx) { mx.lim_val = 0x00; });
    const Rendered hard = renderWithMixer([](MixerState& mx) { mx.lim_val = 0xFF; });

    REQUIRE(off.peak > 0.0f);
    REQUIRE(hard.peak < off.peak);
}

TEST_CASE("MB3 OTT is off at 00 and changes the mix above it", "[mixer]") {
    const Rendered off = renderWithMixer([](MixerState& mx) { mx.ott = 0x00; });
    const Rendered on  = renderWithMixer([](MixerState& mx) { mx.ott = 0xC0; });

    REQUIRE(off.energy > 0.0f);
    REQUIRE(on.energy != off.energy);
}

TEST_CASE("MB4 meters report level and decay back to silence", "[mixer]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.dry = 0xFF;
    state.instruments[0].macrosyn.pan = 0x80;
    state.instruments[0].macrosyn.amp = 0x40;
    state.mixer.mix_vol = 0xFF;
    state.mixer.out_vol = 0xFF;

    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(4000);

    const auto playing = host.engine().getTrackLevel(0);
    const auto master  = host.engine().getMasterLevel();
    REQUIRE(playing.peakL > 0);
    REQUIRE(master.peakL > 0);

    // Track 7 is silent throughout and must read as such.
    REQUIRE(host.engine().getTrackLevel(7).peakL == 0);

    // Stop, then render long enough for the held peak to decay away.
    host.push(stop());
    host.render(20000);
    const auto after = host.engine().getTrackLevel(0);
    REQUIRE(after.peakL < playing.peakL);
    REQUIRE(after.peakL <= 2);
}

TEST_CASE("MB5 master bus stages allocate nothing", "[mixer]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.dry = 0xFF;
    state.instruments[0].macrosyn.pan = 0x80;
    // Everything engaged at once.
    state.mixer.lim_val = 0x80;
    state.mixer.djf_freq = 0x30;
    state.mixer.ott = 0xA0;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(8000);

    REQUIRE(g_allocCount == 0);
}
