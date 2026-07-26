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
#include "m8/ParamRange.h"

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
    float tempo,
    const std::string& samplePath = "",
    int tableTick = 0xFF,
    int slice = 0,
    int modAmount = 0xFF,
    int modHold = 0xFF)
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

    // MidiSettings is a plain struct with no default initialisers (types.hpp), and
    // it IS serialised into the file (writeSongFile) — so leaving it default gives
    // the probe 25 bytes of uninitialised memory in the MIDI block. That is not
    // cosmetic: garbage in track_input_mode / track_input_channel routes the M8's
    // tracks to MIDI input/output instead of internal audio, and the device plays
    // SILENCE. Zero it so every probe is deterministic and audible (matches the
    // known-good probe_selftest, which happened to get zeros here).
    song.midi_settings = {};

    // Default grooves (6 rows = standard timing)
    song.grooves.resize(m8::Song::N_GROOVES);
    for (size_t i = 0; i < m8::Song::N_GROOVES; ++i) {
        song.grooves[i].number = static_cast<uint8_t>(i);
        song.grooves[i].steps = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6};
    }

    // Song steps: fill track 0 with chain 0x00 for all 16 song rows. The phrase is
    // 16 rows (~2 s at 120 BPM), so a single song row would leave the tail of a fixed
    // 3 s capture silent and fail the longest_silence health gate. Repeating the chain
    // keeps the device producing sound for the whole capture window; the AHD envelope's
    // long hold means each retrigger lands before the previous note decays, so it reads
    // as a continuous tone (the pitch window only sees the first, clean note).
    song.song.steps.fill(0xFF);
    for (size_t i = 0; i < 16; ++i) {
        song.song.steps[i * 8] = 0x00;
    }

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

    // TBL 00 (library command byte 0x06 -> engine FxCmd::TBL, see
    // SongIO.cpp's libFxToEngine): assigns table 0 to this track's
    // instrument. table_tick alone (set on the instrument, below) only
    // controls the tick RATE once a table is assigned -- Engine::tickTable()
    // is a no-op until assignedTable is set, which only happens by
    // processing this FX command (Engine.cpp tickTrack()).
    if (tableTick != 0xFF) {
        song.phrases[0].steps[0].fx1 = m8::FX(0x06, 0x00);
    }

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
    // so the note sustains >= 1.5s at 120 BPM (spec M8_HARDWARE_TEST_SPEC.md §4)
    auto makeSynthParams = [&](int volVal) {
        m8::SynthParams sp{};
        sp.volume = static_cast<uint8_t>(volVal);
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

        // Mod slot 0: AHD -> VOLUME (dest=1), full amount, maximum hold (0xFF=255 ticks).
        // At 120 BPM (6 ticks/step), 1 tick = 41.67 ms. hold=0xFF provides ~10.6 seconds
        // of sustained peak volume, guaranteeing a steady tone over the full capture window.
        m8::AHDEnv ahd;
        ahd.dest = 1;           // VOLUME
        ahd.amount = static_cast<uint8_t>(modAmount);
        ahd.attack = 0x01;      // fast attack
        ahd.hold = static_cast<uint8_t>(modHold);
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
        ms.table_tick = static_cast<uint8_t>(tableTick);
        ms.shape = static_cast<uint8_t>(shape);
        ms.timbre = static_cast<uint8_t>(timbre);
        ms.color = static_cast<uint8_t>(color);
        ms.degrade = 0;
        ms.reductor = 0;
        ms.synth_params = makeSynthParams(volume);
        song.instruments[0] = ms;
    } else if (instType == "sampler") {
        // Sampler probe (M8_HARDWARE_TEST_SPEC.md §9.1): our sampler is at hardware
        // parity, so a hardware capture vs our render of the SAME sample is a real
        // timbre gate — unlike MacroSynth (a saw placeholder). The sample WAV must
        // exist on the SD card at `samplePath` (M8-absolute, e.g. /probes/x.wav) and
        // locally under --sample-root for the render oracle.
        m8::Sampler smp;
        smp.number = 0;
        smp.name = "PROBE";
        smp.transpose = true;
        smp.table_tick = static_cast<uint8_t>(tableTick);
        smp.sample_path = samplePath;
        smp.play_mode = 0;      // FWD, play once (sample is long enough for the window)
        smp.slice = static_cast<uint8_t>(slice);
        smp.start = 0;
        smp.loop_start = 0;
        smp.length = 0xFF;      // whole sample
        smp.degrade = 0;
        smp.synth_params = makeSynthParams(volume);
        song.instruments[0] = smp;
    } else if (instType == "wavsynth") {
        m8::WavSynth ws;
        ws.number = 0;
        ws.name = "PROBE";
        ws.transpose = true;
        ws.table_tick = static_cast<uint8_t>(tableTick);
        ws.shape = static_cast<m8::WavShape>(shape);
        ws.size = 0x80;
        ws.mult = 0x80;
        ws.warp = 0;
        ws.scan = 0;
        ws.synth_params = makeSynthParams(volume);
        song.instruments[0] = ws;
    } else if (instType == "fmsynth") {
        m8::FMSynth fm;
        fm.number = 0;
        fm.name = "PROBE";
        fm.transpose = true;
        fm.table_tick = static_cast<uint8_t>(tableTick);
        fm.algo = m8::FmAlgo::Algo0;
        fm.mod1 = fm.mod2 = fm.mod3 = fm.mod4 = 0;
        // level=0x80/ratio=1 on every operator, not 0: output amplitude is
        // entirely level-driven, and different algorithms use different
        // operators as carriers (e.g. Algo0's carrier is op D, index 3), so
        // an all-zero-level probe is silent under every algorithm -- not a
        // useful "does this engine make sound" fixture.
        for (auto& op : fm.operators) {
            op.shape = m8::FMWave::Sin;
            op.ratio = 1;
            op.ratio_fine = 0;
            op.level = 0x80;
            op.feedback = 0;
            op.retrigger = 0;
            op.mod_a = op.mod_b = 0;
        }
        fm.synth_params = makeSynthParams(volume);
        song.instruments[0] = fm;
    } else if (instType == "hypersynth") {
        m8::HyperSynth hs;
        hs.number = 0;
        hs.name = "PROBE";
        hs.transpose = true;
        hs.table_tick = static_cast<uint8_t>(tableTick);
        hs.scale = 0;
        hs.shift = 0;
        hs.swarm = 0x40;
        hs.width = 0x80;
        hs.subosc = 0;
        // An empty default_chord means zero active notes regardless of the
        // note actually played (SynthVoice.cpp skips any chord slot <= 0),
        // so the probe was silent by construction. C-4 (MIDI 60) as the
        // sole chord note gives the swarm something to render.
        hs.default_chord = {};
        hs.default_chord[0] = 60;
        for (auto& ch : hs.chords) ch = {};
        hs.synth_params = makeSynthParams(volume);
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

    // Table 0, row 0: a distinctive +12 semitone transpose. Harmless unless
    // the probe's instrument also has table_tick set fast enough to execute
    // it (see --table-tick) -- inert by default, same as every other unused
    // field this generator preserves.
    song.tables[0].steps[0].transpose = 12;
    song.tables[0].steps[0].velocity = 0xFF; // empty (no volume override)

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
                            int shape, int timbre, int color,
                            const std::string& samplePath = "",
                            int expectedVolume = -1) {
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

    // Check instrument type and parameters (type-aware — §9.2)
    if (instType == "sampler") {
        if (!std::holds_alternative<m8::Sampler>(song.instruments[0])) {
            std::fprintf(stderr, "  FAIL: instrument 0 is not Sampler\n");
            return false;
        }
        const auto& smp = std::get<m8::Sampler>(song.instruments[0]);
        if (smp.sample_path != samplePath) {
            std::fprintf(stderr, "  FAIL: sample_path \"%s\" != \"%s\"\n",
                         smp.sample_path.c_str(), samplePath.c_str());
            return false;
        }
    } else if (instType == "macrosynth") {
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
    } else if (instType == "wavsynth") {
        if (!std::holds_alternative<m8::WavSynth>(song.instruments[0])) {
            std::fprintf(stderr, "  FAIL: instrument 0 is not WavSynth\n");
            return false;
        }
    } else if (instType == "fmsynth") {
        if (!std::holds_alternative<m8::FMSynth>(song.instruments[0])) {
            std::fprintf(stderr, "  FAIL: instrument 0 is not FMSynth\n");
            return false;
        }
    } else if (instType == "hypersynth") {
        if (!std::holds_alternative<m8::HyperSynth>(song.instruments[0])) {
            std::fprintf(stderr, "  FAIL: instrument 0 is not HyperSynth\n");
            return false;
        }
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

    // Extended field checks for amplitude-relevant fields
    const m8::SynthParams* sp = nullptr;
    if (instType == "sampler" && std::holds_alternative<m8::Sampler>(song.instruments[0])) {
        sp = &std::get<m8::Sampler>(song.instruments[0]).synth_params;
    } else if (std::holds_alternative<m8::MacroSynth>(song.instruments[0])) {
        sp = &std::get<m8::MacroSynth>(song.instruments[0]).synth_params;
    }

    if (sp) {
        if (expectedVolume >= 0) {
            uint8_t targetVol = (instType == "sampler" && expectedVolume == 0xE0) ? 0x00 : static_cast<uint8_t>(expectedVolume);
            if (sp->volume != targetVol) {
                std::fprintf(stderr, "  FAIL: volume %02X != %02X\n", sp->volume, targetVol);
                return false;
            }
        }
        if (sp->mixer_pan != 0x80) {
            std::fprintf(stderr, "  FAIL: mixer_pan %02X != 0x80\n", sp->mixer_pan);
            return false;
        }
        if (sp->mixer_dry != 0xC0) {
            std::fprintf(stderr, "  FAIL: mixer_dry %02X != 0xC0\n", sp->mixer_dry);
            return false;
        }
        if (sp->mixer_reverb != 0x00 || sp->mixer_chorus != 0x00 || sp->mixer_delay != 0x00) {
            std::fprintf(stderr, "  FAIL: mixer sends non-zero\n");
            return false;
        }
        if (!std::holds_alternative<m8::AHDEnv>(sp->mods[0])) {
            std::fprintf(stderr, "  FAIL: mod 0 is not AHDEnv\n");
            return false;
        }
    }

    std::printf("  self-consistency OK (does NOT prove hardware validity)\n");
    return true;
}

static bool verifyAgainstGolden(const std::string& probePath, const std::string& goldenPath) {
    FILE* f1 = std::fopen(probePath.c_str(), "rb");
    if (!f1) { std::fprintf(stderr, "cannot open probe file %s\n", probePath.c_str()); return false; }
    std::fseek(f1, 0, SEEK_END); long sz1 = std::ftell(f1); std::fseek(f1, 0, SEEK_SET);
    std::vector<uint8_t> gen(sz1); std::fread(gen.data(), 1, sz1, f1); std::fclose(f1);

    FILE* f2 = std::fopen(goldenPath.c_str(), "rb");
    if (!f2) { std::fprintf(stderr, "cannot open golden file %s\n", goldenPath.c_str()); return false; }
    std::fseek(f2, 0, SEEK_END); long sz2 = std::ftell(f2); std::fseek(f2, 0, SEEK_SET);
    std::vector<uint8_t> gold(sz2); std::fread(gold.data(), 1, sz2, f2); std::fclose(f2);

    size_t maxLen = std::max(gen.size(), gold.size());
    int suspiciousDiffs = 0;

    auto isBenign = [](size_t offset) {
        if (offset < 4) return true;
        if (offset >= 0x0E && offset < 0x8E) return true;
        if (offset >= 0x93 && offset < 0x9F) return true;
        return false;
    };

    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t b1 = (i < gen.size()) ? gen[i] : 0;
        uint8_t b2 = (i < gold.size()) ? gold[i] : 0;
        if (b1 != b2) {
            if (!isBenign(i)) {
                std::fprintf(stderr, "  SUSPICIOUS DIFF at offset %zu (0x%zX): gen=%02X golden=%02X\n", i, i, b1, b2);
                suspiciousDiffs++;
            }
        }
    }

    if (suspiciousDiffs > 0) {
        std::fprintf(stderr, "cross-oracle verification FAILED: %d suspicious byte diffs\n", suspiciousDiffs);
        return false;
    }
    std::printf("  cross-oracle validation OK against %s\n", goldenPath.c_str());
    return true;
}

