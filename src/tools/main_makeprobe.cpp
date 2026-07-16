// ===========================================================================
// src/tools/main_makeprobe.cpp
//
// Generates minimal .m8s probe files: one instrument at known parameters,
// one phrase with one sustained note, one chain, one song row.
//
//   m8_makeprobe --type macrosynth --shape 0x00 --timbre 0x40 --color 0x80 \
//                --note C-4 --out probe_macro_00_40_80.m8s
//
//   m8_makeprobe --sweep shape --type macrosynth --note C-4 --out-dir probes/
//
// Links m8_files_cpp only. No SDL, no engine.
// ===========================================================================

#include "song.hpp"
#include "synths.hpp"
#include "instruments.hpp"
#include "writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <variant>

// ---- note parsing ---------------------------------------------------------

static uint8_t parseNote(const char* s) {
    // Format: "C4", "C#4", "Db4", "D4", etc.  Also accepts "C-4" (ignore dash).
    // MIDI: C4 = 60, each semitone = +1
    if (std::strlen(s) < 2) throw std::runtime_error("bad note: " + std::string(s));

    char noteChar = s[0];
    int idx = 1;
    // Skip optional dash separator
    if (s[idx] == '-') ++idx;

    bool sharp = (s[idx] == '#');
    bool flat  = (s[idx] == 'b');
    int semitone = 0;
    int octave = 0;

    if (sharp || flat) {
        if (noteChar == 'C') semitone = sharp ? 1 : 11;
        else if (noteChar == 'D') semitone = sharp ? 3 : 1;
        else if (noteChar == 'E') semitone = sharp ? 5 : 3;
        else if (noteChar == 'F') semitone = sharp ? 6 : 4;
        else if (noteChar == 'G') semitone = sharp ? 8 : 6;
        else if (noteChar == 'A') semitone = sharp ? 10 : 8;
        else if (noteChar == 'B') semitone = sharp ? 0 : 10;
        else throw std::runtime_error("bad note letter: " + std::string(s));
        octave = s[idx + 1] - '0';
    } else {
        if (noteChar == 'C') semitone = 0;
        else if (noteChar == 'D') semitone = 2;
        else if (noteChar == 'E') semitone = 4;
        else if (noteChar == 'F') semitone = 5;
        else if (noteChar == 'G') semitone = 7;
        else if (noteChar == 'A') semitone = 9;
        else if (noteChar == 'B') semitone = 11;
        else throw std::runtime_error("bad note letter: " + std::string(s));
        octave = s[idx] - '0';
    }
    return static_cast<uint8_t>(60 + (octave - 4) * 12 + semitone);
}

// ---- build a minimal song -------------------------------------------------

