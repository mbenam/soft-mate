#include "io/SongIO.h"
#include "song.hpp"
#include "instruments.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <span>

namespace m8::io {

// ---- helpers ----

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    // Create the parent directory if it doesn't exist yet -- ofstream does
    // not do this itself, so a save to a not-yet-existing directory used to
    // fail silently (surfaced by save_reload.m8script saving to artifacts/).
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

// ---- FxCmd mapping ----
// Library: 0xFF=none, 0x00=vol, 0x01=pit, 0x02=del, 0x03=rev, 0x04=hop, 0x05=kil
// Engine:  NONE=0, VOL=1, PIT=2, DEL=3, REV=4, HOP=5, KIL=6

static engine::FxCmd libFxToEngine(uint8_t cmd) {
    if (cmd == 0xFF) return engine::FxCmd::NONE;
    if (cmd <= 0x05) return static_cast<engine::FxCmd>(cmd + 1);
    return engine::FxCmd::NONE;
}

static uint8_t engineFxToLib(engine::FxCmd cmd) {
    auto v = static_cast<uint8_t>(cmd);
    if (v == 0) return 0xFF;
    return v - 1;
}

// ---- Mod conversion ----

static void libModToEngine(const m8::Mod& libMod, engine::Modulator& engMod) {
    if (libMod.index() == 0) {
        engMod = {};
        return;
    }
    engMod.dest = 0;
    engMod.amt = 0x80;
    engMod.p1 = engMod.p2 = engMod.p3 = engMod.p4 = 0;

    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, m8::AHDEnv>) {
            engMod.type = 0;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = m.attack;
            engMod.p2 = m.hold;
            engMod.p3 = m.decay;
        } else if constexpr (std::is_same_v<T, m8::ADSREnv>) {
            engMod.type = 1;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = m.attack;
            engMod.p2 = m.decay;
            engMod.p3 = m.sustain;
            engMod.p4 = m.release;
        } else if constexpr (std::is_same_v<T, m8::DrumEnv>) {
            engMod.type = 2;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = m.peak;
            engMod.p2 = m.body;
            engMod.p3 = m.decay;
        } else if constexpr (std::is_same_v<T, m8::LFO>) {
            engMod.type = 3;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = static_cast<uint8_t>(m.shape);
            engMod.p2 = m.trigger_mode;
            engMod.p3 = m.freq;
            engMod.p4 = m.retrigger;
        } else if constexpr (std::is_same_v<T, m8::TrigEnv>) {
            engMod.type = 4;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = m.attack;
            engMod.p2 = m.hold;
            engMod.p3 = m.decay;
            engMod.p4 = m.src;
        } else if constexpr (std::is_same_v<T, m8::TrackingEnv>) {
            engMod.type = 5;
            engMod.dest = m.dest;
            engMod.amt = m.amount;
            engMod.p1 = m.src;
            engMod.p2 = m.lval;
            engMod.p3 = m.hval;
        }
    }, libMod);
}

static m8::Mod engineModToLib(const engine::Modulator& engMod) {
    if (engMod.dest == 0 && engMod.type == 0) return m8::Mod();

    switch (engMod.type) {
    case 0: {
        m8::AHDEnv m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.attack = engMod.p1; m.hold = engMod.p2; m.decay = engMod.p3;
        return m;
    }
    case 1: {
        m8::ADSREnv m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.attack = engMod.p1; m.decay = engMod.p2;
        m.sustain = engMod.p3; m.release = engMod.p4;
        return m;
    }
    case 2: {
        m8::DrumEnv m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.peak = engMod.p1; m.body = engMod.p2; m.decay = engMod.p3;
        return m;
    }
    case 3: {
        m8::LFO m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.shape = static_cast<m8::LfoShape>(engMod.p1);
        m.trigger_mode = engMod.p2; m.freq = engMod.p3;
        m.retrigger = engMod.p4;
        return m;
    }
    case 4: {
        m8::TrigEnv m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.attack = engMod.p1; m.hold = engMod.p2; m.decay = engMod.p3;
        m.src = engMod.p4;
        return m;
    }
    case 5: {
        m8::TrackingEnv m;
        m.dest = engMod.dest; m.amount = engMod.amt;
        m.src = engMod.p1; m.lval = engMod.p2; m.hval = engMod.p3;
        return m;
    }
    default:
        return m8::Mod();
    }
}

// ---- SynthParams ↔ engine instrument fields ----

static void libSynthParamsToEngine(const m8::SynthParams& sp,
                                    engine::SamplerState& s,
                                    engine::Modulator* mods) {
    s.amp = sp.volume;
    s.filter_type = sp.filter_type;
    s.cutoff = sp.filter_cutoff;
    s.res = sp.filter_res;
    s.lim = sp.amp_type;
    s.pan = sp.mixer_pan;
    s.dry = sp.mixer_dry;
    s.cho = sp.mixer_chorus;
    s.del = sp.mixer_delay;
    s.rev = sp.mixer_reverb;
    for (int i = 0; i < 4; ++i)
        libModToEngine(sp.mods[i], mods[i]);
}

