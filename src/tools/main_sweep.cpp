// ===========================================================================
// m8_sweep — set a field, capture, measure, next value. Unattended.
//
// Why this exists
// ---------------
// The same loop was hand-written three times on 2026-08-24 -- sweeping REPITCH
// STEPS, sweeping the REP.BPM byte, sweeping FILTER cutoff -- and each rewrite
// carried its own bugs. One measured the sequencer's row rate instead of the
// sampler's loop and "passed". One read the wrong screen row because a short
// label collided by substring. One left a play mode reset because a field
// lookup matched the row above.
//
// None of that was hard, and all of it was avoidable. This is that loop, once:
// set the field, play a note, capture, measure, print a row, restore.
//
// It is deliberately dumb and deliberately self-sufficient. Every value is
// verified by read-back before the capture, the field is restored at the end,
// and the output is a table plus an exit code -- so running it needs a
// scheduled command, not an agent watching. The judgement is in choosing the
// sweep and reading the table, and that stays with a human.
//
//   m8_sweep --port COM3 --audio M8 --field CUTOFF --values 20,40,60,80,A0,C0,E0 \
//            --out-dir sweep_cutoff --allow-mutation
//
// Exit 0 = every value set, captured and measured. 1 = at least one failed;
// the table says which. 2 = setup failed.
// ===========================================================================

#include "audio/CaptureCore.h"
#include "audio/Metrics.h"

#include "m8/FieldGuard.h"
#include "m8/Gestures.h"
#include "m8/M8Device.h"
#include "m8/Primitives.h"
#include "m8/ScreenModel.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace m8::dev;
using namespace m8::audio;

