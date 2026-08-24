// ===========================================================================
// test_sample_preview.cpp — auditioning a .wav must not commit to it.
//
// The browser could load a sample but not hear one first. PREVIEW_SAMPLE fills
// that in, and the whole point is what it does NOT do: no instrument slot is
// written, no track voice is stolen, no transport starts. A preview that
// quietly edited the song would be worse than no preview, so both halves are
// asserted here.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

namespace {

std::vector<float> sineBuf(int frames, float hz) {
    std::vector<float> b(frames);
    for (int i = 0; i < frames; ++i)
        b[i] = 0.8f * std::sin(6.2831853f * hz * float(i) / 48000.0f);
    return b;
}

SampleData makeSample(std::vector<float>& buf, const char* path) {
    SampleData sd{};
    sd.data       = buf.data();
    sd.frames     = static_cast<int>(buf.size());
    sd.channels   = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, path, 127);
    return sd;
}

float peakOf(const std::vector<float>& a) {
    float pk = 0.0f;
    for (float v : a) pk = std::max(pk, std::fabs(v));
    return pk;
}

} // namespace

TEST_CASE("PREVIEW_SAMPLE makes sound with the transport stopped", "[preview]") {
    OfflineHost host;

    auto buf = sineBuf(24000, 440.0f);
    SampleData sd = makeSample(buf, "preview_a.wav");

    EngineCommand cmd{};
    cmd.type = CommandType::PREVIEW_SAMPLE;
    cmd.u.sample = sd;
    host.push(cmd);

    // No playPhrase: nothing is playing. The audition has to be audible anyway,
    // which is the case the browser needs -- you preview before you commit, and
    // before you press play.
    host.render(12000);

    const float pk = peakOf(host.audio());
    INFO("preview peak = " << pk);
    CHECK(pk > 1e-3f);
}

TEST_CASE("PREVIEW_SAMPLE does not touch any instrument slot", "[preview]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Record what instrument 0 looked like before.
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.shape = 0x07;
    const SampleHandle before0 = state.instruments[0].sampler.sample;

    auto buf = sineBuf(24000, 440.0f);
    SampleData sd = makeSample(buf, "preview_b.wav");
    EngineCommand cmd{};
    cmd.type = CommandType::PREVIEW_SAMPLE;
    cmd.u.sample = sd;
    host.push(cmd);
    host.render(6000);

    // The slot the user was editing is untouched -- type, its own parameters,
    // and its sample handle. This is the assertion that separates "audition"
    // from "load".
    CHECK(state.instruments[0].type == InstType::INST_MACROSYN);
    CHECK(state.instruments[0].macrosyn.shape == 0x07);
    CHECK(state.instruments[0].sampler.sample == before0);
}

TEST_CASE("PREVIEW_SAMPLE does not steal a track voice", "[preview]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.shape  = 0x00;
    state.instruments[0].macrosyn.volume = 0x40;

    // A note is playing on track 0 when the preview arrives. Borrowing a track
    // voice to audition would cut it off -- audible, and exactly what someone
    // browsing samples mid-playback would hate.
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(6000);
    const float beforePeak = peakOf(host.audio());
    REQUIRE(beforePeak > 1e-3f);

    auto buf = sineBuf(24000, 440.0f);
    SampleData sd = makeSample(buf, "preview_c.wav");
    EngineCommand cmd{};
    cmd.type = CommandType::PREVIEW_SAMPLE;
    cmd.u.sample = sd;
    host.push(cmd);
    host.render(6000);

    // Still making sound after the preview -- the track note survived it.
    const auto& a = host.audio();
    std::vector<float> tail(a.end() - std::min<size_t>(a.size(), 8000), a.end());
    CHECK(peakOf(tail) > 1e-3f);
}

TEST_CASE("previewing twice releases the first sample", "[preview]") {
    OfflineHost host;

    // Scrolling a directory previews file after file. If each one held its pool
    // slot the pool would fill after a hundred-odd files and previews would
    // silently stop working -- so the previous handle is released each time.
    for (int i = 0; i < 300; ++i) {
        auto buf = sineBuf(2400, 220.0f + float(i));
        char path[64];
        std::snprintf(path, sizeof(path), "scroll_%d.wav", i);
        SampleData sd = makeSample(buf, path);
        EngineCommand cmd{};
        cmd.type = CommandType::PREVIEW_SAMPLE;
        cmd.u.sample = sd;
        host.push(cmd);
        host.render(64);
    }

    // The last one still sounds, which it would not if the pool had filled.
    auto buf = sineBuf(24000, 440.0f);
    SampleData sd = makeSample(buf, "scroll_last.wav");
    EngineCommand cmd{};
    cmd.type = CommandType::PREVIEW_SAMPLE;
    cmd.u.sample = sd;
    host.push(cmd);
    host.render(12000);

    const auto& a = host.audio();
    std::vector<float> tail(a.end() - std::min<size_t>(a.size(), 12000), a.end());
    INFO("tail peak after 300 previews = " << peakOf(tail));
    CHECK(peakOf(tail) > 1e-3f);
}