static void libSynthParamsToMacrosyn(const m8::SynthParams& sp,
                                      engine::MacrosynState& m) {
    m.amp = sp.volume;
    m.filter_type = sp.filter_type;
    m.cutoff = sp.filter_cutoff;
    m.res = sp.filter_res;
    m.lim = sp.amp_type;
    m.pan = sp.mixer_pan;
    m.dry = sp.mixer_dry;
    m.cho = sp.mixer_chorus;
    m.del = sp.mixer_delay;
    m.rev = sp.mixer_reverb;
}

static void engineSamplerToLibSynthParams(const engine::SamplerState& s,
                                           m8::SynthParams& sp) {
    sp.volume = s.amp;
    sp.filter_type = s.filter_type;
    sp.filter_cutoff = s.cutoff;
    sp.filter_res = s.res;
    sp.amp_type = s.lim;
    sp.mixer_pan = s.pan;
    sp.mixer_dry = s.dry;
    sp.mixer_chorus = s.cho;
    sp.mixer_delay = s.del;
    sp.mixer_reverb = s.rev;
}

static void engineMacrosynToLibSynthParams(const engine::MacrosynState& ms,
                                            m8::SynthParams& sp) {
    sp.volume = ms.amp;
    sp.filter_type = ms.filter_type;
    sp.filter_cutoff = ms.cutoff;
    sp.filter_res = ms.res;
    sp.amp_type = ms.lim;
    sp.mixer_pan = ms.pan;
    sp.mixer_dry = ms.dry;
    sp.mixer_chorus = ms.cho;
    sp.mixer_delay = ms.del;
    sp.mixer_reverb = ms.rev;
}

// ---- Groove length derivation ----

static uint8_t grooveLength(const m8::Groove& g) {
    for (int i = 0; i < 16; ++i)
        if (g.steps[i] == 0xFF) return static_cast<uint8_t>(i);
    return 16;
}

// ---- Main conversion: library → engine ----