// ---- main -----------------------------------------------------------------

int main(int argc, char** argv) {
    std::string instType = "macrosynth";
    std::string note = "C-4";
    std::string outPath;
    std::string outDir;
    std::string sweepParam;
    std::string samplePath;   // M8-absolute path for --type sampler, e.g. /probes/x.wav
    std::string verifyAgainst;
    std::string inspectPath;
    int shape = 0, timbre = 0x40, color = 0x80;
    int volume = 0x7F;
    int filterType = 0, filterCutoff = 0xFF, filterRes = 0;
    float tempo = 120.0f;
    int tableTick = 0xFF;  // 0xFF = table disabled (default, matches prior behavior)
    int slice = 0;         // sampler-only: 0=off, 1=FILE, 2..0x80 = N equal divisions
    int modAmt = 0xFF;
    int modHold = 0xFF;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        auto num  = [&]() -> int { return static_cast<int>(std::strtol(next().c_str(), nullptr, 0)); };

        if      (a == "--type")        instType = next();
        else if (a == "--note")        note = next();
        else if (a == "--out")         outPath = next();
        else if (a == "--out-dir")     outDir = next();
        else if (a == "--sweep")       sweepParam = next();
        else if (a == "--sample-path") samplePath = next();
        else if (a == "--shape")       shape = num();
        else if (a == "--timbre")      timbre = num();
        else if (a == "--color")       color = num();
        else if (a == "--volume")      volume = num();
        else if (a == "--mod-amt")     modAmt = num();
        else if (a == "--mod-hold")    modHold = num();
        else if (a == "--filter-type") filterType = num();
        else if (a == "--filter-cutoff") filterCutoff = num();
        else if (a == "--filter-res")  filterRes = num();
        else if (a == "--tempo")       tempo = static_cast<float>(std::atof(next().c_str()));
        // Assigns table 0 (pre-populated with a +12 semitone row-0 transpose,
        // see buildProbeSong) at the given tick rate, e.g. --table-tick 1 for
        // "every tick" -- fast enough to execute within a short render.
        else if (a == "--table-tick")  tableTick = num();
        else if (a == "--slice")       slice = num();
        else if (a == "--verify-against") verifyAgainst = next();
        else if (a == "--inspect")     inspectPath = next();
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
    }

    std::string rangeErr;
    if (!m8::checkRange(m8::kInstrumentVolume, volume, rangeErr)) {
        std::fprintf(stderr, "makeprobe: %s\n", rangeErr.c_str());
        std::fprintf(stderr, "  Instrument volume is capped on hardware; a larger\n"
                             "  value is misread by the device (0xE0 reads as 0x00).\n");
        return 1;
    }
    if (!m8::checkRange(m8::kMixerDry, 0xC0, rangeErr)) {
        std::fprintf(stderr, "makeprobe: %s\n", rangeErr.c_str()); return 1;
    }
    if (!m8::checkRange(m8::kMasterVolume, 0xE0, rangeErr)) {
        std::fprintf(stderr, "makeprobe: %s\n", rangeErr.c_str()); return 1;
    }
    if (!m8::checkRange(m8::kTrackVolume, 0xE0, rangeErr)) {
        std::fprintf(stderr, "makeprobe: %s\n", rangeErr.c_str()); return 1;
    }

    if (!inspectPath.empty()) {
        FILE* f = std::fopen(inspectPath.c_str(), "rb");
        if (!f) { std::fprintf(stderr, "cannot open %s\n", inspectPath.c_str()); return 1; }
        std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> data(sz); std::fread(data.data(), 1, sz, f); std::fclose(f);
        m8::BinaryReader reader(std::move(data));
        m8::Song s = m8::Song::from_reader(reader);
        std::printf("Inspecting %s (size %ld bytes):\n", inspectPath.c_str(), sz);
        std::printf("  Name: '%s', Tempo: %.1f, Transpose: %d\n", s.name.c_str(), s.tempo, s.transpose);
        std::printf("  Mixer: master_vol=0x%02X, track0_vol=0x%02X\n",
                    s.mixer_settings.master_volume, s.mixer_settings.track_volume[0]);
        for (size_t idx = 0; idx < s.instruments.size(); ++idx) {
            const auto& inst = s.instruments[idx];
            std::string typeName = "UNKNOWN";
            uint8_t vol = 0, pan = 0, dry = 0;
            const m8::SynthParams* sp = nullptr;

            if (std::holds_alternative<m8::Sampler>(inst)) {
                typeName = "SAMPLER";
                sp = &std::get<m8::Sampler>(inst).synth_params;
            } else if (std::holds_alternative<m8::MacroSynth>(inst)) {
                typeName = "MACROSYNTH";
                sp = &std::get<m8::MacroSynth>(inst).synth_params;
            } else if (std::holds_alternative<m8::WavSynth>(inst)) {
                typeName = "WAVSYNTH";
                sp = &std::get<m8::WavSynth>(inst).synth_params;
            } else if (std::holds_alternative<m8::FMSynth>(inst)) {
                typeName = "FMSYNTH";
                sp = &std::get<m8::FMSynth>(inst).synth_params;
            } else if (std::holds_alternative<m8::HyperSynth>(inst)) {
                typeName = "HYPERSYNTH";
                sp = &std::get<m8::HyperSynth>(inst).synth_params;
            }

            if (sp) {
                vol = sp->volume;
                pan = sp->mixer_pan;
                dry = sp->mixer_dry;
                std::printf("  Inst %zu: %s vol=0x%02X pan=0x%02X dry=0x%02X\n",
                            idx, typeName.c_str(), vol, pan, dry);
                if (std::holds_alternative<m8::AHDEnv>(sp->mods[0])) {
                    const auto& ahd = std::get<m8::AHDEnv>(sp->mods[0]);
                    std::printf("         mod0 AHD: dest=%d amt=0x%02X att=0x%02X hold=0x%02X dec=0x%02X\n",
                                ahd.dest, ahd.amount, ahd.attack, ahd.hold, ahd.decay);
                }
            }
        }
        return 0;
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
    if (instType == "sampler" && samplePath.empty()) {
        std::fprintf(stderr, "--type sampler requires --sample-path (M8-absolute, e.g. /probes/probe_sine.wav)\n");
        return 1;
    }

    auto song = buildProbeSong(instType, noteVal, shape, timbre, color,
                               volume, filterType, filterCutoff, filterRes, tempo, samplePath,
                               tableTick, slice, modAmt, modHold);
    writeSongFile(outPath, song);

    if (!verifyRoundTrip(outPath, instType, shape, timbre, color, samplePath, volume)) {
        std::fprintf(stderr, "round-trip FAILED\n");
        return 1;
    }
    if (!verifyAgainst.empty()) {
        if (!verifyAgainstGolden(outPath, verifyAgainst)) {
            std::fprintf(stderr, "verify-against FAILED\n");
            return 1;
        }
    }
    if (instType == "sampler")
        std::printf("wrote %s  (type=sampler note=%s sample=%s)\n",
                   outPath.c_str(), note.c_str(), samplePath.c_str());
    else
        std::printf("wrote %s  (type=%s note=%s shape=%02X timbre=%02X color=%02X)\n",
                   outPath.c_str(), instType.c_str(), note.c_str(), shape, timbre, color);
    return 0;
}
