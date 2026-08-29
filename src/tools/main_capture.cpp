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

#include "audio/CaptureCore.h"

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

// The audio half lives in audio/CaptureCore.{h,cpp}, shared with
// m8_watchcapture. Only the serial driving and the CLI stay here.
using namespace m8::audio;

// ---- Win32 serial ---------------------------------------------------------

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// NOMINMAX is load-bearing here. Until the audio half moved to CaptureCore,
// miniaudio's implementation pulled windows.h in first and the min/max undefs
// above it took effect, so this include was a no-op. Now it is the first one,
// and without this the macros shadow std::min/std::max further down.
#ifndef NOMINMAX
#define NOMINMAX
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

#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

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
                                      int keyjazzNote, uint8_t keyjazzVel, double noteMs) {
    {
        std::lock_guard<std::mutex> lock(captureData.mtx);
        captureData.frames.clear();
    }
    captureData.done.store(false);

    const bool keyjazz = keyjazzNote >= 0;
    std::printf("capture: recording %.1f s...\n", seconds);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (keyjazz) {
        // m8_capture speaks serial but does not decode the display, so unlike
        // m8_nav it cannot tell which screen the note is about to land on --
        // and on PHRASE or TABLE the M8 records keyjazz into the cell under
        // the cursor rather than only sounding it. Four capture runs on
        // 2026-08-18 overwrote a phrase row that way (hw_findings.md UI-14).
        // Warning rather than refusing, because refusing needs the screen and
        // this tool cannot see it; the refusal lives in m8_nav, which can.
        std::printf("! keyjazz writes into the cell under the cursor on PHRASE and\n"
                    "!   TABLE. If the device is on either, this capture is editing the\n"
                    "!   loaded project. Park it first:  m8_nav --port <PORT>\n"
                    "!   --goto-screen INSTRUMENT\n");
        serialKeyjazzOn(serial, static_cast<uint8_t>(keyjazzNote), keyjazzVel);
        std::printf("serial: keyjazz note-on (note %d, vel 0x%02X)\n", keyjazzNote, keyjazzVel);
    } else {
        serialPressStart(serial, startMask);
        std::printf("serial: play sent (start-mask: 0x%02X)\n", startMask);
    }

    // --note-ms releases the keyjazz note early and keeps recording for the rest of
    // the window. Without it the note is held for the whole capture, so anything that
    // rings on AFTER the note stops -- a reverb or a delay tail -- is never in the file.
    // That is what blocked the reverb RT60 measurement; see hw_findings.md UI-29.
    const double windowMs = seconds * 1000.0;
    const bool earlyRelease = keyjazz && noteMs > 0.0 && noteMs < windowMs;
    if (earlyRelease) {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(noteMs)));
        serialKeyjazzOff(serial);
        std::printf("serial: keyjazz note-off after %.0f ms (recording the tail)\n", noteMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(windowMs - noteMs)));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(windowMs)));
    }

    captureData.done.store(true);
    if (keyjazz) {
        if (!earlyRelease) {
            serialKeyjazzOff(serial);
            std::printf("serial: keyjazz note-off\n");
        }
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
    double noteMs = 0.0;           // >0 => release the keyjazz note early, keep recording
    bool checkLevel = false;
    float floorLevel = 0.5f;

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
        else if (a == "--note-ms")     noteMs = std::atof(next().c_str());
        else if (a == "--check-level") {
            checkLevel = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                floorLevel = static_cast<float>(std::atof(next().c_str()));
            }
        }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (port.empty()) {
        std::fprintf(stderr,
            "usage: m8_capture --port COM4 --audio M8 [--seconds 3] [--out out.wav]\n"
            "                  [--start-mask 0x08] [--stop-mask 0x08] [--note-ms 300]\n"
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

#ifdef _WIN32
    ensureM8WindowsInputVolume100();
#endif

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

            auto trimmed = captureOnce(serial, captureData, startMask, stopMask, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel, noteMs);
            bool levelPassed = checkCapturePeak(trimmed, floorLevel, checkLevel);
            float peak = 0.0f;
            for (float s : trimmed) peak = std::max(peak, std::fabs(s));

            CaptureManifest cm{ port, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel, checkLevel, floorLevel, peak, levelPassed };
            std::string path = outDir + "/" + e.label + ".wav";
            writeWav(path, trimmed, 2, 48000, &cm);
            ++ok;
        }
        std::printf("\nbatch complete: %zu/%zu captured\n", ok, batchEntries.size());
    } else {
        auto trimmed = captureOnce(serial, captureData, startMask, stopMask, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel, noteMs);
        bool levelPassed = checkCapturePeak(trimmed, floorLevel, checkLevel);
        float peak = 0.0f;
        for (float s : trimmed) peak = std::max(peak, std::fabs(s));

        CaptureManifest cm{ port, seconds, preRollMs, tailSeconds, keyjazzNote, keyjazzVel, checkLevel, floorLevel, peak, levelPassed };

        if (!outPath.empty()) {
            writeWav(outPath, trimmed, 2, 48000, &cm);
        } else if (!outDir.empty()) {
            std::filesystem::create_directories(outDir);
            // Default filename from timestamp
            auto now = std::chrono::system_clock::now();
            auto epoch = now.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
            char filename[64];
            std::snprintf(filename, sizeof(filename), "capture_%lld.wav", millis);
            std::string path = outDir + "/" + filename;
            writeWav(path, trimmed, 2, 48000, &cm);
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