static void convertSongToEngine(const m8::Song& song,
                                 engine::Sequencer& seq,
                                 engine::EngineState& state) {
    seq = engine::Sequencer{};

    // Phrases (library: 0..254, engine: 0..254, index 255 = empty sentinel)
    for (size_t p = 0; p < song.phrases.size() && p < 255; ++p) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = song.phrases[p].steps[r];
            auto& dst = seq.phrases[p][r];
            dst.note = src.note.value;
            dst.vol = src.velocity;
            dst.instr = src.instrument;
            dst.fx[0] = {libFxToEngine(src.fx1.command), src.fx1.value};
            dst.fx[1] = {libFxToEngine(src.fx2.command), src.fx2.value};
            dst.fx[2] = {libFxToEngine(src.fx3.command), src.fx3.value};
        }
    }

    // Chains (library: 0..254, engine: 0..254)
    for (size_t c = 0; c < song.chains.size() && c < 255; ++c) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = song.chains[c].steps[r];
            auto& dst = seq.chains[c][r];
            dst.phrase = src.phrase;
            dst.tsp = static_cast<int8_t>(src.transpose); // same bits
        }
    }

    // Song steps — library flat array is row-major: steps[row * 8 + track]
    for (int row = 0; row < 256; ++row) {
        for (int t = 0; t < 8; ++t) {
            seq.song[row].tracks[t] = song.song.steps[row * 8 + t];
        }
    }

    // Grooves
    for (size_t g = 0; g < song.grooves.size() && g < 32; ++g) {
        for (int i = 0; i < 16; ++i)
            seq.grooves[g].steps[i] = song.grooves[g].steps[i];
        seq.grooves[g].length = grooveLength(song.grooves[g]);
    }

    // Project
    engine::setName(state.project.name, song.name.c_str());
    state.project.transpose = song.transpose;
    state.project.scale = 0;
    state.project.groove = 0;

    // Tempo
    float t = song.tempo;
    state.bpm = static_cast<int>(t);
    state.bpm_frac = static_cast<int>(std::round((t - state.bpm) * 100.0f));

    // Mixer
    state.mixer.out_vol = song.mixer_settings.master_volume;
    for (int i = 0; i < 8; ++i)
        state.mixer.track_vol[i] = song.mixer_settings.track_volume[i];
    state.mixer.cho_vol = song.mixer_settings.chorus_volume;
    state.mixer.del_vol = song.mixer_settings.delay_volume;
    state.mixer.rev_vol = song.mixer_settings.reverb_volume;
    state.mixer.lim_val = song.mixer_settings.master_limit;
    state.mixer.djf_freq = song.mixer_settings.dj_filter;
    state.mixer.djf_res = song.mixer_settings.dj_peak;
    state.mixer.djf_typ = song.mixer_settings.dj_filter_type;

    // Effects
    auto& fx = state.effects;
    fx.cho_mod_depth = song.effects_settings.chorus_mod_depth;
    fx.cho_mod_freq = song.effects_settings.chorus_mod_freq;
    fx.cho_width = song.effects_settings.chorus_reverb_send;
    fx.del_time_l = song.effects_settings.delay_time_l;
    fx.del_time_r = song.effects_settings.delay_time_r;
    fx.del_feedback = song.effects_settings.delay_feedback;
    fx.del_width = song.effects_settings.delay_width;
    fx.del_reverb = song.effects_settings.delay_reverb_send;
    fx.rev_size = song.effects_settings.reverb_size;
    fx.rev_decay = song.effects_settings.reverb_damping;
    fx.rev_mod_depth = song.effects_settings.reverb_mod_depth;
    fx.rev_mod_freq = song.effects_settings.reverb_mod_freq;
    fx.rev_width = song.effects_settings.reverb_width;

    // Instruments
    for (size_t i = 0; i < song.instruments.size() && i < 128; ++i) {
        const auto& libInst = song.instruments[i];
        auto& engInst = state.instruments[i];

        std::visit([&](const auto& inst) {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                engInst.type = engine::InstType::INST_NONE;
            }
            else if constexpr (std::is_same_v<T, m8::Sampler>) {
                engInst.type = engine::InstType::INST_SAMPLER;
                engine::setName(engInst.name, inst.name.c_str());
                auto& s = engInst.sampler;
                std::strncpy(s.samplePath, inst.sample_path.c_str(), sizeof(s.samplePath) - 1);
                s.transp  = inst.transpose ? 1 : 0;
                s.tbl_tic = inst.table_tick;
                // DETUNE: file fine_pitch is signed (0 == centre); engine detune is
                // unsigned (0x80 == centre). Convert with a signed re-centre.
                s.detune  = static_cast<int>(static_cast<int8_t>(inst.synth_params.fine_pitch)) + 0x80;
                s.play = inst.play_mode;
                s.slice = inst.slice;
                s.start = inst.start;
                s.loop_st = inst.loop_start;
                s.length = inst.length;
                s.degrade = inst.degrade;
                libSynthParamsToEngine(inst.synth_params, s, engInst.mods);
            }
            else if constexpr (std::is_same_v<T, m8::MacroSynth>) {
                engInst.type = engine::InstType::INST_MACROSYN;
                engine::setName(engInst.name, inst.name.c_str());
                auto& ms = engInst.macrosyn;
                ms.transp  = inst.transpose ? 1 : 0;
                ms.tbl_tic = inst.table_tick;
                ms.shape = inst.shape;
                ms.timbre = inst.timbre;
                ms.color = inst.color;
                ms.amp = inst.synth_params.volume;
                ms.filter_type = inst.synth_params.filter_type;
                ms.cutoff = inst.synth_params.filter_cutoff;
                ms.res = inst.synth_params.filter_res;
                ms.lim = inst.synth_params.amp_type;
                ms.pan = inst.synth_params.mixer_pan;
                ms.dry = inst.synth_params.mixer_dry;
                ms.cho = inst.synth_params.mixer_chorus;
                ms.del = inst.synth_params.mixer_delay;
                ms.rev = inst.synth_params.mixer_reverb;
                ms.degrade = inst.degrade;
                ms.redux = inst.reductor;
                for (int m = 0; m < 4; ++m)
                    libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
            }
            else if constexpr (std::is_same_v<T, m8::HyperSynth>) {
                engInst.type = engine::InstType::INST_HYPERSYN;
                engine::setName(engInst.name, inst.name.c_str());
                auto& h = engInst.hyper;
                h.transp  = inst.transpose ? 1 : 0;
                h.tbl_tic = inst.table_tick;
                h.scale = inst.scale;
                h.shift = inst.shift;
                h.swarm = inst.swarm;
                h.width = inst.width;
                h.subosc = inst.subosc;
                for (int c = 0; c < 7; ++c)
                    h.default_chord[c] = inst.default_chord[c];
                for (int s = 0; s < 16; ++s)
                    for (int n = 0; n < 6; ++n)
                        h.chords[s][n] = inst.chords[s][n];
                h.amp = inst.synth_params.volume;
                h.filter_type = inst.synth_params.filter_type;
                h.cutoff = inst.synth_params.filter_cutoff;
                h.res = inst.synth_params.filter_res;
                h.lim = inst.synth_params.amp_type;
                h.pan = inst.synth_params.mixer_pan;
                h.dry = inst.synth_params.mixer_dry;
                h.cho = inst.synth_params.mixer_chorus;
                h.del = inst.synth_params.mixer_delay;
                h.rev = inst.synth_params.mixer_reverb;
                for (int m = 0; m < 4; ++m)
                    libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
            }
            else if constexpr (std::is_same_v<T, m8::FMSynth>) {
                engInst.type = engine::InstType::INST_FMSYNTH;
                engine::setName(engInst.name, inst.name.c_str());
                auto& fm = engInst.fm;
                fm.transp  = inst.transpose ? 1 : 0;
                fm.tbl_tic = inst.table_tick;
                fm.algo    = static_cast<int>(inst.algo);
                for (int i = 0; i < 4; ++i) {
                    fm.ops[i].shape      = static_cast<int>(inst.operators[i].shape);
                    fm.ops[i].ratio      = inst.operators[i].ratio;
                    fm.ops[i].ratio_fine = inst.operators[i].ratio_fine;
                    fm.ops[i].level      = inst.operators[i].level;
                    fm.ops[i].feedback   = inst.operators[i].feedback;
                    fm.ops[i].retrigger  = inst.operators[i].retrigger;
                    fm.ops[i].mod_a      = inst.operators[i].mod_a;
                    fm.ops[i].mod_b      = inst.operators[i].mod_b;
                }
                fm.mod1 = inst.mod1;
                fm.mod2 = inst.mod2;
                fm.mod3 = inst.mod3;
                fm.mod4 = inst.mod4;
                fm.amp         = inst.synth_params.volume;
                fm.filter_type = inst.synth_params.filter_type;
                fm.cutoff      = inst.synth_params.filter_cutoff;
                fm.res         = inst.synth_params.filter_res;
                fm.lim         = inst.synth_params.amp_type;
                fm.pan         = inst.synth_params.mixer_pan;
                fm.dry         = inst.synth_params.mixer_dry;
                fm.cho         = inst.synth_params.mixer_chorus;
                fm.del         = inst.synth_params.mixer_delay;
                fm.rev         = inst.synth_params.mixer_reverb;
                for (int m = 0; m < 4; ++m)
                    libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
            }
            else if constexpr (std::is_same_v<T, m8::WavSynth>) {
                engInst.type = engine::InstType::INST_WAVSYNTH;
                engine::setName(engInst.name, inst.name.c_str());
                auto& ws = engInst.wav;
                ws.transp  = inst.transpose ? 1 : 0;
                ws.tbl_tic = inst.table_tick;
                ws.shape   = static_cast<int>(inst.shape);
                ws.size    = inst.size;
                ws.mult    = inst.mult;
                ws.warp    = inst.warp;
                ws.scan    = inst.scan;
                ws.amp         = inst.synth_params.volume;
                ws.filter_type = inst.synth_params.filter_type;
                ws.cutoff      = inst.synth_params.filter_cutoff;
                ws.res         = inst.synth_params.filter_res;
                ws.lim         = inst.synth_params.amp_type;
                ws.pan         = inst.synth_params.mixer_pan;
                ws.dry         = inst.synth_params.mixer_dry;
                ws.cho         = inst.synth_params.mixer_chorus;
                ws.del         = inst.synth_params.mixer_delay;
                ws.rev         = inst.synth_params.mixer_reverb;
                for (int m = 0; m < 4; ++m)
                    libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
            }
            else {
                // Unimplemented types (MIDIOut, External)
                engInst.type = engine::InstType::INST_NONE;
                engine::setName(engInst.name, inst.name.c_str());
            }
        }, libInst);
    }
}

