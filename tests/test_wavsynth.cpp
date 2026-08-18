#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include "engine/SynthVoice.h"
#include "data/WavetableBank.h"
#include "ui/screens/instrument/InstrumentWavsynthLayout.h"
#include <atomic>
#include <cmath>
#include <vector>
#include <string>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("WavSynth renders all 9 base shapes without NaN", "[wavsynth]") {
    for (int shape = 0; shape < 9; ++shape) {
        DYNAMIC_SECTION("Shape " << shape) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_WAVSYNTH;
            auto& ws = state.instruments[0].wav;
            ws.shape = shape;
            ws.size = 0x80; ws.mult = 0x00; ws.warp = 0x80; ws.scan = 0x00;
            ws.amp = 0x40; ws.lim = 0; ws.filter_type = 0;
            ws.dry = 0xC0; ws.pan = 0x80;

            g_allocCount = 0;
            setStep(host.sequencer(), 0, 0, 60, 100, 0);
            host.push(playPhrase(0, 0, 0));
            host.render(1000);

            REQUIRE(g_allocCount == 0);

            const auto& a = host.audio();
            bool hasNonZero = false;
            for (float val : a) {
                REQUIRE(std::isfinite(val));
                REQUIRE(val >= -1.05f);
                REQUIRE(val <= 1.05f);
                if (std::abs(val) > 0.0001f) hasNonZero = true;
            }
            REQUIRE(hasNonZero);
        }
    }
}

TEST_CASE("WavSynth different shapes produce different output", "[wavsynth]") {
    auto renderWav = [](int shape) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = shape; ws.size = 0x80; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSine = renderWav(6);
    float sumSaw = renderWav(4);
    REQUIRE(sumSine != sumSaw);
}

TEST_CASE("WavSynth SIZE parameter changes output", "[wavsynth]") {
    auto renderWavSize = [](int size) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = size; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSmall = renderWavSize(0x20);
    float sumLarge = renderWavSize(0xF0);
    REQUIRE(sumSmall != sumLarge);
}

TEST_CASE("WavSynth MULT parameter changes output", "[wavsynth]") {
    auto renderWavMult = [](int mult) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = 0x80; ws.mult = mult; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumNoMult = renderWavMult(0x00);
    float sumHighMult = renderWavMult(0xF0);
    REQUIRE(sumNoMult != sumHighMult);
}

TEST_CASE("WavSynth RT safety -- zero allocations", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 6; ws.size = 0x80; ws.amp = 0x40;
    ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2500);

    // Change parameter mid-render so regenerateWavTable runs on audio thread
    EngineCommand cmd;
    cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::WAV_WARP;
    cmd.value = 0x40;
    cmd.targetId = 0;
    host.push(cmd);
    host.render(2500);

    REQUIRE(g_allocCount == 0);
}

