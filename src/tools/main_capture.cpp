// ===========================================================================
// src/tools/main_capture.cpp
//
// Drives an M8 headless over serial and records its USB audio via miniaudio.
// Assumes the probe .m8s is already loaded on the device by the operator.
//
//   m8_capture --port COM4 --audio "M8" --seconds 2.5 --out ref.wav
//   m8_capture --port COM4 --audio "M8" --batch probes.txt --out-dir refs/
//
// Links miniaudio (header-only). No SDL, no engine. Standalone.
// ===========================================================================

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// miniaudio may define min/max macros that conflict with std::min/std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <fstream>
#include <iostream>

// ---- Win32 serial ---------------------------------------------------------

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct SerialPort {
    HANDLE h = INVALID_HANDLE_VALUE;

    bool open(const char* port) {
        char path[64];
        std::snprintf(path, sizeof(path), "\\\\.\\%s", port);
        h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "cannot open %s (error %lu)\n", port, GetLastError());
            return false;
        }

        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;

        if (!SetCommState(h, &dcb)) {
            std::fprintf(stderr, "SetCommState failed (error %lu)\n", GetLastError());
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            return false;
        }

        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        SetCommTimeouts(h, &timeouts);

        // Flush
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    bool send(const void* data, size_t len) {
        DWORD written = 0;
        if (!WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr)) {
            std::fprintf(stderr, "serial write failed (error %lu)\n", GetLastError());
            return false;
        }
        return written == len;
    }

    bool sendByte(uint8_t b) { return send(&b, 1); }

    void close() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
    }

    ~SerialPort() { close(); }
};
#else
// POSIX stub (not the primary platform)
struct SerialPort {
    int fd = -1;
    bool open(const char*) { std::fprintf(stderr, "serial not implemented on this platform\n"); return false; }
    bool send(const void*, size_t) { return false; }
    bool sendByte(uint8_t) { return false; }
    void close() {}
};
#endif

// ---- button protocol (matches m8_client.py) -------------------------------
//
//   'E'           enable display
//   'C' <mask>    set button state
//
//   Button bit positions (from status.md):
//     bit 0 = UP, bit 1 = DOWN, bit 2 = LEFT, bit 3 = RIGHT
//     bit 4 = A,  bit 5 = B,    bit 6 = C,    bit 7 = D
//     START = bit 3 of the second byte? Actually from the Python client:
//       'C' + (button_byte) where buttons are mapped differently.
//
//   Looking at m8_client.py more carefully:
//     KEY_UP=0x01, KEY_DOWN=0x02, KEY_LEFT=0x04, KEY_RIGHT=0x08
//     KEY_A=0x10, KEY_B=0x20, KEY_C=0x40, KEY_D=0x80
//     START is sent as 'C' 0x08 (right arrow?) No...
//
//   Actually from the m8 protocol: the 'C' command sends a button mask.
//   START = 0x08 based on the spec.

