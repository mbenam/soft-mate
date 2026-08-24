// The one translation unit that compiles miniaudio. Must come before the
// header's own include of it.
#define MINIAUDIO_IMPLEMENTATION
#include "CaptureCore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

namespace m8 {
namespace audio {

void captureCallback(ma_device* device, void* output, const void* input,
                     ma_uint32 frameCount) {
    (void)output;
    auto* data = static_cast<CaptureData*>(device->pUserData);
    if (data->done.load()) return;

    const float* in = static_cast<const float*>(input);
    std::lock_guard<std::mutex> lock(data->mtx);
    data->frames.insert(data->frames.end(), in, in + frameCount * kChannels);
}

bool findAudioDevice(ma_context* ctx, const char* match,
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

    std::fprintf(stderr, "no audio device matching '%s'. available:\n", match);
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::fprintf(stderr, "  [%u] %s\n", i, captureInfos[i].name);
    }
    return false;
}

uint64_t computeFnv1a64(const std::string& filepath, size_t& byteCount) {
    byteCount = 0;
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return 0;
    uint64_t hash = 14695981039346656037ULL;
    char buffer[4096];
    while (f.read(buffer, sizeof(buffer)) || f.gcount() > 0) {
        std::streamsize count = f.gcount();
        byteCount += static_cast<size_t>(count);
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

std::string hexHash(uint64_t hash) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

std::string getIsoTimestampUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now_time);
#else
    gmtime_r(&now_time, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

void writeManifestFile(const std::string& wavPath,
                       int channels, int sampleRate, size_t nFrames,
                       const CaptureManifest& cm) {
    std::string manifestPath = wavPath;
    if (manifestPath.length() >= 4 && manifestPath.substr(manifestPath.length() - 4) == ".wav") {
        manifestPath = manifestPath.substr(0, manifestPath.length() - 4) + ".manifest.json";
    } else {
        manifestPath += ".manifest.json";
    }

    size_t byteCount = 0;
    uint64_t hash = computeFnv1a64(wavPath, byteCount);

    FILE* f = std::fopen(manifestPath.c_str(), "w");
    if (!f) return;

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"output_path\": \"%s\",\n", wavPath.c_str());
    std::fprintf(f, "  \"bytes\": %zu,\n", byteCount);
    std::fprintf(f, "  \"fnv1a64\": \"%s\",\n", hexHash(hash).c_str());
    std::fprintf(f, "  \"sample_rate\": %d,\n", sampleRate);
    std::fprintf(f, "  \"channels\": %d,\n", channels);
    std::fprintf(f, "  \"duration_seconds\": %.3f,\n", static_cast<double>(nFrames) / sampleRate);
    std::fprintf(f, "  \"port\": \"%s\",\n", cm.port.c_str());
    std::fprintf(f, "  \"timestamp_utc\": \"%s\",\n", getIsoTimestampUtc().c_str());
    std::fprintf(f, "  \"capture_settings\": {\n");
    std::fprintf(f, "    \"seconds\": %.2f,\n", cm.seconds);
    std::fprintf(f, "    \"pre_roll_ms\": %.1f,\n", cm.preRollMs);
    std::fprintf(f, "    \"tail_seconds\": %.2f,\n", cm.tailSeconds);
    std::fprintf(f, "    \"keyjazz_note\": %d,\n", cm.keyjazzNote);
    std::fprintf(f, "    \"keyjazz_vel\": %d\n", cm.keyjazzVel);
    std::fprintf(f, "  },\n");
    std::fprintf(f, "  \"check_level\": {\n");
    std::fprintf(f, "    \"enabled\": %s,\n", cm.checkLevel ? "true" : "false");
    std::fprintf(f, "    \"floor_level\": %.5f,\n", cm.floorLevel);
    std::fprintf(f, "    \"measured_peak\": %.5f,\n", cm.measuredPeak);
    std::fprintf(f, "    \"passed\": %s\n", cm.levelPassed ? "true" : "false");
    std::fprintf(f, "  }%s\n", cm.extraJson.empty() ? "" : ",");
    if (!cm.extraJson.empty()) std::fprintf(f, "%s\n", cm.extraJson.c_str());
    std::fprintf(f, "}\n");
    std::fclose(f);
    std::printf("  wrote %s\n", manifestPath.c_str());
}

void writeWav(const std::string& path, const std::vector<float>& interleaved,
              int channels, int sampleRate, const CaptureManifest* cm) {
    size_t nFrames = interleaved.size() / channels;
    uint32_t dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
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

    if (cm) writeManifestFile(path, channels, sampleRate, nFrames, *cm);
}

std::vector<float> trimToOnset(const std::vector<float>& audio,
                               int sampleRate, float preRollMs) {
    const size_t frames = audio.size() / kChannels;
    const float threshold = 0.01f;
    const int preRoll = static_cast<int>(preRollMs * sampleRate / 1000.0f);

    size_t onset = 0;
    for (size_t i = 0; i < frames; ++i) {
        float l = std::fabs(audio[i * 2]);
        float r = std::fabs(audio[i * 2 + 1]);
        if (l > threshold || r > threshold) { onset = i; break; }
    }

    size_t start = (onset > static_cast<size_t>(preRoll)) ? onset - preRoll : 0;
    std::vector<float> trimmed(audio.begin() + start * 2, audio.end());
    std::printf("  trim: onset at frame %zu, pre-roll %d, output from frame %zu (%zu frames)\n",
                onset, preRoll, start, trimmed.size() / 2);
    return trimmed;
}

bool checkCapturePeak(const std::vector<float>& samples, float floorLevel, bool enforceCheck) {
    float peak = 0.0f;
    for (float s : samples) {
        float absS = std::abs(s);
        if (absS > peak) peak = absS;
    }
    std::printf("capture peak: %.5f (floor: %.5f)\n", peak, floorLevel);
    if (enforceCheck && peak < floorLevel) {
        std::fprintf(stderr, "\n========================================================\n");
        std::fprintf(stderr, "WARNING: Capture peak (%.5f) is below floor (%.5f)!\n", peak, floorLevel);
        std::fprintf(stderr, "Host Windows recording level for M8 input may have reset.\n");
        std::fprintf(stderr, "Verify Windows Sound Settings -> M8 Input Volume is 100%%.\n");
        std::fprintf(stderr, "========================================================\n\n");
        return false;
    }
    return true;
}

#ifdef _WIN32
void ensureM8WindowsInputVolume100() {
    CoInitialize(nullptr);
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr) || !enumerator) return;

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(collection->Item(i, &dev)) && dev) {
                IPropertyStore* props = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))
                        && varName.vt == VT_LPWSTR && varName.pwszVal) {
                        if (std::wcsstr(varName.pwszVal, L"M8") != nullptr) {
                            IAudioEndpointVolume* epv = nullptr;
                            if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                                        nullptr, (void**)&epv)) && epv) {
                                float curLevel = 0.0f;
                                epv->GetMasterVolumeLevelScalar(&curLevel);
                                if (curLevel < 0.99f) {
                                    std::printf("Windows input volume for M8 was %.1f%%; setting to 100%%\n",
                                                curLevel * 100.0f);
                                    epv->SetMasterVolumeLevelScalar(1.0f, nullptr);
                                }
                                epv->Release();
                            }
                        }
                    }
                    PropVariantClear(&varName);
                    props->Release();
                }
                dev->Release();
            }
        }
        collection->Release();
    }
    enumerator->Release();
}
#else
void ensureM8WindowsInputVolume100() {}
#endif

} // namespace audio
} // namespace m8
