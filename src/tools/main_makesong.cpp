// -----------------------------------------------------------------------------
// m8_makesong — writes the committed "opening" song (songs/opening.m8s) plus its
// drum samples (songs/samples/*.wav).
//
// The opening song is the in-code demo ("Night Drive"): sampler drums + MacroSynth
// (the placeholder saw) melodics. Its drum samples are generated in code, so to
// make a real, reloadable .m8s we (1) regenerate those four drums with the EXACT
// same DSP the engine uses and write them as WAVs, and (2) export the engine's
// full song state — instrument types, mods/envelopes, names, sample paths — via
// io::saveNewSong(). Reloading songs/opening.m8s with --sample-root songs then
// reproduces the demo.
//
// Regenerate with:  build\Release\m8_makesong.exe
// Play back with:   build\Release\m8_render.exe --load songs/opening.m8s \
//                       --sample-root songs --song --seconds 8 --out opening
// -----------------------------------------------------------------------------
#include "io/SongIO.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <filesystem>

using namespace m8;

static constexpr float kSR = 48000.0f;

// 16-bit mono PCM WAV writer.
static bool writeWavMono(const std::string& path, const std::vector<float>& mono, int sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, f); };
    const uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2;
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1);
    u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr) * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    for (float s : mono) {
        int v = static_cast<int>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
    std::fclose(f);
    return true;
}

// The four demo drums — DSP copied verbatim from Engine.h setupDemoSamples so the
// written WAVs are sample-identical to what the engine generates in memory.
static std::vector<float> genDrum(const char* which) {
    auto build = [](float seconds, float (*gen)(float t)) {
        const int frames = static_cast<int>(kSR * seconds);
        std::vector<float> buf(frames);
        for (int i = 0; i < frames; ++i) buf[i] = gen(static_cast<float>(i) / kSR);
        return buf;
    };
    if (!std::strcmp(which, "kick")) return build(0.28f, [](float t) {
        const float pitch = 55.0f + 160.0f * std::exp(-t * 32.0f);
        const float amp   = std::exp(-t * 9.0f);
        const float click = std::exp(-t * 400.0f) * 0.35f;
        float s = amp * std::sin(6.2831853f * pitch * t) + click;
        return std::tanh(s * 1.6f);
    });
    if (!std::strcmp(which, "snare")) return build(0.22f, [](float t) {
        uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 2654435761u; s ^= s >> 13;
        const float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
        const float body  = std::exp(-t * 30.0f) * std::sin(6.2831853f * 190.0f * t);
        const float crack = std::exp(-t * 18.0f) * n;
        return std::tanh((body * 0.6f + crack * 0.9f) * 1.3f);
    });
    if (!std::strcmp(which, "hat")) return build(0.09f, [](float t) {
        uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 2246822519u; s ^= s >> 11;
        float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
        uint32_t s2 = (static_cast<uint32_t>(t * 48000.0f) - 1) * 2246822519u; s2 ^= s2 >> 11;
        const float n2 = (static_cast<float>(s2 >> 8) / 8388608.0f) - 1.0f;
        n = (n - n2) * 0.5f;
        return n * std::exp(-t * 60.0f);
    });
    if (!std::strcmp(which, "clap")) return build(0.30f, [](float t) {
        uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 3266489917u; s ^= s >> 15;
        const float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
        float env = 0.0f;
        const float bursts[3] = { 0.000f, 0.011f, 0.022f };
        for (float b : bursts) if (t >= b) env = std::max(env, std::exp(-(t - b) * 260.0f));
        env = std::max(env, 0.55f * std::exp(-(t - 0.022f) * 22.0f) * (t > 0.022f ? 1.0f : 0.0f));
        return n * env * 0.9f;
    });
    return {};
}

