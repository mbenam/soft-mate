#pragma once

// ===========================================================================
// CaptureCore — the USB-audio half of m8_capture, extracted so more than one
// tool can use it.
//
// Why this exists
// ---------------
// m8_capture speaks serial but does not decode the display, so it cannot see
// what the device is doing while it records -- its own keyjazz warning says as
// much ("this tool cannot see it; the refusal lives in m8_nav, which can").
// m8_watchcapture does both, which means a second tool needs this code.
//
// Copying it instead would have produced two WAV writers and two device
// pickers that drift apart, which docs/tools/README.md already flags as a
// hazard for the two independent serial implementations. One capture core,
// two callers.
//
// Deliberately NOT part of m8_device: that library is serial + SLIP +
// ScreenGrid and its CMake comment commits it to "no engine, no SDL, no
// audio". Audio stays out here, and a tool that needs both links both.
//
// The miniaudio implementation is compiled exactly once, in CaptureCore.cpp.
// Including this header gives you the declarations only.
// ===========================================================================

#include "miniaudio.h"

// miniaudio may define min/max macros that conflict with std::min/std::max.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace m8 {
namespace audio {

// The M8's USB interface presents 48 kHz stereo float. Every helper here
// assumes interleaved stereo float in and 16-bit PCM out.
inline constexpr int kSampleRate = 48000;
inline constexpr int kChannels   = 2;

// Shared between the caller and miniaudio's realtime thread.
struct CaptureData {
    std::vector<float> frames;
    std::mutex mtx;
    std::atomic<bool> done{false};
};

// ma_device_config::dataCallback. pUserData must be a CaptureData*.
void captureCallback(ma_device* device, void* output, const void* input,
                     ma_uint32 frameCount);

// First capture device whose name contains `match`. On failure, prints the
// available devices to stderr so the caller does not have to guess.
bool findAudioDevice(ma_context* ctx, const char* match,
                     ma_device_id* outId, char* outName, size_t nameLen);

// Provenance written next to every WAV, so a measurement carries the state it
// was taken in rather than relying on someone's notes.
struct CaptureManifest {
    std::string port;
    double seconds = 0.0;
    float preRollMs = 0.0f;
    float tailSeconds = 0.0f;
    int keyjazzNote = -1;
    uint8_t keyjazzVel = 0;
    bool checkLevel = false;
    float floorLevel = 0.0f;
    float measuredPeak = 0.0f;
    bool levelPassed = true;
    // Free-form JSON fragment appended inside the object, without a leading
    // comma. m8_watchcapture puts its guard verdict here; m8_capture leaves it
    // empty. Keeps one manifest writer rather than two similar ones.
    std::string extraJson;
};

// <path minus .wav>.manifest.json, including a hash of the WAV as written.
void writeManifestFile(const std::string& wavPath,
                       int channels, int sampleRate, size_t nFrames,
                       const CaptureManifest& cm);

// 16-bit PCM RIFF. Writes the manifest too when `cm` is given.
void writeWav(const std::string& path, const std::vector<float>& interleaved,
              int channels, int sampleRate, const CaptureManifest* cm = nullptr);

// Drop the silence before the first sample over 0.01, keeping preRollMs of it.
std::vector<float> trimToOnset(const std::vector<float>& audio,
                               int sampleRate, float preRollMs = 5.0f);

// Peak check against a floor. The failure it catches is the Windows recording
// level for the M8 input silently resetting, which yields a quiet capture that
// looks like a quiet instrument.
bool checkCapturePeak(const std::vector<float>& samples, float floorLevel,
                      bool enforceCheck);

// Force that recording level to 100% if something moved it. No-op off Windows.
void ensureM8WindowsInputVolume100();

uint64_t computeFnv1a64(const std::string& filepath, size_t& byteCount);
std::string hexHash(uint64_t hash);
std::string getIsoTimestampUtc();

} // namespace audio
} // namespace m8