// ---- Main conversion: engine → library (for save) ----

static void convertEngineToSong(const engine::Sequencer& seq,
                                 const engine::EngineState& state,
                                 m8::Song& song) {
    // Phrases
    song.phrases.resize(m8::Song::N_PHRASES);
    for (size_t p = 0; p < m8::Song::N_PHRASES; ++p) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = seq.phrases[p][r];
            auto& dst = song.phrases[p].steps[r];
            dst.note.value = src.note;
            dst.velocity = src.vol;
            dst.instrument = src.instr;
            dst.fx1 = {engineFxToLib(src.fx[0].cmd), src.fx[0].val};
            dst.fx2 = {engineFxToLib(src.fx[1].cmd), src.fx[1].val};
            dst.fx3 = {engineFxToLib(src.fx[2].cmd), src.fx[2].val};
        }
    }

    // Chains
    song.chains.resize(m8::Song::N_CHAINS);
    for (size_t c = 0; c < m8::Song::N_CHAINS; ++c) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = seq.chains[c][r];
            auto& dst = song.chains[c].steps[r];
            dst.phrase = src.phrase;
            dst.transpose = static_cast<uint8_t>(src.tsp); // same bits
        }
    }

    // Song steps
    for (int row = 0; row < 256; ++row)
        for (int t = 0; t < 8; ++t)
            song.song.steps[row * 8 + t] = seq.song[row].tracks[t];

    // Grooves
    song.grooves.resize(m8::Song::N_GROOVES);
    for (size_t g = 0; g < m8::Song::N_GROOVES; ++g) {
        song.grooves[g].number = static_cast<uint8_t>(g);
        for (int i = 0; i < 16; ++i)
            song.grooves[g].steps[i] = seq.grooves[g].steps[i];
    }

    // Tempo
    song.tempo = static_cast<float>(state.bpm)
               + static_cast<float>(state.bpm_frac) / 100.0f;

    // Mixer
    song.mixer_settings.master_volume = state.mixer.out_vol;
    for (int i = 0; i < 8; ++i)
        song.mixer_settings.track_volume[i] = state.mixer.track_vol[i];
    song.mixer_settings.chorus_volume = state.mixer.cho_vol;
    song.mixer_settings.delay_volume = state.mixer.del_vol;
    song.mixer_settings.reverb_volume = state.mixer.rev_vol;
    song.mixer_settings.master_limit = state.mixer.lim_val;
    song.mixer_settings.dj_filter = state.mixer.djf_freq;
    song.mixer_settings.dj_peak = state.mixer.djf_res;
    song.mixer_settings.dj_filter_type = state.mixer.djf_typ;

    // Effects
    auto& fx = song.effects_settings;
    fx.chorus_mod_depth = state.effects.cho_mod_depth;
    fx.chorus_mod_freq = state.effects.cho_mod_freq;
    fx.chorus_reverb_send = state.effects.cho_width;
    fx.delay_time_l = state.effects.del_time_l;
    fx.delay_time_r = state.effects.del_time_r;
    fx.delay_feedback = state.effects.del_feedback;
    fx.delay_width = state.effects.del_width;
    fx.delay_reverb_send = state.effects.del_reverb;
    fx.reverb_size = state.effects.rev_size;
    fx.reverb_damping = state.effects.rev_decay;
    fx.reverb_mod_depth = state.effects.rev_mod_depth;
    fx.reverb_mod_freq = state.effects.rev_mod_freq;
    fx.reverb_width = state.effects.rev_width;

    // Instruments — overlay the fields our engine models onto the ORIGINAL song
    // instruments. We only touch modeled/screen-exposed fields; every other byte
    // (pitch, amp_limit, env_*_amt, lfo_*_amt, mods, associated_eq, sample_path,
    // number) is preserved from the file that was re-read at the start of saveSong().
    // That preservation is what keeps the byte-identical round-trip test passing.
    for (size_t i = 0; i < song.instruments.size() && i < 128; ++i) {
        const auto& engInst = state.instruments[i];

        if (engInst.type == engine::InstType::INST_SAMPLER &&
            std::holds_alternative<m8::Sampler>(song.instruments[i])) {
            auto& smp = std::get<m8::Sampler>(song.instruments[i]);
            const auto& s = engInst.sampler;
            smp.transpose  = (s.transp != 0);
            smp.table_tick = static_cast<uint8_t>(s.tbl_tic);
            smp.play_mode  = static_cast<uint8_t>(s.play);
            smp.slice      = static_cast<uint8_t>(s.slice);
            smp.start      = static_cast<uint8_t>(s.start);
            smp.loop_start = static_cast<uint8_t>(s.loop_st);
            smp.length     = static_cast<uint8_t>(s.length);
            smp.degrade    = static_cast<uint8_t>(s.degrade);
            engineSamplerToLibSynthParams(s, smp.synth_params); // volume/filter/lim/pan/dry/sends
            // DETUNE: engine detune is unsigned with 0x80 == centre; the file's
            // fine_pitch is SIGNED with 0 == centre. See the WARNING in §1.3.
            smp.synth_params.fine_pitch = static_cast<uint8_t>(s.detune - 0x80);
        }
        else if (engInst.type == engine::InstType::INST_MACROSYN &&
                 std::holds_alternative<m8::MacroSynth>(song.instruments[i])) {
            auto& mac = std::get<m8::MacroSynth>(song.instruments[i]);
            const auto& m = engInst.macrosyn;
            mac.transpose  = (m.transp != 0);
            mac.table_tick = static_cast<uint8_t>(m.tbl_tic);
            mac.shape      = static_cast<uint8_t>(m.shape);
            mac.timbre     = static_cast<uint8_t>(m.timbre);
            mac.color      = static_cast<uint8_t>(m.color);
            mac.degrade    = static_cast<uint8_t>(m.degrade);
            mac.reductor   = static_cast<uint8_t>(m.redux);
            engineMacrosynToLibSynthParams(m, mac.synth_params);
        }
        else if (engInst.type == engine::InstType::INST_HYPERSYN &&
                 std::holds_alternative<m8::HyperSynth>(song.instruments[i])) {
            auto& hyp = std::get<m8::HyperSynth>(song.instruments[i]);
            const auto& h = engInst.hyper;
            hyp.transpose  = (h.transp != 0);
            hyp.table_tick = static_cast<uint8_t>(h.tbl_tic);
            hyp.scale      = static_cast<uint8_t>(h.scale);
            hyp.shift      = static_cast<uint8_t>(h.shift);
            hyp.swarm      = static_cast<uint8_t>(h.swarm);
            hyp.width      = static_cast<uint8_t>(h.width);
            hyp.subosc     = static_cast<uint8_t>(h.subosc);
            for (int c = 0; c < 7; ++c)
                hyp.default_chord[c] = static_cast<uint8_t>(h.default_chord[c]);
            for (int s = 0; s < 16; ++s)
                for (int n = 0; n < 6; ++n)
                    hyp.chords[s][n] = static_cast<uint8_t>(h.chords[s][n]);
            hyp.synth_params.volume = static_cast<uint8_t>(h.amp);
            hyp.synth_params.filter_type = static_cast<uint8_t>(h.filter_type);
            hyp.synth_params.filter_cutoff = static_cast<uint8_t>(h.cutoff);
            hyp.synth_params.filter_res = static_cast<uint8_t>(h.res);
            hyp.synth_params.amp_type = static_cast<uint8_t>(h.lim);
            hyp.synth_params.mixer_pan = static_cast<uint8_t>(h.pan);
            hyp.synth_params.mixer_dry = static_cast<uint8_t>(h.dry);
            hyp.synth_params.mixer_chorus = static_cast<uint8_t>(h.cho);
            hyp.synth_params.mixer_delay = static_cast<uint8_t>(h.del);
            hyp.synth_params.mixer_reverb = static_cast<uint8_t>(h.rev);
            for (int m = 0; m < 4; ++m)
                hyp.synth_params.mods[m] = engineModToLib(engInst.mods[m]);
        }
        else if (engInst.type == engine::InstType::INST_FMSYNTH &&
                 std::holds_alternative<m8::FMSynth>(song.instruments[i])) {
            auto& fms = std::get<m8::FMSynth>(song.instruments[i]);
            const auto& fm = engInst.fm;
            fms.transpose  = (fm.transp != 0);
            fms.table_tick = static_cast<uint8_t>(fm.tbl_tic);
            fms.algo       = static_cast<m8::FmAlgo>(fm.algo);
            for (int k = 0; k < 4; ++k) {
                fms.operators[k].shape      = static_cast<m8::FMWave>(fm.ops[k].shape);
                fms.operators[k].ratio      = static_cast<uint8_t>(fm.ops[k].ratio);
                fms.operators[k].ratio_fine = static_cast<uint8_t>(fm.ops[k].ratio_fine);
                fms.operators[k].level      = static_cast<uint8_t>(fm.ops[k].level);
                fms.operators[k].feedback   = static_cast<uint8_t>(fm.ops[k].feedback);
                fms.operators[k].retrigger  = static_cast<uint8_t>(fm.ops[k].retrigger);
                fms.operators[k].mod_a      = static_cast<uint8_t>(fm.ops[k].mod_a);
                fms.operators[k].mod_b      = static_cast<uint8_t>(fm.ops[k].mod_b);
            }
            fms.mod1 = static_cast<uint8_t>(fm.mod1);
            fms.mod2 = static_cast<uint8_t>(fm.mod2);
            fms.mod3 = static_cast<uint8_t>(fm.mod3);
            fms.mod4 = static_cast<uint8_t>(fm.mod4);
            fms.synth_params.volume        = static_cast<uint8_t>(fm.amp);
            fms.synth_params.filter_type   = static_cast<uint8_t>(fm.filter_type);
            fms.synth_params.filter_cutoff = static_cast<uint8_t>(fm.cutoff);
            fms.synth_params.filter_res    = static_cast<uint8_t>(fm.res);
            fms.synth_params.amp_type      = static_cast<uint8_t>(fm.lim);
            fms.synth_params.mixer_pan     = static_cast<uint8_t>(fm.pan);
            fms.synth_params.mixer_dry     = static_cast<uint8_t>(fm.dry);
            fms.synth_params.mixer_chorus  = static_cast<uint8_t>(fm.cho);
            fms.synth_params.mixer_delay   = static_cast<uint8_t>(fm.del);
            fms.synth_params.mixer_reverb  = static_cast<uint8_t>(fm.rev);
            for (int m = 0; m < 4; ++m)
                fms.synth_params.mods[m] = engineModToLib(engInst.mods[m]);
        }
        else if (engInst.type == engine::InstType::INST_WAVSYNTH &&
                 std::holds_alternative<m8::WavSynth>(song.instruments[i])) {
            auto& wvs = std::get<m8::WavSynth>(song.instruments[i]);
            const auto& ws = engInst.wav;
            wvs.transpose  = (ws.transp != 0);
            wvs.table_tick = static_cast<uint8_t>(ws.tbl_tic);
            wvs.shape      = static_cast<m8::WavShape>(ws.shape);
            wvs.size       = static_cast<uint8_t>(ws.size);
            wvs.mult       = static_cast<uint8_t>(ws.mult);
            wvs.warp       = static_cast<uint8_t>(ws.warp);
            wvs.scan       = static_cast<uint8_t>(ws.scan);
            wvs.synth_params.volume        = static_cast<uint8_t>(ws.amp);
            wvs.synth_params.filter_type   = static_cast<uint8_t>(ws.filter_type);
            wvs.synth_params.filter_cutoff = static_cast<uint8_t>(ws.cutoff);
            wvs.synth_params.filter_res    = static_cast<uint8_t>(ws.res);
            wvs.synth_params.amp_type      = static_cast<uint8_t>(ws.lim);
            wvs.synth_params.mixer_pan     = static_cast<uint8_t>(ws.pan);
            wvs.synth_params.mixer_dry     = static_cast<uint8_t>(ws.dry);
            wvs.synth_params.mixer_chorus  = static_cast<uint8_t>(ws.cho);
            wvs.synth_params.mixer_delay   = static_cast<uint8_t>(ws.del);
            wvs.synth_params.mixer_reverb  = static_cast<uint8_t>(ws.rev);
            for (int m = 0; m < 4; ++m)
                wvs.synth_params.mods[m] = engineModToLib(engInst.mods[m]);
        }
        // Any other case (INST_NONE, or engine type != library type): leave the
        // original instrument bytes untouched.
    }
}