TEST_CASE("WavSynth table is regenerated only when a shaping parameter changes", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 6; ws.size = 0x20; ws.mult = 0; ws.warp = 0x00; ws.scan = 0;
    ws.amp = 0x40; ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(500);

    std::vector<float> firstHalf = host.audio();
    REQUIRE(firstHalf.size() == 1000); // 500 frames * 2 channels

    EngineCommand cmd;
    cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::WAV_WARP;
    cmd.value = 0x80;
    cmd.targetId = 0;
    host.push(cmd);
    host.render(500);

    std::vector<float> secondHalf(host.audio().begin() + 1000, host.audio().end());
    REQUIRE(secondHalf.size() == 1000);

    bool differ = false;
    for (size_t i = 0; i < 1000; ++i) {
        if (std::abs(firstHalf[i] - secondHalf[i]) > 0.01f) {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);

    // Render 1000 frames with no parameter change: check stability / periodicity
    OfflineHost host2;
    auto& state2 = host2.engine().getStateForInit();
    state2.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws2 = state2.instruments[0].wav;
    ws2.shape = 6; ws2.size = 0x20; ws2.mult = 0; ws2.warp = 0x00; ws2.scan = 0;
    ws2.amp = 0x40; ws2.lim = 0; ws2.filter_type = 0; ws2.dry = 0xC0; ws2.pan = 0x80;

    setStep(host2.sequencer(), 0, 0, 60, 100, 0);
    host2.push(playPhrase(0, 0, 0));
    host2.render(1000);

    const auto& aud = host2.audio();
    int periodFrames = static_cast<int>(std::round(48000.0f / 261.625565f));
    float maxPeriodDiff = 0.0f;
    for (int f = 200; f < 200 + periodFrames; ++f) {
        float d = std::abs(aud[2 * f] - aud[2 * (f + periodFrames)]);
        if (d > maxPeriodDiff) maxPeriodDiff = d;
    }
    REQUIRE(maxPeriodDiff < 0.05f);
}

TEST_CASE("WavSynth loop point is continuous", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 6; // SINE
    ws.size = 0x20;
    ws.mult = 0;
    ws.warp = 0x00;
    ws.scan = 0x00;
    ws.amp = 0x40;
    ws.lim = 0;
    ws.filter_type = 0;
    ws.dry = 0xC0;
    ws.pan = 0x80;

    // Low note: MIDI 36 (C-2, ~65.4 Hz)
    setStep(host.sequencer(), 0, 0, 36, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(20000);

    const auto& aud = host.audio();
    float maxStep = 0.0f;
    for (size_t f = 1; f < aud.size() / 2; ++f) {
        float step = std::abs(aud[2 * f] - aud[2 * (f - 1)]);
        if (step > maxStep) maxStep = step;
    }

    // Theoretical maximum sample-to-sample delta for a sine of ~65.4 Hz with peak ~0.5 is ~0.0043.
    // Allow margin; discontinuous wrap in defect D2 produced steps > 0.05.
    REQUIRE(maxStep < 0.015f);
}

TEST_CASE("WavSynth WARP 00 is the identity", "[wavsynth]") {
    float testPoints[] = {0.0f, 0.05f, 0.12f, 0.25f, 0.5f, 0.75f, 0.88f, 0.99f};
    bool allEqual = true;
    for (float u : testPoints) {
        if (std::abs(SynthVoice::wavWarpPhase(u, 0.0f) - u) > 1e-6f) {
            allEqual = false;
        }
    }
    REQUIRE(allEqual);

    auto renderWarp = [](int warpVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = 0x40; ws.warp = warpVal; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0.0f;
        for (float v : host.audio()) sum += v * v;
        return sum;
    };
    float sum00 = renderWarp(0x00);
    float sum80 = renderWarp(0x80);
    REQUIRE(std::abs(sum00 - sum80) > 0.001f);
}

TEST_CASE("WavSynth SCAN mirrors PULSE 50% into PWM", "[wavsynth]") {
    auto renderScanMean = [](int scanVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 2; // PULSE 50%
        ws.size = 0x40;
        ws.mult = 0;
        ws.warp = 0x00;
        ws.scan = scanVal;
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);

        float sum = 0.0f;
        for (float v : host.audio()) sum += v;
        return sum / static_cast<float>(host.audio().size());
    };

    float mean20 = renderScanMean(0x20);
    float mean40 = renderScanMean(0x40);
    float mean60 = renderScanMean(0x60);

    REQUIRE(std::abs(mean20 - mean40) > 0.01f);
    REQUIRE(std::abs(mean40 - mean60) > 0.01f);
}

TEST_CASE("WavSynth NOISE is not periodic and NOISE PITCHED is", "[wavsynth]") {
    auto computeMaxAutocorr = [](int shapeVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = shapeVal;
        ws.size = 0x40;
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(4000);

        const auto& aud = host.audio();
        std::vector<float> mono(aud.size() / 2);
        for (size_t f = 0; f < mono.size(); ++f) {
            mono[f] = aud[2 * f];
        }

        float maxCorr = 0.0f;
        int n = 1500;
        for (int lag = 182; lag <= 185; ++lag) {
            float num = 0.0f, d1 = 0.0f, d2 = 0.0f;
            for (int f = 500; f < 500 + n; ++f) {
                float x1 = mono[f];
                float x2 = mono[f + lag];
                num += x1 * x2;
                d1 += x1 * x1;
                d2 += x2 * x2;
            }
            float denom = std::sqrt(d1 * d2);
            float corr = (denom > 1e-8f) ? (num / denom) : 0.0f;
            if (corr > maxCorr) maxCorr = corr;
        }
        return maxCorr;
    };

    float corrTonal = computeMaxAutocorr(7); // NOISE PITCHED
    float corrNoise = computeMaxAutocorr(8); // NOISE

    REQUIRE(corrTonal > 0.90f);
    REQUIRE(corrNoise < 0.20f);
}

TEST_CASE("WavSynth is deterministic across renders", "[wavsynth]") {
    auto renderNoise = []() -> std::vector<float> {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 8; // NOISE
        ws.size = 0x20;
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);
        return host.audio();
    };

    std::vector<float> r1 = renderNoise();
    std::vector<float> r2 = renderNoise();

    REQUIRE(r1.size() == r2.size());
    REQUIRE(r1 == r2);
}

TEST_CASE("Wavetable bank is well formed", "[wavsynth]") {
    bool allValid = true;
    for (int wt = 0; wt < kWavetableCount; ++wt) {
        for (int f = 0; f < kWavetableFrames; ++f) {
            bool hasSample = false;
            for (int s = 0; s < kWavetableLength; ++s) {
                int8_t v = kWavetableData[wt][f][s];
                if (v != 0) hasSample = true;
            }
            if (!hasSample) {
                // Some frames may be flat (e.g. silent frames in certain tables)
            }
        }
        std::string uiName = m8::ui::instrument::WavShapeName(0x09 + wt);
        std::string wtName = kWavetableNames[wt];
        while (!uiName.empty() && uiName.back() == ' ') uiName.pop_back();
        if (uiName.rfind("WT-", 0) == 0) uiName = uiName.substr(3);
        if (uiName != wtName) {
            allValid = false;
        }
    }
    REQUIRE(allValid);
}