static void serialEnable(SerialPort& sp) {
    sp.sendByte('E');
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static void serialPressStart(SerialPort& sp, uint8_t pressMask) {
    sp.sendByte('C');
    sp.sendByte(pressMask);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Release
    sp.sendByte('C');
    sp.sendByte(0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

static void serialStop(SerialPort& sp, uint8_t stopMask) {
    sp.sendByte('C');
    sp.sendByte(stopMask);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sp.sendByte('C');
    sp.sendByte(0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// Keyjazz: play a live note on the currently-selected instrument ('K' note vel),
// note-off with 'K' 0xFF. Same command m8c uses — triggers the synth engine
// directly, no song/sequencer, so it's a from-scratch note for synth parity.
static void serialKeyjazzOn(SerialPort& sp, uint8_t note, uint8_t vel) {
    sp.sendByte('K'); sp.sendByte(note); sp.sendByte(vel);
}
static void serialKeyjazzOff(SerialPort& sp) {
    sp.sendByte('K'); sp.sendByte(0xFF);
}

// ---- miniaudio capture ----------------------------------------------------

struct CaptureData {
    std::vector<float> frames;
    std::mutex mtx;
    std::atomic<bool> done{false};
};

static void captureCallback(ma_device* device, void* output, const void* input,
                            ma_uint32 frameCount) {
    auto* data = static_cast<CaptureData*>(device->pUserData);
    if (data->done.load()) return;

    const float* in = static_cast<const float*>(input);
    std::lock_guard<std::mutex> lock(data->mtx);
    data->frames.insert(data->frames.end(), in, in + frameCount * 2);  // stereo float
}

// ---- find audio device by substring match ---------------------------------

static bool findAudioDevice(ma_context* ctx, const char* match,
                            ma_device_id* outId, char* outName, size_t nameLen) {
    ma_device_info* captureInfos;
    ma_uint32 captureCount;
    ma_result res = ma_context_get_devices(ctx, nullptr, nullptr, &captureInfos, &captureCount);
    if (res != MA_SUCCESS) return false;

    for (ma_uint32 i = 0; i < captureCount; ++i) {
        if (std::strstr(captureInfos[i].name, match)) {
            *outId = captureInfos[i].id;
            std::strncpy(outName, captureInfos[i].name, nameLen - 1);
            return true;
        }
    }

    // Print available devices
    std::fprintf(stderr, "no audio device matching '%s'. available:\n", match);
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::fprintf(stderr, "  [%u] %s\n", i, captureInfos[i].name);
    }
    return false;
}

// ---- WAV writer (same as m8_render) ---------------------------------------

static void writeWav(const std::string& path, const std::vector<float>& interleaved,
                     int channels, int sampleRate) {
    const uint32_t nFrames  = static_cast<uint32_t>(interleaved.size() / channels);
    const uint32_t dataSize = nFrames * channels * 2;
    const uint32_t riffSize = 36 + dataSize;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "! cannot open %s\n", path.c_str()); return; }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  u32(riffSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);
    u16(1);
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * channels * 2));
    u16(static_cast<uint16_t>(channels * 2));
    u16(16);
    std::fwrite("data", 1, 4, f);  u32(dataSize);

    for (float s : interleaved) {
        s = std::max(-1.0f, std::min(1.0f, s));
        int16_t v = static_cast<int16_t>(s * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    std::printf("  wrote %-28s  %6.2f s\n", path.c_str(),
                static_cast<double>(nFrames) / sampleRate);
}

// ---- trim to note onset ---------------------------------------------------

static std::vector<float> trimToOnset(const std::vector<float>& audio,
                                      int sampleRate, float preRollMs = 5.0f) {
    const size_t frames = audio.size() / 2;
    const float threshold = 0.01f;
    const int preRoll = static_cast<int>(preRollMs * sampleRate / 1000.0f);

    // Find first frame where either channel exceeds threshold
    size_t onset = 0;
    for (size_t i = 0; i < frames; ++i) {
        float l = std::fabs(audio[i * 2]);
        float r = std::fabs(audio[i * 2 + 1]);
        if (l > threshold || r > threshold) {
            onset = i;
            break;
        }
    }

    // Apply pre-roll
    size_t start = (onset > static_cast<size_t>(preRoll)) ? onset - preRoll : 0;

    // Return from start to end
    std::vector<float> trimmed(audio.begin() + start * 2, audio.end());
    std::printf("  trim: onset at frame %zu, pre-roll %d, output from frame %zu (%zu frames)\n",
               onset, preRoll, start, trimmed.size() / 2);
    return trimmed;
}

// ---- batch file (Tier 2, M8_HARDWARE_TEST_SPEC.md §9.3) -------------------
//
// One `name<TAB>label` pair per line. `name` is the probe filename the operator
// loads on the device (informational, printed in the prompt); `label` names the
// output WAV (<out-dir>/<label>.wav). Blank lines and lines starting with '#'
// are skipped.

struct BatchEntry { std::string name; std::string label; };

static std::vector<BatchEntry> readBatchFile(const std::string& path, bool& ok) {
    std::vector<BatchEntry> entries;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "cannot open batch file %s\n", path.c_str());
        ok = false;
        return entries;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back(); // tolerate CRLF
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            std::fprintf(stderr, "batch file %s:%d: missing tab, expected 'name<TAB>label': %s\n",
                        path.c_str(), lineNo, line.c_str());
            ok = false;
            continue;
        }
        entries.push_back({ line.substr(0, tab), line.substr(tab + 1) });
    }
    return entries;
}