namespace {

struct Row {
    std::string value;
    bool   set = false;
    std::string readBack;
    float  peak = 0.0f;
    Bands  bands;
    double pitchHz = 0.0;
    int    periodSamples = 0;
    double periodCorr = 0.0;
    std::string note;
};

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string port = "COM3", audioMatch = "M8", field = "CUTOFF";
    std::string valuesArg = "20,40,60,80,A0,C0,E0", outDir = "sweep_out";
    double seconds = 2.0;
    int note = 60, holdMs = 20;
    bool allowMutation = false, wantPeriod = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (!std::strcmp(a, "--port")    && i + 1 < argc) port = argv[++i];
        else if (!std::strcmp(a, "--audio")   && i + 1 < argc) audioMatch = argv[++i];
        else if (!std::strcmp(a, "--field")   && i + 1 < argc) field = argv[++i];
        else if (!std::strcmp(a, "--values")  && i + 1 < argc) valuesArg = argv[++i];
        else if (!std::strcmp(a, "--out-dir") && i + 1 < argc) outDir = argv[++i];
        else if (!std::strcmp(a, "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(a, "--note")    && i + 1 < argc) note = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--hold-ms") && i + 1 < argc) holdMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--period")) wantPeriod = true;
        else if (!std::strcmp(a, "--allow-mutation")) allowMutation = true;
        else if (!std::strcmp(a, "--help")) {
            std::printf("usage: m8_sweep --port COM3 --audio M8 --field CUTOFF\n"
                        "                --values 20,40,60 --out-dir dir --allow-mutation\n"
                        "                [--seconds 2] [--note 60] [--hold-ms 20] [--period]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown argument: %s\n", a); return 2; }
    }

    if (!allowMutation) {
        std::fprintf(stderr, "m8_sweep edits a field; pass --allow-mutation\n");
        return 2;
    }

    const std::vector<std::string> values = splitCsv(valuesArg);
    if (values.empty()) { std::fprintf(stderr, "no --values\n"); return 2; }

    GestureTable& g = getGestures();
    if (!g.loadFromFile("hw_buttons.json") || !g.isReady()) {
        std::fprintf(stderr, "gestures not pinned (hw_buttons.json); run from the repo root\n");
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    M8Device dev;
    if (!dev.open(port.c_str())) {
        std::fprintf(stderr, "could not open %s\n", port.c_str());
        return 2;
    }
    dev.readSettled(0, 250, 2000);
    const Screen screen = identifyScreen(dev.grid());
    // The INSTRUMENT screen has one field map per instrument TYPE. Without the
    // hint every lookup uses the Sampler layout, so on a MacroSynth the rows
    // are wrong and rowTextFor comes back empty -- which is exactly what the
    // first run of this tool printed under "read back".
    const std::string instType =
        (screen == Screen::INSTRUMENT) ? readInstrumentType(dev.grid()) : std::string();

    // The value to put back. A sweep that leaves the instrument somewhere else
    // silently poisons whatever is measured next -- that is how a play mode got
    // left reset and an AMP left at 5F earlier the same day.
    const std::string originalRow = rowTextFor(dev.grid(), field, screen, instType);
    // The VALUE, not the row. Primitives::readField deliberately returns the
    // whole row so assertField can substring-match inside it -- conflating the
    // two is a documented trap (m8drv's `read` vs `read --row`), and the first
    // run of this tool fell straight into it: the restore was handed
    // "CUTOFF  00        DEL 00" as a target and failed.
    std::string original;
    {
        const std::string row = rowTextFor(dev.grid(), field, screen, instType);
        const FieldInfo* info = findFieldInfo(screen, field, instType);
        const std::string label = info ? info->label : field;
        size_t p = row.find(label);
        if (p != std::string::npos) {
            p += label.size();
            while (p < row.size() && row[p] == ' ') ++p;
            while (p < row.size() && std::isxdigit(static_cast<unsigned char>(row[p])))
                original += row[p++];
        }
    }
    std::printf("field   : %s on %s\n", field.c_str(), dev.grid().canon().c_str());
    std::printf("original: %s\n", originalRow.c_str());

    // ---- audio ----
    ensureM8WindowsInputVolume100();
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::fprintf(stderr, "ma_context_init failed\n"); dev.close(); return 2;
    }
    ma_device_id deviceId;
    char deviceName[256] = {0};
    if (!findAudioDevice(&context, audioMatch.c_str(), &deviceId, deviceName, sizeof(deviceName))) {
        ma_context_uninit(&context); dev.close(); return 2;
    }
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
        std::fprintf(stderr, "ma_device_init failed\n");
        ma_context_uninit(&context); dev.close(); return 2;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "ma_device_start failed\n");
        ma_device_uninit(&device); ma_context_uninit(&context); dev.close(); return 2;
    }
    std::printf("audio   : %s\n\n", deviceName);

    std::vector<Row> rows;
    int failures = 0;

    for (const std::string& want : values) {
        Row r; r.value = want;

        auto res = editValue(dev, field, want, holdMs);
        if (!res.ok) {
            r.note = res.error.substr(0, 90);
            ++failures;
            rows.push_back(r);
            std::fprintf(stderr, "  %s: set failed -- %s\n", want.c_str(), r.note.c_str());
            continue;
        }
        r.set = true;

        // Verified by read-back before the capture, not assumed. A measurement
        // is only worth the state it was taken in (hw_measure.py's rule).
        dev.readSettled(0, 200, 1500);
        r.readBack = canonRow(rowTextFor(dev.grid(), field, screen, instType));

        {   // capture one note
            std::lock_guard<std::mutex> lock(captureData.mtx);
            captureData.frames.clear();
        }
        captureData.done.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        dev.keyjazz(static_cast<uint8_t>(note), 0x7F);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
        captureData.done.store(true);
        dev.keyjazz(static_cast<uint8_t>(note), 0x00);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        std::vector<float> frames;
        {
            std::lock_guard<std::mutex> lock(captureData.mtx);
            frames = captureData.frames;
        }
        auto trimmed = trimToOnset(frames, kSampleRate, 5.0f);

        std::vector<float> mono;
        mono.reserve(trimmed.size() / 2);
        for (size_t i = 0; i + 1 < trimmed.size(); i += 2)
            mono.push_back(0.5f * (trimmed[i] + trimmed[i + 1]));

        for (float v : mono) r.peak = std::max(r.peak, std::fabs(v));
        r.bands   = measureBands(mono, kSampleRate);
        r.pitchHz = measurePitch(mono, kSampleRate, 600, 4000);
        if (wantPeriod) r.periodSamples = measurePeriod(mono, 2000, 60000, r.periodCorr);

        const std::string wav = outDir + "/" + field + "_" + want + ".wav";
        CaptureManifest cm;
        cm.port = port;
        cm.seconds = seconds;
        cm.measuredPeak = r.peak;
        writeWav(wav, trimmed, kChannels, kSampleRate, &cm);

        rows.push_back(r);
        std::fprintf(stderr, "  %s: peak %.4f  low %.2f mid %.2f high %.2f\n",
                     want.c_str(), r.peak, r.bands.lowRatio(), r.bands.midRatio(),
                     r.bands.highRatio());
    }

    ma_device_stop(&device);
    ma_device_uninit(&device);
    ma_context_uninit(&context);

    // Put it back.
    bool restored = false;
    if (!original.empty()) {
        auto rr = editValue(dev, field, original, holdMs);
        restored = rr.ok;
    }

    std::printf("\n%-8s %-18s %8s %6s %6s %6s %9s %8s\n",
                "value", "read back", "peak", "low", "mid", "high", "pitch(Hz)", "period");
    for (const Row& r : rows) {
        if (!r.set) { std::printf("%-8s %-18s   SET FAILED: %s\n",
                                  r.value.c_str(), "-", r.note.c_str()); continue; }
        std::printf("%-8s %-18s %8.4f %6.2f %6.2f %6.2f %9.1f %8d\n",
                    r.value.c_str(), r.readBack.substr(0, 18).c_str(), r.peak,
                    r.bands.lowRatio(), r.bands.midRatio(), r.bands.highRatio(),
                    r.pitchHz, r.periodSamples);
    }
    std::printf("\nrestored: %s (to '%s')\n", restored ? "yes" : "NO", original.c_str());
    if (!restored)
        std::fprintf(stderr, "! %s was NOT restored -- reload the project from the card\n",
                     field.c_str());

    dev.close();
    return (failures || !restored) ? 1 : 0;
}
