#include "InstrumentIO.h"
#include "song.hpp"
#include "instruments.hpp"
#include "synths.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

namespace m8::io {

static void decodeEqBand(const uint8_t* src, engine::EqBand& dst) {
    dst.rawModeType = src[0];
    dst.type = src[0] & 0x7;
    dst.mode = (src[0] >> 5) & 0x7;
    dst.freq = (int(src[2]) << 8) | int(src[1]);
    dst.gain = static_cast<int16_t>((uint16_t(src[4]) << 8) | uint16_t(src[3]));
    dst.q    = src[5];
}

static void encodeEqBand(const engine::EqBand& src, uint8_t* dst) {
    const uint8_t base = static_cast<uint8_t>(src.rawModeType);
    dst[0] = static_cast<uint8_t>((base & ~0xE7) | (src.type & 0x7) | ((src.mode & 0x7) << 5));
    dst[1] = static_cast<uint8_t>(src.freq & 0xFF);
    dst[2] = static_cast<uint8_t>((src.freq >> 8) & 0xFF);
    const uint16_t g = static_cast<uint16_t>(static_cast<int16_t>(src.gain));
    dst[3] = static_cast<uint8_t>(g & 0xFF);
    dst[4] = static_cast<uint8_t>((g >> 8) & 0xFF);
    dst[5] = static_cast<uint8_t>(src.q);
}

// Helper: Convert library Mod to engine Modulator
static void libModToEngine(const m8::Mod& src, engine::Modulator& dst) {
    dst.type = 0;
    dst.dest = 0;
    dst.amt = 0x80;
    dst.p1 = dst.p2 = dst.p3 = dst.p4 = 0;

    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, m8::AHDEnv>) {
            dst.type = 0; // AHD
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = m.attack;
            dst.p2 = m.hold;
            dst.p3 = m.decay;
        } else if constexpr (std::is_same_v<T, m8::ADSREnv>) {
            dst.type = 1; // ADSR
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = m.attack;
            dst.p2 = m.decay;
            dst.p3 = m.sustain;
            dst.p4 = m.release;
        } else if constexpr (std::is_same_v<T, m8::DrumEnv>) {
            dst.type = 2; // DRUM
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = m.peak;
            dst.p2 = m.body;
            dst.p3 = m.decay;
        } else if constexpr (std::is_same_v<T, m8::LFO>) {
            dst.type = 3; // LFO
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = static_cast<uint8_t>(m.shape);
            dst.p2 = m.trigger_mode;
            dst.p3 = m.freq;
            dst.p4 = m.retrigger;
        } else if constexpr (std::is_same_v<T, m8::TrigEnv>) {
            dst.type = 4; // TRIG
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = m.attack;
            dst.p2 = m.hold;
            dst.p3 = m.decay;
            dst.p4 = m.src;
        } else if constexpr (std::is_same_v<T, m8::TrackingEnv>) {
            dst.type = 5; // TRACKING
            dst.dest = m.dest;
            dst.amt = m.amount;
            dst.p1 = m.src;
            dst.p2 = m.lval;
            dst.p3 = m.hval;
        }
    }, src);
}

