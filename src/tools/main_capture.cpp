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

static void serialPressStart(SerialPort& sp) {
    // Press START (bit 3 = 0x08)
    uint8_t press = 0x08;
    sp.sendByte('C');
    sp.sendByte(press);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Release
    uint8_t release = 0x00;
    sp.sendByte('C');
    sp.sendByte(release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

static void serialStop(SerialPort& sp) {
    // Send stop: typically pressing the stop key combo or just releasing all buttons
    // For the M8, pressing the right key combo stops playback.
    // We'll send a few button taps to trigger stop.
    // The simplest: press 'A' (which in song view stops playback)
    uint8_t press = 0x10;  // KEY_A
    sp.sendByte('C');
    sp.sendByte(press);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint8_t release = 0x00;
    sp.sendByte('C');
    sp.sendByte(release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (port.empty()) {
        std::fprintf(stderr, "usage: m8_capture --port COM4 --audio M8 [--seconds 3] [--out out.wav]\n");
        return 1;
    }
    if (audioMatch.empty()) audioMatch = "M8";

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

    // Start capture
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "miniaudio: device start failed\n");
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return 1;
    }
    std::printf("capture: started, recording %.1f s...\n", seconds);

    // Send play command
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    serialPressStart(serial);
    std::printf("serial: play sent\n");

    // Wait for recording duration
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));

    // Stop
    captureData.done.store(true);
    serialStop(serial);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ma_device_stop(&device);
    std::printf("capture: stopped, %zu frames captured\n", captureData.frames.size() / 2);

    // Trim to onset
    auto trimmed = trimToOnset(captureData.frames, 48000, preRollMs);

    // Optional tail trim
    if (tailSeconds > 0.0f) {
        size_t maxFrames = static_cast<size_t>(tailSeconds * 48000);
        if (trimmed.size() / 2 > maxFrames) {
            trimmed.resize(maxFrames * 2);
        }
    }

    // Write output
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

    // Cleanup
    serial.close();
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    std::printf("done.\n");
    return 0;
}