static m8::Song buildProbeSong(
    const std::string& instType,
    uint8_t noteVal,
    int shape, int timbre, int color,
    int volume, int filterType, int filterCutoff, int filterRes,
    float tempo)
{
    m8::Song song;

    // Version V4.1 (round-trips through write_over)
    song.version = {4, 1, 0};
    song.directory = "";
    song.transpose = 0;
    song.tempo = tempo;
    song.quantize = 0;
    song.name = "PROBE";
    song.key = 0;

    // Default grooves (6 rows = standard timing)
    song.grooves.resize(m8::Song::N_GROOVES);
    for (size_t i = 0; i < m8::Song::N_GROOVES; ++i) {
        song.grooves[i].number = static_cast<uint8_t>(i);
        song.grooves[i].steps = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6};
    }

    // Song steps: row 0, track 0 = chain 0x00
    song.song.steps.fill(0xFF);
    song.song.steps[0] = 0x00;  // row 0, track 0

    // Phrase 0x00: row 0 = note, instrument 0, full velocity (0x80)
    song.phrases.resize(m8::Song::N_PHRASES);
    for (auto& p : song.phrases) {
        for (auto& s : p.steps) {
            s.note.value = 0xFF;
            s.velocity = 0xFF;
            s.instrument = 0xFF;
        }
    }
    song.phrases[0].steps[0].note.value = noteVal;
    song.phrases[0].steps[0].velocity = 0x80;
    song.phrases[0].steps[0].instrument = 0x00;

    // Chain 0x00: slot 0 = phrase 0x00
    song.chains.resize(m8::Song::N_CHAINS);
    for (auto& c : song.chains) {
        for (auto& cs : c.steps) {
            cs.phrase = 0xFF;
            cs.transpose = 0;
        }
    }
    song.chains[0].steps[0].phrase = 0x00;

    // Instruments: index 0 = our probe instrument, rest empty
    song.instruments.resize(m8::Song::N_INSTRUMENTS);
    for (auto& inst : song.instruments) {
        inst = std::monostate{};
    }

    // SynthParams for mod slot 0: AHD -> VOLUME with long hold+decay
    // so the note sustains >= 1.5s at 120 BPM
    auto makeSynthParams = [&]() {
        m8::SynthParams sp{};
        sp.volume = static_cast<uint8_t>(volume);
        sp.pitch = 0;
        sp.fine_pitch = 0;
        sp.filter_type = static_cast<uint8_t>(filterType);
        sp.filter_cutoff = static_cast<uint8_t>(filterCutoff);
        sp.filter_res = static_cast<uint8_t>(filterRes);
        sp.amp_type = 0;
        sp.amp_limit = 0;
        sp.env_amp_amt = 0;
        sp.env_flt_amt = 0;
        sp.env_pit_amt = 0;
        sp.lfo_amp_amt = 0;
        sp.lfo_flt_amt = 0;
        sp.lfo_pit_amt = 0;
        sp.mixer_pan = 0x80;
        sp.mixer_dry = 0xC0;
        sp.mixer_chorus = 0;
        sp.mixer_delay = 0;
        sp.mixer_reverb = 0;

        // Mod slot 0: AHD -> VOLUME (dest=1), full amount, long hold+decay
        // At 120 BPM, 1 tick = 1000 samples. hold=0x80=128 ticks, decay=0x80=128 ticks.
        // Total sustain ~= (hold+decay) * samplesPerTick ~= 256 * 1000 = 256000 samples ~= 5.3s
        m8::AHDEnv ahd;
        ahd.dest = 1;           // VOLUME
        ahd.amount = 0xFF;      // full positive
        ahd.attack = 0x01;      // fast attack
        ahd.hold = 0x80;        // long hold
        ahd.decay = 0x80;       // long decay
        sp.mods[0] = ahd;

        // Mods 1-3: empty (monostate)
        sp.mods[1] = std::monostate{};
        sp.mods[2] = std::monostate{};
        sp.mods[3] = std::monostate{};

        sp.associated_eq = 0xFF;
        return sp;
    };

    if (instType == "macrosynth") {
        m8::MacroSynth ms;
        ms.number = 0;
        ms.name = "PROBE";
        ms.transpose = true;
        ms.table_tick = 0xFF;
        ms.shape = static_cast<uint8_t>(shape);
        ms.timbre = static_cast<uint8_t>(timbre);
        ms.color = static_cast<uint8_t>(color);
        ms.degrade = 0;
        ms.reductor = 0;
        ms.synth_params = makeSynthParams();
        song.instruments[0] = ms;
    } else if (instType == "wavsynth") {
        m8::WavSynth ws;
        ws.number = 0;
        ws.name = "PROBE";
        ws.transpose = true;
        ws.table_tick = 0xFF;
        ws.shape = static_cast<m8::WavShape>(shape);
        ws.size = 0x80;
        ws.mult = 0x80;
        ws.warp = 0;
        ws.scan = 0;
        ws.synth_params = makeSynthParams();
        song.instruments[0] = ws;
    } else if (instType == "fmsynth") {
        m8::FMSynth fm;
        fm.number = 0;
        fm.name = "PROBE";
        fm.transpose = true;
        fm.table_tick = 0xFF;
        fm.algo = m8::FmAlgo::Algo0;
        fm.mod1 = fm.mod2 = fm.mod3 = fm.mod4 = 0;
        for (auto& op : fm.operators) {
            op.shape = m8::FMWave::Sin;
            op.ratio = 0;
            op.ratio_fine = 0;
            op.level = 0;
            op.feedback = 0;
            op.retrigger = 0;
            op.mod_a = op.mod_b = 0;
        }
        fm.synth_params = makeSynthParams();
        song.instruments[0] = fm;
    } else if (instType == "hypersynth") {
        m8::HyperSynth hs;
        hs.number = 0;
        hs.name = "PROBE";
        hs.transpose = true;
        hs.table_tick = 0xFF;
        hs.scale = 0;
        hs.shift = 0;
        hs.swarm = 0;
        hs.width = 0x80;
        hs.subosc = 0;
        hs.default_chord = {};
        for (auto& ch : hs.chords) ch = {};
        hs.synth_params = makeSynthParams();
        song.instruments[0] = hs;
    } else {
        throw std::runtime_error("unknown instrument type: " + instType);
    }

    // Effects: sensible defaults
    song.effects_settings = {};
    song.effects_settings.delay_time_l = 0x30;
    song.effects_settings.delay_time_r = 0x30;
    song.effects_settings.delay_feedback = 0x60;
    song.effects_settings.reverb_size = 0x80;
    song.effects_settings.reverb_damping = 0x80;

    // Mixer: all tracks on, moderate volume
    song.mixer_settings = {};
    song.mixer_settings.master_volume = 0xE0;
    song.mixer_settings.master_limit = 0x40;
    for (auto& v : song.mixer_settings.track_volume) v = 0xE0;
    song.mixer_settings.chorus_volume = 0;
    song.mixer_settings.delay_volume = 0;
    song.mixer_settings.reverb_volume = 0;

    // Scales, tables, EQs: default
    song.scales.resize(m8::Song::N_SCALES);
    song.tables.resize(m8::Song::N_TABLES);
    song.eqs.resize(m8::Song::N_GROOVES); // V4.1 uses 32 EQs
    song.midi_mappings.resize(m8::Song::N_MIDI_MAPPINGS);

    return song;
}