// Helper: Convert engine Modulator to library Mod
static m8::Mod engineModToLib(const engine::Modulator& src) {
    if (src.dest == 0 && src.type == 0) return m8::Mod();

    switch (src.type) {
    case 0: { // AHD
        m8::AHDEnv env;
        env.dest = src.dest;
        env.amount = src.amt;
        env.attack = src.p1;
        env.hold = src.p2;
        env.decay = src.p3;
        return env;
    }
    case 1: { // ADSR
        m8::ADSREnv env;
        env.dest = src.dest;
        env.amount = src.amt;
        env.attack = src.p1;
        env.decay = src.p2;
        env.sustain = src.p3;
        env.release = src.p4;
        return env;
    }
    case 2: { // DRUM
        m8::DrumEnv env;
        env.dest = src.dest;
        env.amount = src.amt;
        env.peak = src.p1;
        env.body = src.p2;
        env.decay = src.p3;
        return env;
    }
    case 3: { // LFO
        m8::LFO lfo;
        lfo.dest = src.dest;
        lfo.amount = src.amt;
        lfo.shape = static_cast<m8::LfoShape>(src.p1);
        lfo.trigger_mode = src.p2;
        lfo.freq = src.p3;
        lfo.retrigger = src.p4;
        return lfo;
    }
    case 4: { // TRIG
        m8::TrigEnv env;
        env.dest = src.dest;
        env.amount = src.amt;
        env.attack = src.p1;
        env.hold = src.p2;
        env.decay = src.p3;
        env.src = src.p4;
        return env;
    }
    case 5: { // TRACKING
        m8::TrackingEnv env;
        env.dest = src.dest;
        env.amount = src.amt;
        env.src = src.p1;
        env.lval = src.p2;
        env.hval = src.p3;
        return env;
    }
    default:
        return m8::Mod();
    }
}

