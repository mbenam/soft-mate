#include "RenderAudio.h"
#include "../engine/EngineEvents.h"
#include "../engine/SamplePool.h"
#include "dr_wav.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

namespace m8 {
namespace io {

namespace {

static bool writeWavFile(const std::string& path, const std::vector<float>& interleaved,
                         int channels, int sampleRate, bool is32BitFloat) {
    fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    const uint32_t nFrames = static_cast<uint32_t>(interleaved.size() / channels);
    const uint16_t bytesPerSample = is32BitFloat ? 4 : 2;
    const uint16_t formatTag = is32BitFloat ? 3 /* IEEE FLOAT */ : 1 /* PCM */;
    const uint32_t dataSize = nFrames * channels * bytesPerSample;
    const uint32_t riffSize = 36 + dataSize;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  u32(riffSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);
    u16(formatTag);
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * channels * bytesPerSample));
    u16(static_cast<uint16_t>(channels * bytesPerSample));
    u16(static_cast<uint16_t>(bytesPerSample * 8));
    std::fwrite("data", 1, 4, f);  u32(dataSize);

    if (is32BitFloat) {
        std::fwrite(interleaved.data(), sizeof(float), interleaved.size(), f);
    } else {
        for (float s : interleaved) {
            s = std::max(-1.0f, std::min(1.0f, s));
            int16_t v = static_cast<int16_t>(s * 32767.0f);
            std::fwrite(&v, 2, 1, f);
        }
    }
    std::fclose(f);
    return true;
}

// Find the last non-empty song row in the sequencer
static int computeAutoLastRow(const engine::Sequencer& seq) {
    for (int r = engine::Sequencer::SONG_ROWS - 1; r >= 0; --r) {
        for (int t = 0; t < 8; ++t) {
            if (seq.song[r].tracks[t] != engine::CHAIN_EMPTY) {
                return r;
            }
        }
    }
    return 0;
}

// Check if a track has any chain in the given row range
static bool trackHasContentInRange(const engine::Sequencer& seq, int track, int startRow, int endRow) {
    for (int r = startRow; r <= endRow && r < engine::Sequencer::SONG_ROWS; ++r) {
        if (seq.song[r].tracks[track] != engine::CHAIN_EMPTY) {
            return true;
        }
    }
    return false;
}

// Render a single pass (mixed or single-track solo)
static std::vector<float> renderEnginePass(const engine::Sequencer& inputSeq,
                                          const engine::EngineState& inputState,
                                          const RenderSettings& settings,
                                          int soloTrack /* -1 for all */,
                                          int startRow,
                                          int endRow,
                                          int repeats) {
    auto ring = std::make_unique<engine::CommandRing<engine::EngineCommand, 1024>>();
    auto engine = std::make_unique<engine::Engine>(*ring);

    // Prepare modified sequencer and engine state
    engine::Sequencer seq = inputSeq;
    engine::EngineState state = inputState;

    for (int t = 0; t < 8; ++t) {
        bool enabled = settings.trackEnabled[t];
        if (soloTrack >= 0 && t != soloTrack) enabled = false;

        if (!enabled) {
            for (int r = 0; r < engine::Sequencer::SONG_ROWS; ++r) {
                seq.song[r].tracks[t] = engine::CHAIN_EMPTY;
            }
            state.mixer.track_vol[t] = 0.0f;
        }
    }

    // Apply effect disables
    if (!settings.modfxEnabled) {
        state.effects.cho_mod_depth = 0.0f;
        state.mixer.cho_vol = 0.0f;
    }
    if (!settings.delayEnabled) {
        state.mixer.del_vol = 0.0f;
        state.effects.del_feedback = 0.0f;
    }
    if (!settings.reverbEnabled) {
        state.mixer.rev_vol = 0.0f;
    }
    if (!settings.limiterEnabled) {
        state.mixer.lim_val = 0.0f; // 0 = CLIP (or transparent)
    }
    if (!settings.mixEqEnabled) {
        state.mixer.djf_freq = 0.5f; // neutral
        state.mixer.djf_res = 0.0f;
    }

    // Push LOAD_SONG
    auto* songData = new engine::LoadedSongData{ seq, state };
    engine::EngineCommand loadCmd{};
    loadCmd.type = engine::CommandType::LOAD_SONG;
    loadCmd.u.song.data = songData;
    if (!ring->push(loadCmd)) {
        delete songData;
    }

    // Load and push samples for all INST_SAMPLER instruments
    std::vector<float*> loadedPcmPointers;
    std::vector<std::string> candidateRoots;
    if (!settings.sampleRoot.empty()) candidateRoots.push_back(settings.sampleRoot);
    candidateRoots.push_back("songs");
    candidateRoots.push_back("Songs");
    candidateRoots.push_back("samples");
    candidateRoots.push_back("Samples");
    candidateRoots.push_back("");
    candidateRoots.push_back("..");
    candidateRoots.push_back("../songs");
    candidateRoots.push_back("../../songs");
    candidateRoots.push_back("../../../songs");
    candidateRoots.push_back("../Samples");
    candidateRoots.push_back("../../Samples");
    candidateRoots.push_back("../../../Samples");
    candidateRoots.push_back("../samples");
    candidateRoots.push_back("../../samples");
    candidateRoots.push_back("../../../samples");

    for (int i = 0; i < 128; ++i) {
        if (state.instruments[i].type != engine::InstType::INST_SAMPLER) continue;
        const char* mpath = state.instruments[i].sampler.samplePath;
        if (mpath[0] == '\0') continue;
        std::string rel = mpath;
        if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel = rel.substr(1);

        unsigned int ch = 0, sr = 0;
        drwav_uint64 frames = 0;
        float* pcm = nullptr;

        for (const auto& root : candidateRoots) {
            std::filesystem::path p;
            if (root.empty()) p = rel;
            else p = std::filesystem::path(root) / rel;

            pcm = drwav_open_file_and_read_pcm_frames_f32(
                p.string().c_str(), &ch, &sr, &frames, NULL);
            if (pcm) break;
        }

        if (pcm) {
            loadedPcmPointers.push_back(pcm);
            engine::SampleData buf{};
            buf.data = pcm;
            buf.frames = static_cast<uint32_t>(frames);
            buf.channels = static_cast<uint8_t>(ch);
            buf.sampleRate = static_cast<uint32_t>(sr);
            std::strncpy(buf.path, mpath, sizeof(buf.path) - 1);
            buf.path[sizeof(buf.path) - 1] = '\0';

            engine::EngineCommand sampCmd{};
            sampCmd.type = engine::CommandType::LOAD_SAMPLE;
            sampCmd.targetId = i;
            sampCmd.u.sample = buf;
            ring->push(sampCmd);
        }
    }

    // Start playback
    engine::EngineCommand playCmd{};
    playCmd.type = engine::CommandType::PLAY_START;
    playCmd.targetId = startRow;
    playCmd.value = 3; // SONG mode
    ring->push(playCmd);

    std::vector<float> audio;
    constexpr int kChunk = 1024;
    std::vector<float> chunkBuf(kChunk * 2);

    int currentRow = startRow;
    int completedRepeats = 0;
    const int targetRepeats = std::max(0, repeats);

    // Max render guard (5 minutes at 48kHz = 14,400,000 frames)
    constexpr size_t kMaxFrames = 48000 * 300;
    size_t totalFrames = 0;
    bool reachedEnd = false;

    while (totalFrames < kMaxFrames && !reachedEnd) {
        engine->render(chunkBuf.data(), kChunk);
        audio.insert(audio.end(), chunkBuf.begin(), chunkBuf.end());
        totalFrames += kChunk;

        auto ph = engine->getPlayhead(0);
        int phRow = ph.songRow;

        // Detect wrap-around or row advance past endRow
        if (phRow != currentRow) {
            if (phRow < currentRow || phRow > endRow) {
                completedRepeats++;
                if (completedRepeats > targetRepeats) {
                    reachedEnd = true;
                }
            }
            currentRow = phRow;
        }

        if (ph.playMode == static_cast<uint8_t>(engine::PlayMode::NONE)) {
            reachedEnd = true;
        }
    }

    // Render tail decay (1.0 second = 48000 frames)
    constexpr int kTailFrames = 48000;
    int tailDone = 0;
    // Send PLAY_STOP so no new notes trigger during tail
    engine::EngineCommand stopCmd{};
    stopCmd.type = engine::CommandType::PLAY_STOP;
    ring->push(stopCmd);

    while (tailDone < kTailFrames) {
        int n = std::min(kChunk, kTailFrames - tailDone);
        engine->render(chunkBuf.data(), n);
        audio.insert(audio.end(), chunkBuf.begin(), chunkBuf.begin() + n * 2);
        tailDone += n;
    }

    // Clean up GC ring
    void* gcPtr = nullptr;
    while (engine->getSongGcRing().pop(gcPtr)) {
        delete static_cast<engine::LoadedSongData*>(gcPtr);
    }

    for (float* ptr : loadedPcmPointers) {
        drwav_free(ptr, NULL);
    }

    return audio;
}

} // namespace