// Build a full m8::Song from engine state, starting from `base` (a valid V4+
// song parsed from a template). Pre-creates each instrument variant to match the
// engine's type and fills the fields convertEngineToSong does NOT write (name,
// sample_path, mods/envelopes); convertEngineToSong then overlays the screen
// params (incl. fine_pitch) and the whole sequencer. This is how a song built
// only in the engine — with no source file — becomes a real, reloadable .m8s.
static m8::Song buildSongFromEngine(const engine::Sequencer& seq,
                                    const engine::EngineState& state,
                                    m8::Song base) {
    auto trimName = [](const char* n) {
        std::string s(n);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
        return s;
    };

    base.instruments.resize(m8::Song::N_INSTRUMENTS);
    for (size_t i = 0; i < base.instruments.size(); ++i) {
        if (i >= state.instruments.size()) { base.instruments[i] = std::monostate{}; continue; }
        const auto& e = state.instruments[i];

        if (e.type == engine::InstType::INST_SAMPLER) {
            m8::Sampler smp{};
            smp.number = static_cast<uint8_t>(i);
            smp.name = trimName(e.name);
            smp.sample_path = e.sampler.samplePath;   // the engine's own record
            smp.synth_params = {};
            smp.synth_params.mixer_pan = 0x80;
            for (int k = 0; k < 4; ++k)
                smp.synth_params.mods[k] = engineModToLib(e.mods[k]);
            smp.synth_params.associated_eq = 0xFF;
            base.instruments[i] = smp;                // screen params overlaid below
        } else if (e.type == engine::InstType::INST_MACROSYN) {
            m8::MacroSynth ms{};
            ms.number = static_cast<uint8_t>(i);
            ms.name = trimName(e.name);
            ms.synth_params = {};
            ms.synth_params.mixer_pan = 0x80;
            for (int k = 0; k < 4; ++k)
                ms.synth_params.mods[k] = engineModToLib(e.mods[k]);
            ms.synth_params.associated_eq = 0xFF;
            base.instruments[i] = ms;
        } else if (e.type == engine::InstType::INST_HYPERSYN) {
            m8::HyperSynth hyp{};
            hyp.number = static_cast<uint8_t>(i);
            hyp.name = trimName(e.name);
            hyp.synth_params = {};
            hyp.synth_params.mixer_pan = 0x80;
            for (int k = 0; k < 4; ++k)
                hyp.synth_params.mods[k] = engineModToLib(e.mods[k]);
            hyp.synth_params.associated_eq = 0xFF;
            base.instruments[i] = hyp;
        } else if (e.type == engine::InstType::INST_FMSYNTH) {
            m8::FMSynth fms{};
            fms.number = static_cast<uint8_t>(i);
            fms.name = trimName(e.name);
            fms.algo = m8::FmAlgo::Algo0;
            for (int k = 0; k < 4; ++k) {
                fms.operators[k].shape = m8::FMWave::Sin;
                fms.operators[k].ratio = 1;
                fms.operators[k].ratio_fine = 0;
                fms.operators[k].level = 0xFF;
                fms.operators[k].feedback = 0;
                fms.operators[k].retrigger = 1;
                fms.operators[k].mod_a = 0;
                fms.operators[k].mod_b = 0;
            }
            fms.mod1 = 0x80; fms.mod2 = 0x80; fms.mod3 = 0x80; fms.mod4 = 0x80;
            fms.synth_params = {};
            fms.synth_params.mixer_pan = 0x80;
            for (int k = 0; k < 4; ++k)
                fms.synth_params.mods[k] = engineModToLib(e.mods[k]);
            fms.synth_params.associated_eq = 0xFF;
            base.instruments[i] = fms;
        } else if (e.type == engine::InstType::INST_WAVSYNTH) {
            m8::WavSynth wvs{};
            wvs.number = static_cast<uint8_t>(i);
            wvs.name = trimName(e.name);
            wvs.shape = m8::WavShape::Sine;
            wvs.size = 0x80;
            wvs.mult = 0x00;
            wvs.warp = 0x80;
            wvs.scan = 0x00;
            wvs.synth_params = {};
            wvs.synth_params.mixer_pan = 0x80;
            for (int k = 0; k < 4; ++k)
                wvs.synth_params.mods[k] = engineModToLib(e.mods[k]);
            wvs.synth_params.associated_eq = 0xFF;
            base.instruments[i] = wvs;
        } else {
            base.instruments[i] = std::monostate{};
        }
    }

    // Project header fields convertEngineToSong doesn't set (they live in the
    // file header, not the data sections). loadSong reads these back into
    // project.name / project.transpose, so set them for the round-trip.
    std::string nm(state.project.name);
    while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\0')) nm.pop_back();
    base.name = nm;
    base.transpose = static_cast<uint8_t>(state.project.transpose);

    // Overlays sequencer + instrument screen params (play/slice/start/loop/
    // length/degrade/transpose/table_tick/synth-params/fine_pitch). Leaves the
    // name/sample_path/mods we set above intact.
    convertEngineToSong(seq, state, base);
    return base;
}