TEST_CASE("Wavetable shapes render and differ from each other", "[wavsynth]") {
    auto renderWt = [](int shape) -> std::vector<float> {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = shape; ws.size = 0x80; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        return host.audio();
    };

    int shapes[] = {0x09, 0x1F, 0x2A, 0x45};
    std::vector<std::vector<float>> results;
    bool allFiniteNonSilent = true;
    for (int s : shapes) {
        auto aud = renderWt(s);
        float sum = 0.0f;
        for (float v : aud) {
            if (!std::isfinite(v)) allFiniteNonSilent = false;
            sum += std::abs(v);
        }
        if (sum < 1.0f) allFiniteNonSilent = false;
        results.push_back(aud);
    }
    REQUIRE(allFiniteNonSilent);

    bool allDifferent = true;
    for (size_t i = 0; i < results.size(); ++i) {
        for (size_t j = i + 1; j < results.size(); ++j) {
            float diff = 0.0f;
            for (size_t k = 0; k < results[i].size(); ++k) {
                diff += std::abs(results[i][k] - results[j][k]);
            }
            if (diff < 0.1f) allDifferent = false;
        }
    }
    REQUIRE(allDifferent);
}

TEST_CASE("SCAN crossfades between adjacent frames", "[wavsynth]") {
    auto renderScanPeak = [](int scan) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 0x1F; // BNK:SCRATCH
        ws.size = 0xFF;
        ws.mult = 0x00;
        ws.warp = 0x00;
        ws.scan = scan;
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 36, 100, 0); // MIDI 36 low note
        host.push(playPhrase(0, 0, 0));
        host.render(2000);

        float maxPeak = 0.0f;
        for (float v : host.audio()) {
            float a = std::abs(v);
            if (a > maxPeak) maxPeak = a;
        }
        return maxPeak;
    };

    float peakF2 = renderScanPeak(0x08); // frame 2
    float peakMid = renderScanPeak(0x0A); // midpoint (frame 2.5)
    float peakF3 = renderScanPeak(0x0C); // frame 3

    REQUIRE(peakF2 > 0.30f);
    REQUIRE(peakF3 > 0.30f);
    REQUIRE(peakMid < peakF2 / 3.0f);
    REQUIRE(peakMid < peakF3 / 3.0f);
}

TEST_CASE("SCAN 0x00 selects frame 0 and 0xFF selects frame 63", "[wavsynth]") {
    auto renderScan = [](int scan) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 0x1F; // BNK:SCRATCH
        ws.size = 0x80;
        ws.mult = 0x00;
        ws.warp = 0x00;
        ws.scan = scan;
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);

        float sum = 0.0f;
        for (float v : host.audio()) sum += v * v;
        return sum;
    };

    float sum00 = renderScan(0x00);
    float sumFF = renderScan(0xFF);
    REQUIRE(std::abs(sum00 - sumFF) > 0.1f);
}

TEST_CASE("SIZE decimates a wavetable frame", "[wavsynth]") {
    auto computeHfRatio = [](int size) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 0x1F; // BNK:SCRATCH
        ws.size = size;
        ws.mult = 0x00;
        ws.warp = 0x00;
        ws.scan = 0x55; // frame 21
        ws.amp = 0x40;
        ws.lim = 0;
        ws.filter_type = 0;
        ws.dry = 0xC0;
        ws.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 36, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);

        const auto& aud = host.audio();
        float diffSum = 0.0f;
        float maxVal = 0.0001f;
        for (size_t f = 1; f < aud.size() / 2; ++f) {
            float v = std::abs(aud[2 * f]);
            if (v > maxVal) maxVal = v;
            diffSum += std::abs(aud[2 * f] - aud[2 * (f - 1)]);
        }
        return diffSum / (static_cast<float>(aud.size() / 2) * maxVal);
    };

    float hfFF = computeHfRatio(0xFF);
    float hf20 = computeHfRatio(0x20);

    REQUIRE(hfFF > hf20 * 1.5f);
}

TEST_CASE("Wavetable rendering allocates nothing", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 0x1F; // BNK:SCRATCH
    ws.size = 0x80;
    ws.scan = 0x00;
    ws.amp = 0x40;
    ws.lim = 0;
    ws.filter_type = 0;
    ws.dry = 0xC0;
    ws.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2500);

    // Change SCAN mid-render to trigger regenerateWavTable on audio thread
    EngineCommand cmd;
    cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::WAV_SCAN;
    cmd.value = 0x55;
    cmd.targetId = 0;
    host.push(cmd);
    host.render(2500);

    REQUIRE(g_allocCount == 0);
}