RenderResult RenderSongAudio(const RenderSettings& settings,
                            const engine::Sequencer& uiSequencer,
                            const engine::EngineState& uiEngineState,
                            bool stemsMode,
                            const std::string& outputDirectory) {
    RenderResult res;

    // Clean name (strip dashes and trailing spaces)
    std::string baseName = settings.name;
    while (!baseName.empty() && (baseName.back() == '-' || baseName.back() == ' ')) {
        baseName.pop_back();
    }
    if (baseName.empty()) baseName = "RENDER";

    int startRow = std::clamp(settings.songRowStart, 0, engine::Sequencer::SONG_ROWS - 1);
    int endRow = settings.songRowLast;
    if (endRow < 0 || endRow >= engine::Sequencer::SONG_ROWS) {
        endRow = computeAutoLastRow(uiSequencer);
    }
    if (endRow < startRow) {
        endRow = startRow;
    }

    int repeats = std::clamp(settings.repeatCount, 0, 99);

    if (!stemsMode) {
        // MIXED render
        auto audio = renderEnginePass(uiSequencer, uiEngineState, settings, -1, startRow, endRow, repeats);
        std::string filePath = outputDirectory + "/" + baseName + ".wav";
        if (writeWavFile(filePath, audio, 2, 48000, settings.is32Bit)) {
            res.ok = true;
            res.outputFiles.push_back(filePath);
            res.totalFrames = audio.size() / 2;
        } else {
            res.ok = false;
            res.errorMsg = "CANNOT WRITE " + filePath;
        }
    } else {
        // STEMS render
        int renderedCount = 0;
        for (int t = 0; t < 8; ++t) {
            if (!settings.trackEnabled[t]) continue;
            if (!trackHasContentInRange(uiSequencer, t, startRow, endRow)) continue;

            auto audio = renderEnginePass(uiSequencer, uiEngineState, settings, t, startRow, endRow, repeats);
            std::string filePath = outputDirectory + "/" + baseName + "_T" + std::to_string(t + 1) + ".wav";
            if (writeWavFile(filePath, audio, 2, 48000, settings.is32Bit)) {
                res.outputFiles.push_back(filePath);
                res.totalFrames += audio.size() / 2;
                renderedCount++;
            }
        }

        if (renderedCount > 0) {
            res.ok = true;
        } else {
            res.ok = false;
            res.errorMsg = "NO ACTIVE TRACKS IN RANGE";
        }
    }

    return res;
}

} // namespace io
} // namespace m8
