// ===========================================================================
// m8_watchcapture — record the M8's USB audio while watching its screen, and
// abort the moment the capture stops being the one you asked for.
//
// Why this exists
// ---------------
// hw_measure.py's docstring states the rule this tool implements: "a
// measurement harness must re-read every field its result depends on,
// immediately before AND after the capture, and refuse the capture if either
// read disagrees." Before-and-after is what you do when you cannot look
// during, and until now nothing could:
//
//   - m8_capture speaks serial but does not decode the display. Its own
//     keyjazz warning says so -- "this tool cannot see it".
//   - m8_nav decodes the display but cannot read during playback at all: the
//     M8 redraws continuously while playing, so readInto never reaches its
//     settle window and every read returns timedOut. Measured on fw 6.5.2 --
//     see docs/tools/m8_livecheck.md.
//   - COM3 is exclusive, so the two cannot be run side by side.
//
// LiveReader closes that gap, so this tool holds the serial port, the audio
// device and the guard in one process, samples the screen every few
// milliseconds for the whole capture window, and fails fast instead of
// producing a number that has to be thrown away later.
//
// The two failures it is built to catch, both from M8_DRIVER_BUGS.md:
//
//   #34 (2026-08-19) -- `set AMP FF` silently moved LIM from 04 to 08. The
//        AMP sweep point taken at the wrong LIM looked exactly like a real
//        measurement and was only discarded because a later read happened to
//        notice. --watch guards the rows a result depends on.
//
//   2026-08-20 -- PLAY is a toggle. The capture tool's start press stopped an
//        already-playing device and its stop press started it again, so the
//        window held the silence between notes. It latched after three good
//        captures and poisoned the remaining seventeen. The transport is read
//        before it is pressed, and watched for the whole window.
//
//   m8_watchcapture --port COM3 --audio M8 --seconds 3 --out probe.wav \
//                   --watch AMP --watch LIM
//
// Exit 0 = clean capture. 1 = a guard fired (the WAV is still written, marked
// `"guard_passed": false`, because a rejected capture is evidence too).
// 2 = could not open the port, the audio device, or the live reader.
// ===========================================================================

#include "audio/CaptureCore.h"

#include "m8/FieldGuard.h"
#include "m8/LiveReader.h"
#include "m8/M8Device.h"
#include "m8/ScreenModel.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace m8::dev;
using namespace m8::audio;
using clk = std::chrono::steady_clock;

namespace {

// canonRow / rowTextFor / sampleWatch live in m8/FieldGuard.h so
// tests/test_field_guard.cpp can pin them; see that header for why.
using Watch = FieldWatch;

struct Sample {
    int  tMs = 0;
    long seq = 0;
    bool playing = false;
    int  quietMs = 0;
};

void writeTimeline(const std::string& wavPath, const std::vector<Sample>& timeline) {
    std::string p = wavPath;
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".wav") p = p.substr(0, p.size() - 4);
    p += ".timeline.json";
    FILE* f = std::fopen(p.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\n  \"samples\": [\n");
    for (size_t i = 0; i < timeline.size(); ++i) {
        const Sample& s = timeline[i];
        std::fprintf(f, "    {\"t_ms\":%d,\"seq\":%ld,\"playing\":%s,\"quiet_ms\":%d}%s\n",
                     s.tMs, s.seq, s.playing ? "true" : "false", s.quietMs,
                     i + 1 < timeline.size() ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
    std::fclose(f);
    std::printf("  wrote %s  (%zu samples)\n", p.c_str(), timeline.size());
}

} // namespace

int main(int argc, char** argv) {
    std::string port = "COM3", audioMatch = "M8", out = "watchcapture.wav";
    double seconds = 3.0;
    int sampleMs = 10;
    float preRollMs = 5.0f;
    std::vector<Watch> watches;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--port") && i + 1 < argc)            port = argv[++i];
        else if (!std::strcmp(a, "--audio") && i + 1 < argc)      audioMatch = argv[++i];
        else if (!std::strcmp(a, "--seconds") && i + 1 < argc)    seconds = std::atof(argv[++i]);
        else if (!std::strcmp(a, "--out") && i + 1 < argc)        out = argv[++i];
        else if (!std::strcmp(a, "--watch") && i + 1 < argc)      watches.push_back({argv[++i], "", "", false});
        else if (!std::strcmp(a, "--sample-ms") && i + 1 < argc)  sampleMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--pre-roll") && i + 1 < argc)   preRollMs = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(a, "--help")) {
            std::printf("usage: m8_watchcapture --port COM3 --audio M8 --seconds 3 --out f.wav\n"
                        "                       [--watch LABEL]... [--sample-ms 10] [--pre-roll 5]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a);
            return 2;
        }
    }