int main(int argc, char** argv) {
    std::string outSong    = "songs/opening.m8s";
    std::string samplesDir = "songs/samples";
    std::string tmpl       = "third_party/m8-files-cxx/examples/songs/V4EMPTY.m8s";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]{ return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if      (a == "--out")      outSong    = next();
        else if (a == "--samples")  samplesDir = next();
        else if (a == "--template") tmpl       = next();
    }

    std::filesystem::create_directories(std::filesystem::path(outSong).parent_path());
    std::filesystem::create_directories(samplesDir);

    // 1. Write the drum samples.
    struct Drum { const char* key; const char* file; };
    const Drum drums[] = {{"kick","kick.wav"},{"snare","snare.wav"},
                          {"hat","hat.wav"},{"clap","clap.wav"}};
    for (const auto& d : drums) {
        auto buf = genDrum(d.key);
        std::string p = samplesDir + "/" + d.file;
        if (!writeWavMono(p, buf, 48000)) {
            std::fprintf(stderr, "FAIL: cannot write %s\n", p.c_str());
            return 1;
        }
        std::printf("  wrote %s  (%zu frames)\n", p.c_str(), buf.size());
    }

    // 2. Build the demo in the engine. Engine carries large inline DSP buffers
    //    (two 96k delay lines + reverb), so it must live on the heap, not the stack.
    engine::CommandRing<engine::EngineCommand, 1024> ring;
    auto engPtr = std::make_unique<engine::Engine>(ring);
    engine::Engine& eng = *engPtr;
    eng.loadDemoSong();
    auto& state = eng.getStateForInit();
    auto& seq   = eng.getSequencerForInit();

    // 3. Repoint the sampler instruments at the committed WAVs (M8-absolute
    //    paths; loadSong resolves them under --sample-root). Map by the demo's
    //    in-memory sample name.
    struct Remap { const char* from; const char* to; };
    const Remap remap[] = {
        {"demo_kick.wav",  "/samples/kick.wav"},
        {"demo_snare.wav", "/samples/snare.wav"},
        {"demo_hat.wav",   "/samples/hat.wav"},
        {"demo_clap.wav",  "/samples/clap.wav"},
    };
    for (auto& inst : state.instruments) {
        if (inst.type != engine::InstType::INST_SAMPLER) continue;
        for (const auto& rm : remap) {
            if (std::strcmp(inst.sampler.samplePath, rm.from) == 0) {
                std::strncpy(inst.sampler.samplePath, rm.to, sizeof(inst.sampler.samplePath) - 1);
                inst.sampler.samplePath[sizeof(inst.sampler.samplePath) - 1] = '\0';
            }
        }
    }

    // The .m8s format does not persist the global groove SELECTION — load always
    // forces project.groove = 0. The demo's swing lives in another slot, so move
    // the active groove into slot 0 (the slot the reloaded song will use) to keep
    // the swing feel.
    if (state.project.groove > 0 && state.project.groove < engine::Sequencer::NUM_GROOVES) {
        seq.grooves[0] = seq.grooves[state.project.groove];
        state.project.groove = 0;
    }

    // 4. Export to a real .m8s.
    std::string err;
    if (!io::saveNewSong(outSong, tmpl, seq, state, err)) {
        std::fprintf(stderr, "FAIL: saveNewSong: %s\n", err.c_str());
        return 1;
    }
    std::printf("  wrote %s\n", outSong.c_str());

    // 5. Verify the round-trip: reload and diff key state against the source.
    auto rt = io::loadSong(outSong, "songs");
    if (!rt.ok) { std::fprintf(stderr, "FAIL: reload: %s\n", rt.error.c_str()); return 1; }
    int mism = 0;
    auto& b = rt.state;
    auto chk = [&](const char* what, int a, int c) {
        if (a != c) { std::printf("  DIFF %-16s src=%3d reload=%3d\n", what, a, c); ++mism; }
    };
    chk("bpm", state.bpm, b.bpm);
    chk("out_vol", state.mixer.out_vol, b.mixer.out_vol);
    for (int t = 0; t < 8; ++t) chk("track_vol", state.mixer.track_vol[t], b.mixer.track_vol[t]);
    chk("rev_size", state.effects.rev_size, b.effects.rev_size);
    chk("del_feedback", state.effects.del_feedback, b.effects.del_feedback);
    chk("project.groove", state.project.groove, b.project.groove);
    for (int i = 0; i < 8; ++i) {
        const auto& ea = state.instruments[i];
        const auto& eb = b.instruments[i];
        if (ea.type == engine::InstType::INST_MACROSYN) {
            chk("mac.cutoff", ea.macrosyn.cutoff, eb.macrosyn.cutoff);
            chk("mac.amp",    ea.macrosyn.amp,    eb.macrosyn.amp);
        } else if (ea.type == engine::InstType::INST_SAMPLER) {
            chk("smp.amp",    ea.sampler.amp,     eb.sampler.amp);
            chk("smp.rev",    ea.sampler.rev,     eb.sampler.rev);
        }
        for (int k = 0; k < 4; ++k) {
            if (ea.mods[k].dest == 0 && eb.mods[k].dest == 0) continue; // both inactive
            chk("mod.dest", ea.mods[k].dest, eb.mods[k].dest);
            chk("mod.type", ea.mods[k].type, eb.mods[k].type);
            chk("mod.p1",   ea.mods[k].p1,   eb.mods[k].p1);
            chk("mod.p2",   ea.mods[k].p2,   eb.mods[k].p2);
            chk("mod.p3",   ea.mods[k].p3,   eb.mods[k].p3);
        }
    }
    std::printf(mism == 0 ? "  round-trip OK (state matches)\n"
                          : "  round-trip: %d field(s) differ\n", mism);

    std::printf("done. Play: m8_render --load %s --sample-root songs --song --seconds 8 --out opening\n",
                outSong.c_str());
    return 0;
}