// Convert m8::Instrument to engine::Instrument
static void convertLibInstrument(const m8::Instrument& libInst, engine::Instrument& engInst) {
    std::visit([&](const auto& inst) {
        using T = std::decay_t<decltype(inst)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            engInst.type = engine::InstType::INST_NONE;
        } else if constexpr (std::is_same_v<T, m8::MacroSynth>) {
            engInst.type = engine::InstType::INST_MACROSYN;
            engine::setName(engInst.name, inst.name.c_str());
            auto& ms = engInst.macrosyn;
            ms.transp = inst.transpose ? 1 : 0;
            ms.tbl_tic = inst.table_tick;
            ms.shape = inst.shape;
            ms.timbre = inst.timbre;
            ms.color = inst.color;
            ms.degrade = inst.degrade;
            ms.redux = inst.reductor;
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
            for (int m = 0; m < 4; ++m) libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
        } else if constexpr (std::is_same_v<T, m8::Sampler>) {
            engInst.type = engine::InstType::INST_SAMPLER;
            engine::setName(engInst.name, inst.name.c_str());
            auto& s = engInst.sampler;
            std::strncpy(s.samplePath, inst.sample_path.c_str(), sizeof(s.samplePath) - 1);
            s.samplePath[sizeof(s.samplePath) - 1] = '\0';
            s.transp = inst.transpose ? 1 : 0;
            s.tbl_tic = inst.table_tick;
            s.detune = static_cast<int>(static_cast<int8_t>(inst.synth_params.fine_pitch)) + 0x80;
            s.play = inst.play_mode;
            s.slice = inst.slice;
            s.start = inst.start;
            s.loop_st = inst.loop_start;
            s.length = inst.length;
            s.degrade = inst.degrade;
            s.amp = inst.synth_params.volume;
            s.filter_type = inst.synth_params.filter_type;
            s.cutoff = inst.synth_params.filter_cutoff;
            s.res = inst.synth_params.filter_res;
            s.lim = inst.synth_params.amp_type;
            s.pan = inst.synth_params.mixer_pan;
            s.dry = inst.synth_params.mixer_dry;
            s.cho = inst.synth_params.mixer_chorus;
            s.del = inst.synth_params.mixer_delay;
            s.rev = inst.synth_params.mixer_reverb;
            for (int m = 0; m < 4; ++m) libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
        } else if constexpr (std::is_same_v<T, m8::HyperSynth>) {
            engInst.type = engine::InstType::INST_HYPERSYN;
            engine::setName(engInst.name, inst.name.c_str());
            auto& h = engInst.hyper;
            h.transp = inst.transpose ? 1 : 0;
            h.tbl_tic = inst.table_tick;
            h.scale = inst.scale;
            h.shift = inst.shift;
            h.swarm = inst.swarm;
            h.width = inst.width;
            h.subosc = inst.subosc;
            for (int c = 0; c < 7; ++c) h.default_chord[c] = inst.default_chord[c];
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
            for (int m = 0; m < 4; ++m) libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
        } else if constexpr (std::is_same_v<T, m8::FMSynth>) {
            engInst.type = engine::InstType::INST_FMSYNTH;
            engine::setName(engInst.name, inst.name.c_str());
            auto& f = engInst.fm;
            f.transp = inst.transpose ? 1 : 0;
            f.tbl_tic = inst.table_tick;
            f.algo = static_cast<uint8_t>(inst.algo);
            for (int o = 0; o < 4; ++o) {
                f.ops[o].shape = static_cast<uint8_t>(inst.operators[o].shape);
                f.ops[o].ratio = inst.operators[o].ratio;
                f.ops[o].ratio_fine = inst.operators[o].ratio_fine;
                f.ops[o].level = inst.operators[o].level;
                f.ops[o].feedback = inst.operators[o].feedback;
                f.ops[o].retrigger = inst.operators[o].retrigger;
                f.ops[o].mod_a = inst.operators[o].mod_a;
                f.ops[o].mod_b = inst.operators[o].mod_b;
            }
            f.mod1 = inst.mod1;
            f.mod2 = inst.mod2;
            f.mod3 = inst.mod3;
            f.mod4 = inst.mod4;
            f.amp = inst.synth_params.volume;
            f.filter_type = inst.synth_params.filter_type;
            f.cutoff = inst.synth_params.filter_cutoff;
            f.res = inst.synth_params.filter_res;
            f.lim = inst.synth_params.amp_type;
            f.pan = inst.synth_params.mixer_pan;
            f.dry = inst.synth_params.mixer_dry;
            f.cho = inst.synth_params.mixer_chorus;
            f.del = inst.synth_params.mixer_delay;
            f.rev = inst.synth_params.mixer_reverb;
            for (int m = 0; m < 4; ++m) libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
        } else if constexpr (std::is_same_v<T, m8::WavSynth>) {
            engInst.type = engine::InstType::INST_WAVSYNTH;
            engine::setName(engInst.name, inst.name.c_str());
            auto& w = engInst.wav;
            w.transp = inst.transpose ? 1 : 0;
            w.tbl_tic = inst.table_tick;
            w.shape = static_cast<uint8_t>(inst.shape);
            w.size = inst.size;
            w.mult = inst.mult;
            w.warp = inst.warp;
            w.scan = inst.scan;
            w.amp = inst.synth_params.volume;
            w.filter_type = inst.synth_params.filter_type;
            w.cutoff = inst.synth_params.filter_cutoff;
            w.res = inst.synth_params.filter_res;
            w.lim = inst.synth_params.amp_type;
            w.pan = inst.synth_params.mixer_pan;
            w.dry = inst.synth_params.mixer_dry;
            w.cho = inst.synth_params.mixer_chorus;
            w.del = inst.synth_params.mixer_delay;
            w.rev = inst.synth_params.mixer_reverb;
            for (int m = 0; m < 4; ++m) libModToEngine(inst.synth_params.mods[m], engInst.mods[m]);
        }
    }, libInst);
}