    // ---- serial ----------------------------------------------------------
    M8Device dev;
    if (!dev.open(port.c_str())) {
        std::fprintf(stderr, "watchcapture: could not open %s\n", port.c_str());
        return 2;
    }

    // A settled read first, while nothing is moving. The baselines have to be
    // taken from a quiet screen -- reading them mid-repaint would bake a
    // half-drawn row in as the thing everything else is compared against.
    dev.readSettled(0, 250, 2000);
    const Screen screen = identifyScreen(dev.grid());
    // ScreenModel.h has no Screen-to-string helper; SemanticState uses the
    // canonicalised header for this, so do the same rather than add one.
    const std::string screenCanon = dev.grid().canon();
    const bool playheadObservable = isGridScreen(screen);
    const bool wasPlaying = playheadVisible(dev.grid());

    for (Watch& w : watches) {
        const std::string row = rowTextFor(dev.grid(), w.label);
        if (row.empty()) {
            std::fprintf(stderr, "watchcapture: --watch %s not found on this screen\n",
                         w.label.c_str());
            dev.close();
            return 2;
        }
        w.baseline = canonRow(row);
        std::printf("watch: %-10s baseline row %s\n", w.label.c_str(), row.c_str());
    }

    if (!playheadObservable) {
        // Not fatal, but the transport guard is blind here and saying so is
        // the difference between "not playing" and "cannot tell" -- the
        // distinction #28 exists for.
        std::printf("! %s draws no playhead; the transport guard is inactive\n",
                    screenCanon.c_str());
    }