// Runs one press-start / wait / press-stop / trim cycle against an already-open
// serial port and an already-running capture device, resetting the shared frame
// buffer first. Shared by single-shot and batch modes so the serial port and
// audio device are opened exactly once per process regardless of how many
// notes get captured.
static std::vector<float> captureOnce(SerialPort& serial, CaptureData& captureData,
                                      uint8_t startMask, uint8_t stopMask,
                                      double seconds, float preRollMs, float tailSeconds,
                                      int keyjazzNote, uint8_t keyjazzVel) {
    {
        std::lock_guard<std::mutex> lock(captureData.mtx);
        captureData.frames.clear();
    }
    captureData.done.store(false);

    const bool keyjazz = keyjazzNote >= 0;
    std::printf("capture: recording %.1f s...\n", seconds);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (keyjazz) {
        serialKeyjazzOn(serial, static_cast<uint8_t>(keyjazzNote), keyjazzVel);
        std::printf("serial: keyjazz note-on (note %d, vel 0x%02X)\n", keyjazzNote, keyjazzVel);
    } else {
        serialPressStart(serial, startMask);
        std::printf("serial: play sent (start-mask: 0x%02X)\n", startMask);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));

    captureData.done.store(true);
    if (keyjazz) {
        serialKeyjazzOff(serial);
        std::printf("serial: keyjazz note-off\n");
    } else {
        serialStop(serial, stopMask);
        std::printf("serial: stop sent (stop-mask: 0x%02X)\n", stopMask);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<float> frames;
    {
        std::lock_guard<std::mutex> lock(captureData.mtx);
        frames = captureData.frames;
    }
    std::printf("capture: stopped, %zu frames captured\n", frames.size() / 2);

    auto trimmed = trimToOnset(frames, 48000, preRollMs);
    if (tailSeconds > 0.0f) {
        size_t maxFrames = static_cast<size_t>(tailSeconds * 48000);
        if (trimmed.size() / 2 > maxFrames) trimmed.resize(maxFrames * 2);
    }
    return trimmed;
}

// ---- main -----------------------------------------------------------------