// ---- Public API ----

LoadResult loadSong(const std::string& path, const std::string& sampleRoot) {
    LoadResult res;
    try {
        auto data = readFile(path);
        if (data.empty()) { res.error = "cannot read file"; return res; }

        m8::BinaryReader r(data);
        m8::Song song = m8::Song::from_reader(r);

        res.original = data;
        res.writable = song.version.at_least(4, 0);

        convertSongToEngine(song, res.sequencer, res.state);

        // Collect sample paths
        for (size_t i = 0; i < song.instruments.size(); ++i) {
            std::visit([&](const auto& inst) {
                using T = std::decay_t<decltype(inst)>;
                if constexpr (std::is_same_v<T, m8::Sampler>) {
                    if (!inst.sample_path.empty())
                        res.samplePaths.push_back(inst.sample_path);
                }
            }, song.instruments[i]);
        }

        // Deduplicate
        std::sort(res.samplePaths.begin(), res.samplePaths.end());
        res.samplePaths.erase(
            std::unique(res.samplePaths.begin(), res.samplePaths.end()),
            res.samplePaths.end());

        // Check which samples actually exist on disk
        for (const auto& raw : res.samplePaths) {
            // M8 paths are absolute ("/Samples/..."); strip leading "/" for local resolution
            std::string rel = raw;
            if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);

            std::string resolved = sampleRoot.empty() ? rel : sampleRoot + "/" + rel;

            // Try resolved path, then CWD fallback
            bool found = false;
            {
                std::ifstream f(resolved, std::ios::binary);
                if (f.good()) { found = true; }
            }
            if (!found && !rel.empty()) {
                std::ifstream f(rel, std::ios::binary);
                if (f.good()) { found = true; }
            }
            if (!found) {
                res.missing.push_back(raw);
            }
        }

        res.ok = true;
    } catch (const std::exception& e) {
        res.error = e.what();
    } catch (...) {
        res.error = "unknown error";
    }
    return res;
}