    // ---- audio -----------------------------------------------------------
    ensureM8WindowsInputVolume100();

    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::fprintf(stderr, "watchcapture: ma_context_init failed\n");
        dev.close();
        return 2;
    }
    ma_device_id deviceId;
    char deviceName[256] = {0};
    if (!findAudioDevice(&context, audioMatch.c_str(), &deviceId, deviceName, sizeof(deviceName))) {
        ma_context_uninit(&context);
        dev.close();
        return 2;
    }
    std::printf("audio: %s\n", deviceName);

    CaptureData captureData;
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.pDeviceID = &deviceId;
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = kChannels;
    cfg.sampleRate        = kSampleRate;
    cfg.dataCallback      = captureCallback;
    cfg.pUserData         = &captureData;

    ma_device device;
    if (ma_device_init(&context, &cfg, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "watchcapture: ma_device_init failed\n");
        ma_context_uninit(&context);
        dev.close();
        return 2;
    }

    // ---- watch -----------------------------------------------------------
    LiveReader live(dev);
    if (!live.start()) {
        std::fprintf(stderr, "watchcapture: LiveReader::start() refused\n");
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        dev.close();
        return 2;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "watchcapture: ma_device_start failed\n");
        live.stop();
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        dev.close();
        return 2;
    }

    // Read-then-press. Never a blind PLAY.
    if (!wasPlaying) {
        dev.press(Key::PLAY);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::printf("capture: recording %.1f s, sampling the screen every %d ms...\n",
                seconds, sampleMs);

    std::vector<Sample> timeline;
    std::string abortReason;
    bool transportDropped = false;
    const auto t0 = clk::now();
    const auto until = t0 + std::chrono::milliseconds(static_cast<int>(seconds * 1000));

    while (clk::now() < until) {
        const LiveSnapshot s = live.snapshot();
        Sample rec;
        rec.tMs     = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                          clk::now() - t0).count());
        rec.seq     = s.seq;
        rec.quietMs = s.quietMs;
        rec.playing = playheadObservable && playheadVisible(s.grid);
        timeline.push_back(rec);

        // Transport guard. Only meaningful where the playhead is drawn, and
        // only after the first 400 ms -- the marker takes a moment to appear
        // after the press, and firing on that would reject every capture.
        if (playheadObservable && rec.tMs > 400 && !rec.playing) {
            transportDropped = true;
            abortReason = "transport stopped mid-capture (PLAY is a toggle -- "
                          "the device may have been playing on entry)";
            break;
        }

        // Field guard.
        bool drift = false;
        for (Watch& w : watches)
            if (sampleWatch(w, s.grid)) drift = true;
        if (drift) {
            abortReason = "a watched field changed during the capture";
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sampleMs));
    }

    const int capturedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count());

    captureData.done.store(true);
    ma_device_stop(&device);

    // Restore the transport to how we found it.
    if (!wasPlaying && !transportDropped) {
        dev.press(Key::PLAY);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    live.stop();
    ma_device_uninit(&device);
    ma_context_uninit(&context);

    // ---- write out -------------------------------------------------------
    std::vector<float> frames;
    {
        std::lock_guard<std::mutex> lock(captureData.mtx);
        frames = captureData.frames;
    }
    std::printf("capture: stopped after %d ms, %zu frames\n", capturedMs, frames.size() / kChannels);

    const bool guardPassed = abortReason.empty();
    if (!guardPassed) std::fprintf(stderr, "\n! GUARD: %s\n\n", abortReason.c_str());

    auto trimmed = trimToOnset(frames, kSampleRate, preRollMs);

    float peak = 0.0f;
    for (float s : trimmed) peak = std::max(peak, std::fabs(s));

    std::ostringstream extra;
    extra << "  \"watch\": {\n";
    extra << "    \"guard_passed\": " << (guardPassed ? "true" : "false") << ",\n";
    extra << "    \"abort_reason\": \"" << abortReason << "\",\n";
    extra << "    \"screen\": \"" << screenCanon << "\",\n";
    extra << "    \"playhead_observable\": " << (playheadObservable ? "true" : "false") << ",\n";
    extra << "    \"transport_was_running_on_entry\": " << (wasPlaying ? "true" : "false") << ",\n";
    extra << "    \"captured_ms\": " << capturedMs << ",\n";
    extra << "    \"screen_samples\": " << timeline.size() << ",\n";
    extra << "    \"fields\": [\n";
    for (size_t i = 0; i < watches.size(); ++i) {
        extra << "      {\"label\":\"" << watches[i].label << "\",\"held\":"
              << (watches[i].drifted ? "false" : "true");
        if (watches[i].drifted)
            extra << ",\"baseline\":\"" << watches[i].baseline
                  << "\",\"saw\":\"" << watches[i].sawInstead << "\"";
        extra << "}" << (i + 1 < watches.size() ? "," : "") << "\n";
    }
    extra << "    ]\n  }";

    CaptureManifest cm;
    cm.port = port;
    cm.seconds = seconds;
    cm.preRollMs = preRollMs;
    cm.measuredPeak = peak;
    cm.levelPassed = guardPassed;
    cm.extraJson = extra.str();

    writeWav(out, trimmed, kChannels, kSampleRate, &cm);
    writeTimeline(out, timeline);
    std::printf("capture peak: %.5f   guard: %s\n", peak, guardPassed ? "PASSED" : "FAILED");

    dev.close();
    return guardPassed ? 0 : 1;
}