// ---- write song to file ---------------------------------------------------

static void writeSongFile(const std::string& path, const m8::Song& song) {
    // BinaryWriter with zero-filled buffer large enough for the entire file
    // V4.1 total size: EQs end at 0x1AD5A + 4 + 128*18 = 0x23ADE (approx)
    // Round up generously.
    constexpr size_t FILE_SIZE = 0x28000;
    std::vector<uint8_t> buf(FILE_SIZE, 0);

    // Write version header at offset 0
    // from_reader reads 14 bytes: 10-byte version_string + lsb + msb + 2 skip
    m8::BinaryWriter writer(std::move(buf));

    // 10-byte version string (M8 file signature — matches real .m8s files)
    // "M8VERSION" + null = exactly 10 bytes
    const char sig[10] = {'M','8','V','E','R','S','I','O','N','\0'};
    writer.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(sig), 10));
    // Version bytes (lsb, msb) — from_reader expects these at offset 10-11
    uint8_t lsb = (song.version.minor << 4) | song.version.patch;
    uint8_t msb = song.version.major;
    writer.write(lsb);
    writer.write(msb);
    writer.write(0); // skip
    writer.write(0); // skip

    // Now at offset 14 — write the rest of the header fields
    writer.write_string(song.directory, 128);
    writer.write(song.transpose);
    writer.write_f32_le(song.tempo);
    writer.write(song.quantize);
    writer.write_string(song.name, 12);
    song.midi_settings.write(writer);
    writer.write(song.key);
    writer.skip(18); // padding
    song.mixer_settings.write(writer);

    // Write song data sections at their fixed offsets
    song.write(writer);

    auto out = writer.finish();

    // Write to file
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
}

// ---- round-trip verification ----------------------------------------------

static bool verifyRoundTrip(const std::string& path, const std::string& instType,
                            int shape, int timbre, int color) {
    // Read the file back
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "  cannot open %s for verify\n", path.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(sz);
    std::fread(data.data(), 1, sz, f);
    std::fclose(f);

    m8::BinaryReader reader(std::move(data));
    m8::Song song = m8::Song::from_reader(reader);

    // Check instrument type and parameters
    if (!std::holds_alternative<m8::MacroSynth>(song.instruments[0])) {
        std::fprintf(stderr, "  FAIL: instrument 0 is not MacroSynth\n");
        return false;
    }
    const auto& ms = std::get<m8::MacroSynth>(song.instruments[0]);
    if (ms.shape != shape) {
        std::fprintf(stderr, "  FAIL: shape %02X != %02X\n", ms.shape, shape);
        return false;
    }
    if (ms.timbre != timbre) {
        std::fprintf(stderr, "  FAIL: timbre %02X != %02X\n", ms.timbre, timbre);
        return false;
    }
    if (ms.color != color) {
        std::fprintf(stderr, "  FAIL: color %02X != %02X\n", ms.color, color);
        return false;
    }

    // Check phrase has our note
    const auto& step = song.phrases[0].steps[0];
    if (step.instrument != 0x00) {
        std::fprintf(stderr, "  FAIL: phrase[0][0] instrument %02X != 0x00\n", step.instrument);
        return false;
    }
    if (step.note.is_empty()) {
        std::fprintf(stderr, "  FAIL: phrase[0][0] note is empty\n");
        return false;
    }

    // Check chain points to phrase 0
    if (song.chains[0].steps[0].phrase != 0x00) {
        std::fprintf(stderr, "  FAIL: chain[0][0] phrase %02X != 0x00\n",
                     song.chains[0].steps[0].phrase);
        return false;
    }

    // Check song row 0 track 0 = chain 0
    if (song.song.steps[0] != 0x00) {
        std::fprintf(stderr, "  FAIL: song[0][0] chain %02X != 0x00\n", song.song.steps[0]);
        return false;
    }

    return true;
}