bool saveSong(const std::string& path, const LoadResult& origin,
              const engine::Sequencer& seq, const engine::EngineState& state,
              std::string& error) {
    try {
        if (!origin.writable) {
            error = "pre-4.0 song — cannot be saved in place";
            return false;
        }

        // Start from the original song to preserve unimplemented fields
        m8::BinaryReader r(origin.original);
        m8::Song song = m8::Song::from_reader(r);

        // Overlay engine state
        convertEngineToSong(seq, state, song);

        auto out = song.write_over(origin.original);
        if (!writeFile(path, out)) {
            error = "cannot write file";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error";
        return false;
    }
}

bool saveNewSong(const std::string& path, const std::string& templatePath,
                 const engine::Sequencer& seq, const engine::EngineState& state,
                 std::string& error) {
    try {
        auto tmpl = readFile(templatePath);
        if (tmpl.empty()) { error = "cannot read template: " + templatePath; return false; }

        m8::BinaryReader r(tmpl);
        m8::Song base = m8::Song::from_reader(r);
        if (!base.version.at_least(4, 0)) {
            error = "template is pre-4.0 — cannot author a writable song from it";
            return false;
        }

        m8::Song song = buildSongFromEngine(seq, state, std::move(base));

        // Full write. Song::write() only serialises the data sections (song/
        // phrases/chains/tables/instruments/eqs), NOT the header region (tempo,
        // name, mixer, grooves) or the effects block — so write_over would keep
        // the TEMPLATE's tempo/mixer/effects, which is wrong for a song authored
        // from scratch. Write the whole file, seeding unwritten regions (scales,
        // tables, midi mappings, padding) from the template bytes.
        const m8::Offsets& o = song.version.at_least(4, 1) ? m8::V4_1_OFFSETS : m8::V4_OFFSETS;
        m8::BinaryWriter writer(std::move(tmpl));
        const char sig[10] = {'M','8','V','E','R','S','I','O','N','\0'};
        writer.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(sig), 10));
        writer.write(static_cast<uint8_t>((song.version.minor << 4) | song.version.patch));
        writer.write(static_cast<uint8_t>(song.version.major));
        writer.write(0);
        writer.write(0);
        writer.write_string(song.directory, 128);
        writer.write(song.transpose);
        writer.write_f32_le(song.tempo);
        writer.write(song.quantize);
        writer.write_string(song.name, 12);
        song.midi_settings.write(writer);
        writer.write(song.key);
        writer.skip(18);
        song.mixer_settings.write(writer);
        writer.seek(o.groove);
        for (const auto& g : song.grooves) g.write(writer);
        song.write(writer);                        // data sections (seeks internally)
        writer.seek(o.effect_settings);
        song.effects_settings.write(writer, song.version);
        auto out = writer.finish();
        if (!writeFile(path, out)) { error = "cannot write file: " + path; return false; }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error";
        return false;
    }
}

} // namespace m8::io