// Convert engine::Instrument to m8::Instrument
static m8::Instrument convertEngineInstrument(const engine::Instrument& e) {
    std::string nm(e.name);
    while (!nm.empty() && (nm.back() == ' ' || nm.back() == '-' || nm.back() == '\0')) nm.pop_back();

    if (e.type == engine::InstType::INST_MACROSYN) {
        m8::MacroSynth ms{};
        ms.number = 0;
        ms.name = nm;
        ms.transpose = (e.macrosyn.transp != 0);
        ms.table_tick = e.macrosyn.tbl_tic;
        ms.shape = e.macrosyn.shape;
        ms.timbre = e.macrosyn.timbre;
        ms.color = e.macrosyn.color;
        ms.degrade = e.macrosyn.degrade;
        ms.reductor = e.macrosyn.redux;
        ms.synth_params.volume = e.macrosyn.amp;
        ms.synth_params.filter_type = e.macrosyn.filter_type;
        ms.synth_params.filter_cutoff = e.macrosyn.cutoff;
        ms.synth_params.filter_res = e.macrosyn.res;
        ms.synth_params.amp_type = e.macrosyn.lim;
        ms.synth_params.mixer_pan = e.macrosyn.pan;
        ms.synth_params.mixer_dry = e.macrosyn.dry;
        ms.synth_params.mixer_chorus = e.macrosyn.cho;
        ms.synth_params.mixer_delay = e.macrosyn.del;
        ms.synth_params.mixer_reverb = e.macrosyn.rev;
        ms.synth_params.associated_eq = 0xFF;
        for (int k = 0; k < 4; ++k) ms.synth_params.mods[k] = engineModToLib(e.mods[k]);
        return ms;
    } else if (e.type == engine::InstType::INST_SAMPLER) {
        m8::Sampler s{};
        s.number = 0;
        s.name = nm;
        s.sample_path = e.sampler.samplePath;
        s.transpose = (e.sampler.transp != 0);
        s.table_tick = e.sampler.tbl_tic;
        s.play_mode = e.sampler.play;
        s.slice = e.sampler.slice;
        s.start = e.sampler.start;
        s.loop_start = e.sampler.loop_st;
        s.length = e.sampler.length;
        s.degrade = e.sampler.degrade;
        s.synth_params.fine_pitch = static_cast<uint8_t>(static_cast<int8_t>(e.sampler.detune - 0x80));
        s.synth_params.volume = e.sampler.amp;
        s.synth_params.filter_type = e.sampler.filter_type;
        s.synth_params.filter_cutoff = e.sampler.cutoff;
        s.synth_params.filter_res = e.sampler.res;
        s.synth_params.amp_type = e.sampler.lim;
        s.synth_params.mixer_pan = e.sampler.pan;
        s.synth_params.mixer_dry = e.sampler.dry;
        s.synth_params.mixer_chorus = e.sampler.cho;
        s.synth_params.mixer_delay = e.sampler.del;
        s.synth_params.mixer_reverb = e.sampler.rev;
        s.synth_params.associated_eq = 0xFF;
        for (int k = 0; k < 4; ++k) s.synth_params.mods[k] = engineModToLib(e.mods[k]);
        return s;
    } else if (e.type == engine::InstType::INST_HYPERSYN) {
        m8::HyperSynth hyp{};
        hyp.number = 0;
        hyp.name = nm;
        hyp.transpose = (e.hyper.transp != 0);
        hyp.table_tick = e.hyper.tbl_tic;
        hyp.scale = e.hyper.scale;
        hyp.shift = e.hyper.shift;
        hyp.swarm = e.hyper.swarm;
        hyp.width = e.hyper.width;
        hyp.subosc = e.hyper.subosc;
        for (int c = 0; c < 7; ++c) hyp.default_chord[c] = e.hyper.default_chord[c];
        for (int s = 0; s < 16; ++s)
            for (int n = 0; n < 6; ++n)
                hyp.chords[s][n] = e.hyper.chords[s][n];
        hyp.synth_params.volume = e.hyper.amp;
        hyp.synth_params.filter_type = e.hyper.filter_type;
        hyp.synth_params.filter_cutoff = e.hyper.cutoff;
        hyp.synth_params.filter_res = e.hyper.res;
        hyp.synth_params.amp_type = e.hyper.lim;
        hyp.synth_params.mixer_pan = e.hyper.pan;
        hyp.synth_params.mixer_dry = e.hyper.dry;
        hyp.synth_params.mixer_chorus = e.hyper.cho;
        hyp.synth_params.mixer_delay = e.hyper.del;
        hyp.synth_params.mixer_reverb = e.hyper.rev;
        hyp.synth_params.associated_eq = 0xFF;
        for (int k = 0; k < 4; ++k) hyp.synth_params.mods[k] = engineModToLib(e.mods[k]);
        return hyp;
    } else if (e.type == engine::InstType::INST_FMSYNTH) {
        m8::FMSynth fms{};
        fms.number = 0;
        fms.name = nm;
        fms.transpose = (e.fm.transp != 0);
        fms.table_tick = e.fm.tbl_tic;
        fms.algo = static_cast<m8::FmAlgo>(e.fm.algo);
        for (int o = 0; o < 4; ++o) {
            fms.operators[o].shape = static_cast<m8::FMWave>(e.fm.ops[o].shape);
            fms.operators[o].ratio = e.fm.ops[o].ratio;
            fms.operators[o].ratio_fine = e.fm.ops[o].ratio_fine;
            fms.operators[o].level = e.fm.ops[o].level;
            fms.operators[o].feedback = e.fm.ops[o].feedback;
            fms.operators[o].retrigger = e.fm.ops[o].retrigger;
            fms.operators[o].mod_a = e.fm.ops[o].mod_a;
            fms.operators[o].mod_b = e.fm.ops[o].mod_b;
        }
        fms.mod1 = e.fm.mod1;
        fms.mod2 = e.fm.mod2;
        fms.mod3 = e.fm.mod3;
        fms.mod4 = e.fm.mod4;
        fms.synth_params.volume = e.fm.amp;
        fms.synth_params.filter_type = e.fm.filter_type;
        fms.synth_params.filter_cutoff = e.fm.cutoff;
        fms.synth_params.filter_res = e.fm.res;
        fms.synth_params.amp_type = e.fm.lim;
        fms.synth_params.mixer_pan = e.fm.pan;
        fms.synth_params.mixer_dry = e.fm.dry;
        fms.synth_params.mixer_chorus = e.fm.cho;
        fms.synth_params.mixer_delay = e.fm.del;
        fms.synth_params.mixer_reverb = e.fm.rev;
        fms.synth_params.associated_eq = 0xFF;
        for (int k = 0; k < 4; ++k) fms.synth_params.mods[k] = engineModToLib(e.mods[k]);
        return fms;
    } else if (e.type == engine::InstType::INST_WAVSYNTH) {
        m8::WavSynth wvs{};
        wvs.number = 0;
        wvs.name = nm;
        wvs.transpose = (e.wav.transp != 0);
        wvs.table_tick = e.wav.tbl_tic;
        wvs.shape = static_cast<m8::WavShape>(e.wav.shape);
        wvs.size = e.wav.size;
        wvs.mult = e.wav.mult;
        wvs.warp = e.wav.warp;
        wvs.scan = e.wav.scan;
        wvs.synth_params.volume = e.wav.amp;
        wvs.synth_params.filter_type = e.wav.filter_type;
        wvs.synth_params.filter_cutoff = e.wav.cutoff;
        wvs.synth_params.filter_res = e.wav.res;
        wvs.synth_params.amp_type = e.wav.lim;
        wvs.synth_params.mixer_pan = e.wav.pan;
        wvs.synth_params.mixer_dry = e.wav.dry;
        wvs.synth_params.mixer_chorus = e.wav.cho;
        wvs.synth_params.mixer_delay = e.wav.del;
        wvs.synth_params.mixer_reverb = e.wav.rev;
        wvs.synth_params.associated_eq = 0xFF;
        for (int k = 0; k < 4; ++k) wvs.synth_params.mods[k] = engineModToLib(e.mods[k]);
        return wvs;
    }
    return std::monostate{};
}