// ---- main -----------------------------------------------------------------

int main(int argc, char** argv) {
    std::string instType = "macrosynth";
    std::string note = "C-4";
    std::string outPath;
    std::string outDir;
    std::string sweepParam;
    int shape = 0, timbre = 0x40, color = 0x80;
    int volume = 0xE0;
    int filterType = 0, filterCutoff = 0xFF, filterRes = 0;
    float tempo = 120.0f;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        auto num  = [&]() -> int { return static_cast<int>(std::strtol(next().c_str(), nullptr, 0)); };

        if      (a == "--type")        instType = next();
        else if (a == "--note")        note = next();
        else if (a == "--out")         outPath = next();
        else if (a == "--out-dir")     outDir = next();
        else if (a == "--sweep")       sweepParam = next();
        else if (a == "--shape")       shape = num();
        else if (a == "--timbre")      timbre = num();
        else if (a == "--color")       color = num();
        else if (a == "--volume")      volume = num();
        else if (a == "--filter-type") filterType = num();
        else if (a == "--filter-cutoff") filterCutoff = num();
        else if (a == "--filter-res")  filterRes = num();
        else if (a == "--tempo")       tempo = static_cast<float>(std::atof(next().c_str()));
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    uint8_t noteVal = parseNote(note.c_str());

    // Sweep mode: write one file per value of the swept parameter
    if (!sweepParam.empty()) {
        if (outDir.empty()) {
            std::fprintf(stderr, "--sweep requires --out-dir\n");
            return 1;
        }
        std::filesystem::create_directories(outDir);

        for (int val = 0; val <= 0xF0; val += 0x10) {
            int s = shape, t = timbre, c = color;
            if (sweepParam == "shape")  s = val;
            else if (sweepParam == "timbre") t = val;
            else if (sweepParam == "color")  c = val;
            else { std::fprintf(stderr, "unknown sweep param: %s\n", sweepParam.c_str()); return 1; }

            char filename[256];
            std::snprintf(filename, sizeof(filename), "probe_%s_%02X.m8s",
                         sweepParam.c_str(), val);
            std::string path = outDir + "/" + filename;

            auto song = buildProbeSong(instType, noteVal, s, t, c,
                                       volume, filterType, filterCutoff, filterRes, tempo);
            writeSongFile(path, song);

            if (!verifyRoundTrip(path, instType, s, t, c)) {
                std::fprintf(stderr, "  round-trip FAILED for %s\n", path.c_str());
                return 1;
            }
            std::printf("wrote %s  (shape=%02X timbre=%02X color=%02X)\n",
                       path.c_str(), s, t, c);
        }
        std::printf("sweep complete: %s\n", sweepParam.c_str());
        return 0;
    }

    // Single file mode
    if (outPath.empty()) {
        outPath = "probe.m8s";
    }

    auto song = buildProbeSong(instType, noteVal, shape, timbre, color,
                               volume, filterType, filterCutoff, filterRes, tempo);
    writeSongFile(outPath, song);

    if (!verifyRoundTrip(outPath, instType, shape, timbre, color)) {
        std::fprintf(stderr, "round-trip FAILED\n");
        return 1;
    }
    std::printf("wrote %s  (type=%s note=%s shape=%02X timbre=%02X color=%02X)\n",
               outPath.c_str(), instType.c_str(), note.c_str(), shape, timbre, color);
    return 0;
}