int main(int argc, char** argv) {
    std::string port;
    std::string audioMatch;
    std::string outPath;
    std::string outDir;
    std::string batchFile;
    double seconds = 3.0;
    float preRollMs = 5.0f;
    float tailSeconds = 0.0f;  // optional fixed tail trim
    // M8 keybits (as m8c speaks them over the 'C' controller byte): PLAY = 1<<3 = 0x08.
    // PLAY is a *toggle* — pressing it starts playback from the cursor, pressing it again
    // stops. So the stop mask is the same key as start, not a separate one. Both are
    // empirically pinned per M8_HARDWARE_TEST_SPEC.md §5; these are the defaults.
    uint8_t startMask = 0x08;
    uint8_t stopMask = 0x08;
    int keyjazzNote = -1;          // >=0 => play a live note instead of PLAY toggle
    uint8_t keyjazzVel = 0x7F;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };

        if      (a == "--port")     port = next();
        else if (a == "--audio")    audioMatch = next();
        else if (a == "--out")      outPath = next();
        else if (a == "--out-dir")  outDir = next();
        else if (a == "--batch")    batchFile = next();
        else if (a == "--seconds")  seconds = std::atof(next().c_str());
        else if (a == "--pre-roll") preRollMs = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--tail")     tailSeconds = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--start-mask") startMask = static_cast<uint8_t>(std::strtol(next().c_str(), nullptr, 0));
        else if (a == "--stop-mask")  stopMask  = static_cast<uint8_t>(std::strtol(next().c_str(), nullptr, 0));
        else if (a == "--keyjazz")    keyjazzNote = static_cast<int>(std::strtol(next().c_str(), nullptr, 0)); // MIDI note (60=C-4)
        else if (a == "--keyjazz-vel") keyjazzVel = static_cast<uint8_t>(std::strtol(next().c_str(), nullptr, 0));
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (port.empty()) {
        std::fprintf(stderr,
            "usage: m8_capture --port COM4 --audio M8 [--seconds 3] [--out out.wav]\n"
            "                  [--start-mask 0x08] [--stop-mask 0x08]\n"
            "       m8_capture --port COM4 --audio M8 --batch probes.txt --out-dir refs/\n");
        return 1;
    }
    if (audioMatch.empty()) audioMatch = "M8";

    // Batch mode: validate the batch file up front, before touching serial or audio,
    // so a typo in the file (or a missing --out-dir) fails immediately instead of after
    // the operator has already reached for the device.
    std::vector<BatchEntry> batchEntries;
    if (!batchFile.empty()) {
        if (outDir.empty()) {
            std::fprintf(stderr, "--batch requires --out-dir\n");
            return 1;
        }
        bool batchOk = true;
        batchEntries = readBatchFile(batchFile, batchOk);
        if (!batchOk) return 1;
        if (batchEntries.empty()) {
            std::fprintf(stderr, "no entries found in batch file %s\n", batchFile.c_str());
            return 1;
        }
        std::filesystem::create_directories(outDir);
    }

    // Open serial port
    SerialPort serial;
    if (!serial.open(port.c_str())) return 1;
    std::printf("serial: %s opened\n", port.c_str());

    // Enable display
    serialEnable(serial);
    std::printf("serial: display enabled\n");

    // Set up miniaudio capture
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: context init failed\n");
        return 1;
    }

    ma_device_id deviceId;
    char deviceName[256] = {};
    if (!findAudioDevice(&context, audioMatch.c_str(), &deviceId, deviceName, sizeof(deviceName))) {
        ma_context_uninit(&context);
        return 1;
    }
    std::printf("audio: using device '%s'\n", deviceName);

    CaptureData captureData;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 2;
    cfg.sampleRate = 48000;
    cfg.capture.pDeviceID = &deviceId;
    cfg.dataCallback = captureCallback;
    cfg.pUserData = &captureData;

    ma_device device;
    if (ma_device_init(&context, &cfg, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: device init failed\n");
        ma_context_uninit(&context);
        return 1;
    }

    // Start capture. The device stays running for the whole process — including the
    // full batch loop below — so it is started and stopped exactly once regardless of
    // how many notes get captured.
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: device start failed\n");
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return 1;
    }

    if (!batchEntries.empty()) {
        // Tier 2 (M8_HARDWARE_TEST_SPEC.md §9.3): loop the name<TAB>label list. The one
        // human touch per probe is the reload the prompt asks for; capture itself is
        // identical to single-shot mode, just repeated.
        std::printf("batch: %zu probe(s) to capture into %s/\n", batchEntries.size(), outDir.c_str());
        size_t ok = 0;
        for (size_t i = 0; i < batchEntries.size(); ++i) {
            const auto& e = batchEntries[i];
            std::printf("\n[%zu/%zu] Load '%s' on the device now.\n", i + 1, batchEntries.size(), e.name.c_str());
            std::printf("Press Enter when ready to capture (label: %s)... ", e.label.c_str());
            std::fflush(stdout);
            std::string dummy;
            std::getline(std::cin, dummy);

            auto trimmed = captureOnce(serial, captureData, startMask, stopMask, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel);
            std::string path = outDir + "/" + e.label + ".wav";
            writeWav(path, trimmed, 2, 48000);
            ++ok;
        }
        std::printf("\nbatch complete: %zu/%zu captured\n", ok, batchEntries.size());
    } else {
        auto trimmed = captureOnce(serial, captureData, startMask, stopMask, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel);

        if (!outPath.empty()) {
            writeWav(outPath, trimmed, 2, 48000);
        } else if (!outDir.empty()) {
            std::filesystem::create_directories(outDir);
            // Default filename from timestamp
            auto now = std::chrono::system_clock::now();
            auto epoch = now.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
            char filename[64];
            std::snprintf(filename, sizeof(filename), "capture_%lld.wav", millis);
            std::string path = outDir + "/" + filename;
            writeWav(path, trimmed, 2, 48000);
        } else {
            std::fprintf(stderr, "no output path specified\n");
        }
    }

    ma_device_stop(&device);

    // Cleanup
    serial.close();
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    std::printf("done.\n");
    return 0;
}