bool loadInstrument(const std::string& path, engine::Instrument& outInst, std::optional<engine::EqBank>& outEq, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open instrument file: " + path;
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.size() < 229) { // 14 version + 215 instrument
        error = "Instrument file too short (" + std::to_string(buffer.size()) + " bytes)";
        return false;
    }

    try {
        BinaryReader r(buffer);
        Version v = Version::from_reader(r);
        auto ofs = v.at_least(4, 1) ? V4_1_OFFSETS.instrument_file_eq_offset
                                    : V4_OFFSETS.instrument_file_eq_offset;
        BinaryReader reader(buffer);
        auto iwe = m8::read_instrument_file(reader, ofs);
        convertLibInstrument(iwe.instrument, outInst);

        if (iwe.eq.has_value() && ofs && buffer.size() >= *ofs + 18) {
            engine::EqBank bank;
            const uint8_t* p = buffer.data() + *ofs;
            decodeEqBand(p + 0,  bank.low);
            decodeEqBand(p + 6,  bank.mid);
            decodeEqBand(p + 12, bank.high);
            outEq = bank;
        } else {
            outEq = std::nullopt;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool saveInstrument(const std::string& dirPath, const engine::Instrument& inst, const std::optional<engine::EqBank>& eq, std::string& outPath, std::string& error) {
    std::string safeName = inst.name;
    while (!safeName.empty() && (safeName.back() == '-' || safeName.back() == ' ' || safeName.back() == '\0')) {
        safeName.pop_back();
    }
    if (safeName.empty()) {
        switch (inst.type) {
        case engine::InstType::INST_MACROSYN: safeName = "MACROSYN"; break;
        case engine::InstType::INST_SAMPLER:  safeName = "SAMPLER"; break;
        case engine::InstType::INST_HYPERSYN: safeName = "HYPERSYN"; break;
        case engine::InstType::INST_FMSYNTH:  safeName = "FMSYNTH"; break;
        case engine::InstType::INST_WAVSYNTH: safeName = "WAVSYNTH"; break;
        default: safeName = "INSTRUMENT"; break;
        }
    }

    // Replace invalid filename characters
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    std::error_code ec;
    fs::path dir(dirPath.empty() ? "Instruments" : dirPath);
    if (!fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
    }

    fs::path targetFile = dir / (safeName + ".m8i");
    outPath = targetFile.generic_string();

    try {
        m8::Version version{4, 1, 0};
        m8::Instrument libInst = convertEngineInstrument(inst);

        BinaryWriter writer(std::vector<uint8_t>{});
        std::vector<uint8_t> prefix(10, 0);
        std::memcpy(prefix.data(), "M8VERSION", 9);
        writer.write_bytes(prefix);
        version.write(writer);
        m8::write_instrument(libInst, writer, version);

        if (eq.has_value() && m8::V4_1_OFFSETS.instrument_file_eq_offset) {
            size_t eqOff = *m8::V4_1_OFFSETS.instrument_file_eq_offset;
            while (writer.pos() < eqOff) writer.write(static_cast<uint8_t>(0));

            uint8_t eqBuf[18] = {};
            encodeEqBand(eq->low,  eqBuf + 0);
            encodeEqBand(eq->mid,  eqBuf + 6);
            encodeEqBand(eq->high, eqBuf + 12);
            for (int i = 0; i < 18; ++i) writer.write(eqBuf[i]);
        }

        std::vector<uint8_t> bytes = writer.finish();
        if (bytes.size() < 14 + m8::INSTRUMENT_MEMORY_SIZE) {
            bytes.resize(14 + m8::INSTRUMENT_MEMORY_SIZE, 0);
        }
        std::ofstream out(targetFile, std::ios::binary);
        if (!out.is_open()) {
            error = "Could not open file for writing: " + outPath;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!out.good()) {
            error = "Failed to write instrument bytes to: " + outPath;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void ensureFactoryInstruments(const std::string& baseDir) {
    std::error_code ec;
    fs::path instPath(baseDir);
    fs::path factoryPath = instPath / "Factory";

    if (!fs::exists(factoryPath, ec)) {
        fs::create_directories(factoryPath, ec);
    }
}

} // namespace m8::io
