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


// ---- The four EQs the file library does not model ----
// Main mix + ModFX/Delay/Reverb, sitting immediately after the instrument bank
// array in the same 18-byte format (EQ_SPEC.md §4c). The library stops at the
// banks, so these are read and written as raw bytes at a computed offset.
static size_t busEqOffset(const m8::Song& song) {
    const m8::Offsets& o = song.version.at_least(4, 1) ? m8::V4_1_OFFSETS : m8::V4_OFFSETS;
    return o.eq + o.instrument_eq_count * m8::Equ::V4_SIZE;
}

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

static void loadBusEqs(const m8::Song& song, const std::vector<uint8_t>& bytes,
                       engine::EngineState& state) {
    if (!song.version.at_least(4, 0)) return;
    const size_t off = busEqOffset(song);
    const size_t need = off + engine::kBusEqCount * m8::Equ::V4_SIZE;
    if (bytes.size() < need) return;
    for (int b = 0; b < engine::kBusEqCount; ++b) {
        engine::EqBank& bank = state.eqs[engine::kEqMix + b];
        const uint8_t* p = bytes.data() + off + b * m8::Equ::V4_SIZE;
        decodeEqBand(p + 0,  bank.low);
        decodeEqBand(p + 6,  bank.mid);
        decodeEqBand(p + 12, bank.high);
    }
}

// Patch the four bus EQs back into an already-serialised file image. Called
// after Song::write_over, which does not know these bytes exist and therefore
// leaves whatever the original had.
static void saveBusEqs(const m8::Song& song, const engine::EngineState& state,
                       std::vector<uint8_t>& bytes) {
    if (!song.version.at_least(4, 0)) return;
    const size_t off = busEqOffset(song);
    const size_t need = off + engine::kBusEqCount * m8::Equ::V4_SIZE;
    if (bytes.size() < need) return;
    for (int b = 0; b < engine::kBusEqCount; ++b) {
        const engine::EqBank& bank = state.eqs[engine::kEqMix + b];
        uint8_t* p = bytes.data() + off + b * m8::Equ::V4_SIZE;
        encodeEqBand(bank.low,  p + 0);
        encodeEqBand(bank.mid,  p + 6);
        encodeEqBand(bank.high, p + 12);
    }
}

// ---- Blocks the library reads but never writes back -------------------------
//
// `Song::write` only emits the sections it seeks to: song steps, phrases,
// chains, tables, instruments, EQ banks. Everything else the reader parses is
// left exactly as the original file had it. For most of the file that is the
// point -- it is how unmodeled data survives (ARCHITECTURE.md invariant 8) --
// but four blocks are modeled, are editable on their own screens, and were
// being dropped on every save-in-place: the tempo, the mixer, the 32 grooves
// and the effects. `convertEngineToSong` sets all four on the Song object and
// `write_over` then never emits them, so the edit went nowhere.
//
// They are patched into the serialised image here, the same way the bus EQs
// are -- and, like the bus EQs, field by field rather than through the
// library's own `MixerSettings::write` / `EffectsSettings::write`. Those two
// rebuild a whole block and would clobber bytes we must not touch:
//   - the four bytes at the end of the mixer block, which
//     `MixerSettings::from_reader` discards and its writer emits as zeros.
//     They are not padding, and one of them is now identified:
//        0xEA  probably limiter ATK -- it sits immediately before REL, in the
//              same order the device draws them, but its value was 00 at
//              measurement time and 00 matches three parameters. UNPROVEN.
//        0xEB  limiter REL. PROVEN on hardware 2026-08-13: changing REL from
//              10 to FF on the device moved this byte and nothing else in the
//              block (hw_findings.md UI-7).
//        0xEC  unknown.
//        0xED  unknown.
//     OTT's TIME and COLOR are known NOT to live here -- both read 80 on the
//     device while 0xEA/0xEC/0xED were all 00. Six scope parameters never fit
//     in four bytes. Do not assign the remaining three without a device diff.
//   - the reserved bytes inside the effects block, zeroed the same way.
//   - the analog/USB input pair, whose right channel the library cannot
//     represent -- rebuilding it is the data loss closed in hw_findings.md
//     §UI-4e, and it stays closed by not writing those bytes at all.

// Offsets into the file image, derived from the read order in the library's
// `Song::from_reader` and confirmed against real files in this tree:
//   0x8F  tempo -- decodes to 120.0 in V4EMPTY.m8s and 128.0 in songs/sunrise.m8s
//   0xCE  mixer -- 14 header + 128 directory + 1 transpose + 4 tempo + 1
//                  quantize + 12 name + 27 MidiSettings + 1 key + 18 skip.
//                  Cross-checked on the data: the analog right-channel marker
//                  at +14 reads 0xFF on every .m8s in this tree, and
//                  `dj_filter` at +25 reads 0x80 -- the documented "off" -- on
//                  both round-trip fixtures, songs/sunrise.m8s and a 6.5.0
//                  device save. (It reads 0x00 in device_golden/MacroSynth.m8s,
//                  which is a real value, not a misalignment.)
// Groove and effects come from the library's own offset table. `from_reader`
// uses `V4_OFFSETS` for the effects block regardless of version, and both
// tables agree on the groove offset, so this matches the reader exactly.
static constexpr size_t kTempoOffset = 0x8F;
// The global KEY, one byte, right after the 27-byte MIDI block and right before
// the 18 skipped bytes. Located by arithmetic, then confirmed twice over: the
// name field ends at 0x9F so MIDI runs 0xA0..0xBA, and 0xBB + 18 lands exactly
// on kMixerOffset below. The 18 it precedes is the 0xBC-0xCD run status.md
// already flags as volatile (the PROJECT screen's TIME STATS counter), which
// pins the same boundary from the other side.
static constexpr size_t kProjectKeyOffset = 0xBB;
static constexpr size_t kMixerOffset = 0xCE;

// Field offsets inside the 32-byte mixer block. +13..+24 are the analog/USB
// inputs, deliberately skipped.
//
// The last four were an unidentified tail until a device probe named them
// (hw_findings.md §UI-9): +28 limiter ATK, +29 limiter REL, +30 SOFT CLIP,
// +31 OTT. Only OTT has an engine field, so the other three are left alone and
// survive a save; the file library models none of them.
static constexpr size_t kMixMasterVolume = 0;
static constexpr size_t kMixMasterLimit  = 1;
static constexpr size_t kMixTrackVolume  = 2;   // 8 bytes
static constexpr size_t kMixChorusVolume = 10;
static constexpr size_t kMixDelayVolume  = 11;
static constexpr size_t kMixReverbVolume = 12;
static constexpr size_t kMixDjFilter     = 25;
static constexpr size_t kMixDjPeak       = 26;  // DJ filter RESONANCE, not OTT
static constexpr size_t kMixDjFilterType = 27;
static constexpr size_t kMixLimAtk       = 28;  // §UI-9
static constexpr size_t kMixLimRel       = 29;
static constexpr size_t kMixSoftClip     = 30;
static constexpr size_t kMixOtt          = 31;  // OTT amount
static constexpr size_t kMixerBlockSize  = 32;

// OTT is read here rather than through the library, which stops at +27.
// LIM ATK/REL, SOFT CLIP and OTT (+28..+31) are 4.0 and later, the same story
// as the effects block above: the library's MixerSettings stops at +27, and
// below 4.0 those four bytes are leftovers, not mixer state. DEMO1 read 0x32 of
// OTT and 0x12 of soft clip out of them. MASTER VOLUME and MASTER LIMIT are not
// affected -- they are the first two bytes of the block and the library reads
// them sequentially, so they hold in every version.
static void loadMixerTail(const std::vector<uint8_t>& bytes,
                          engine::MixerState& mixer,
                          const m8::Version& version) {
    if (bytes.size() < kMixerOffset + kMixerBlockSize) return;
    if (!version.at_least(4, 0)) return;
    mixer.ott       = bytes[kMixerOffset + kMixOtt];
    mixer.lim_atk   = bytes[kMixerOffset + kMixLimAtk];
    mixer.lim_rel   = bytes[kMixerOffset + kMixLimRel];
    mixer.soft_clip = bytes[kMixerOffset + kMixSoftClip];
}

// ---- The effects block, read and written at measured offsets ----------------
//
// NOT via the file library's `EffectsSettings`, whose field offsets are wrong:
// it allows three filler bytes after the modfx fields where there are five, and
// one after delay where there are three, so it starts delay 3 bytes early and
// reverb 5 bytes early. Newer firmware added MOD TYPE and SHIMMER and the
// library never caught up.
//
// The offsets below are measured, not inferred: the device's EFFECT SETTINGS
// screen was read alongside the bytes of a device-saved file, and delay's five
// values and reverb's five each appear consecutively where this table says
// (hw_findings.md UI-8). The evidence files are committed --
// tests/fixtures/device_golden/scope_rel_10.m8s, whose block reads
// `40 80 FF 00 00 00 00 00 00 30 30 80 FF 00 00 00 00 FF C0 10 FF FF`
// against a screen showing modfx 40:80 / FF / 00, delay 30:30 / 80 / FF / 00,
// reverb FF / C0 / 10:FF / FF.
//
// The gaps are deliberate and carry real device data, so they are left alone
// and survive a save. +4..+8 and +14..+16 read `FD AF 26 40 FF` and `41 10 E0`
// in V4EMPTY.m8s, a genuine M8-authored file -- not padding, and not something
// to zero. MODFX MOD TYPE and reverb SHIMMER are in there somewhere but are
// NOT located: +4 was the obvious guess for MOD TYPE and is wrong, since FD is
// not one of the three valid types. Do not assign these without a device diff.
static constexpr size_t kFxModDepth   = 0;
static constexpr size_t kFxModFreq    = 1;
static constexpr size_t kFxModWidth   = 2;
static constexpr size_t kFxModReverb  = 3;
static constexpr size_t kFxDelTimeL   = 9;
static constexpr size_t kFxDelTimeR   = 10;
static constexpr size_t kFxDelFeedbk  = 11;
static constexpr size_t kFxDelWidth   = 12;
static constexpr size_t kFxDelReverb  = 13;
static constexpr size_t kFxRevSize    = 17;
static constexpr size_t kFxRevDecay   = 18;
static constexpr size_t kFxRevModDep  = 19;
static constexpr size_t kFxRevModFrq  = 20;
static constexpr size_t kFxRevWidth   = 21;
static constexpr size_t kFxRevShimmer = 22;   // §UI-9
static constexpr size_t kFxOttTime    = 23;
static constexpr size_t kFxOttColor   = 24;
static constexpr size_t kFxModType    = 25;
static constexpr size_t kFxBlockSpan  = 26;   // highest touched offset + 1

static bool effectsBlockFits(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= m8::V4_OFFSETS.effect_settings + kFxBlockSpan;
}

// SHIMMER, OTT TIME/COLOR and MOD TYPE (+22..+25) only exist from firmware
// 4.0. Every older file we have -- v1.4, twelve v2.5 songs, v2.7 and v3.0.4 --
// carries leftover firmware bytes there instead, and they are not even
// song-specific: DEMO1 (v2.5.1) and TEST-FILE (v3.0.4) hold the identical run
// F7 DF D2 9E, the same way +26..+29 holds 43 12 49 82 in files of every
// version. Read as effects that gave DEMO1 a 97% shimmer reverb and a flanger
// in place of its chorus. Below 4.0 the four fields keep their defaults.
static void loadEffectsBlock(const std::vector<uint8_t>& bytes,
                             engine::EffectsState& fx,
                             const m8::Version& version) {
    if (!effectsBlockFits(bytes)) return;
    const uint8_t* p = bytes.data() + m8::V4_OFFSETS.effect_settings;
    fx.cho_mod_depth = p[kFxModDepth];
    fx.cho_mod_freq  = p[kFxModFreq];
    fx.cho_width     = p[kFxModWidth];
    fx.cho_reverb    = p[kFxModReverb];
    fx.del_time_l    = p[kFxDelTimeL];
    fx.del_time_r    = p[kFxDelTimeR];
    fx.del_feedback  = p[kFxDelFeedbk];
    fx.del_width     = p[kFxDelWidth];
    fx.del_reverb    = p[kFxDelReverb];
    fx.rev_size      = p[kFxRevSize];
    fx.rev_decay     = p[kFxRevDecay];
    fx.rev_mod_depth = p[kFxRevModDep];
    fx.rev_mod_freq  = p[kFxRevModFrq];
    fx.rev_width     = p[kFxRevWidth];
    if (!version.at_least(4, 0)) return;
    fx.rev_shimmer   = p[kFxRevShimmer];
    fx.ott_time      = p[kFxOttTime];
    fx.ott_color     = p[kFxOttColor];
    fx.modfx_type    = p[kFxModType];
}

static void saveEffectsBlock(const engine::EffectsState& fx,
                             std::vector<uint8_t>& bytes) {
    if (!effectsBlockFits(bytes)) return;
    uint8_t* p = bytes.data() + m8::V4_OFFSETS.effect_settings;
    p[kFxModDepth]  = static_cast<uint8_t>(fx.cho_mod_depth);
    p[kFxModFreq]   = static_cast<uint8_t>(fx.cho_mod_freq);
    p[kFxModWidth]  = static_cast<uint8_t>(fx.cho_width);
    p[kFxModReverb] = static_cast<uint8_t>(fx.cho_reverb);
    p[kFxDelTimeL]  = static_cast<uint8_t>(fx.del_time_l);
    p[kFxDelTimeR]  = static_cast<uint8_t>(fx.del_time_r);
    p[kFxDelFeedbk] = static_cast<uint8_t>(fx.del_feedback);
    p[kFxDelWidth]  = static_cast<uint8_t>(fx.del_width);
    p[kFxDelReverb] = static_cast<uint8_t>(fx.del_reverb);
    p[kFxRevSize]   = static_cast<uint8_t>(fx.rev_size);
    p[kFxRevDecay]  = static_cast<uint8_t>(fx.rev_decay);
    p[kFxRevModDep] = static_cast<uint8_t>(fx.rev_mod_depth);
    p[kFxRevModFrq] = static_cast<uint8_t>(fx.rev_mod_freq);
    p[kFxRevWidth]  = static_cast<uint8_t>(fx.rev_width);
    p[kFxRevShimmer]= static_cast<uint8_t>(fx.rev_shimmer);
    p[kFxOttTime]   = static_cast<uint8_t>(fx.ott_time);
    p[kFxOttColor]  = static_cast<uint8_t>(fx.ott_color);
    p[kFxModType]   = static_cast<uint8_t>(fx.modfx_type);
}

// ---- Scales ----------------------------------------------------------------
// One record is 46 bytes: a 12-bit enable mask (u16 LE) at +0, 12 offsets at +2
// as SIGNED 16-BIT LITTLE-ENDIAN HUNDREDTHS of a semitone, a 16-byte name at
// +26, and four bytes at +42 we do not model. Sixteen sit end to end at
// V4_OFFSETS.scale.
//
// The offset encoding is MEASURED (2026-08-14, fw 6.5.2). An OFFSET of -00.50
// set on the device and saved comes back as `CE FF` = 0xFFCE = -50. It is the
// same scheme AGENTS.md §7 already records for EQ gain, "16-bit signed,
// HUNDREDTHS". We previously read the pair as (signed whole semitone, unsigned
// hundredths), which the library also does: that agrees on every value whose
// bytes are zero, which is why no committed song could tell the two apart, and
// it cannot represent anything in (-1.00, 0.00) at all -- -0.50 would encode as
// whole 0 / cents 50 and read back as +0.50. `tests/fixtures/device_golden/
// scaleprobe.m8s` is the device-authored file that settled it; L31 pins it.
//
// MEASURED, not assumed -- and 42 (2 + 24 + 16, the fields alone) is the wrong
// answer that looks right. Reading three committed songs at a 42 stride put
// "MAJOR" four bytes late in record 1 and "MINOR" eight late in record 2, i.e.
// drifting by exactly 4 per record. At 46 every mask decodes to a real scale
// (0x0FFF chromatic, 0x0AB5 major, 0x05AD natural minor) and every name lands
// on +26. The arithmetic closes it: 16 * 46 = 736, and V4_OFFSETS.scale + 736
// is exactly where the EQ block begins.
//
// The trailing four are zero in every file inspected. TUNE would be the obvious
// candidate -- the SCALE view shows it and nothing else in the record holds it
// -- but 440.00 is not what a zero would decode to under any reading, so it is
// unproven and these bytes are preserved untouched, the same rule the effects
// block's unknown runs get.
//
// Read here rather than through the library's Scale::from_reader, the same
// shape of reason loadEffectsBlock has (hw_findings.md §UI-8): that reader
// takes the semitone byte UNSIGNED, so it cannot represent the negative half of
// the M8's -24.00..+24.00 offset range; its Scale::SIZE says 32; and it reads
// the sixteen records sequentially at the field width, so it drifts exactly as
// described above from record 1 on. Going direct also lets the name survive as
// raw bytes, so a save reproduces it.
static constexpr size_t kScaleRecSize   = 46;
// Pre-4.0 the record is the same 42 bytes of content with the four trailing
// bytes we do not model absent, so only the STRIDE changes -- mask, offsets and
// name keep their places. Measured off the block itself: the scale names sit
// 42 bytes apart in v1.4/v2.5/v3.0 files and 46 apart from v4.0 on, and 16 * 42
// lands exactly on the end of a pre-4.0 file. At 46 every scale after the first
// was read 4 bytes further off than the last.
static constexpr size_t kScaleRecSizePre4 = 42;
static constexpr size_t kScaleNameAt    = 26;
static constexpr size_t kScaleCount     = 16;
static constexpr size_t kScaleBlockSpan = kScaleRecSize * kScaleCount;

static size_t scaleRecSize(const m8::Version& version) {
    return version.at_least(4, 0) ? kScaleRecSize : kScaleRecSizePre4;
}

static bool scalesBlockFits(const std::vector<uint8_t>& bytes,
                            const m8::Version& version) {
    return bytes.size() >= m8::V4_OFFSETS.scale + scaleRecSize(version) * kScaleCount;
}

static void loadScalesBlock(const std::vector<uint8_t>& bytes,
                            engine::EngineState& state,
                            const m8::Version& version) {
    if (!scalesBlockFits(bytes, version)) return;
    const uint8_t* base = bytes.data() + m8::V4_OFFSETS.scale;
    const size_t rec = scaleRecSize(version);
    for (size_t s = 0; s < kScaleCount; ++s) {
        const uint8_t* p = base + s * rec;
        engine::Scale& dst = state.scales[s];
        const uint16_t map = static_cast<uint16_t>(p[0] | (p[1] << 8));
        for (int n = 0; n < 12; ++n) {
            dst.notes[n].enable = ((map >> n) & 1) != 0;
            const int16_t raw = static_cast<int16_t>(
                static_cast<uint16_t>(p[2 + n * 2] | (p[3 + n * 2] << 8)));
            dst.notes[n].offset = static_cast<float>(raw) / 100.0f;
        }
        std::memcpy(dst.name, p + kScaleNameAt, 16);
        dst.name[16] = '\0';
        // KEY and TUNE are not in the record -- 2 + 24 + 16 fills it exactly.
        // The manual calls KEY "a global setting", which agrees. Both are left
        // at their defaults until the byte that holds them is located.
    }
}

// Only reached for 4.0+ songs -- saveSong() refuses anything older -- so the
// 46-byte stride is the right one here.
static void saveScalesBlock(const engine::EngineState& state,
                            std::vector<uint8_t>& bytes) {
    if (bytes.size() < m8::V4_OFFSETS.scale + kScaleBlockSpan) return;
    uint8_t* base = bytes.data() + m8::V4_OFFSETS.scale;
    for (size_t s = 0; s < kScaleCount; ++s) {
        uint8_t* p = base + s * kScaleRecSize;
        const engine::Scale& src = state.scales[s];
        uint16_t map = 0;
        for (int n = 0; n < 12; ++n)
            if (src.notes[n].enable) map |= static_cast<uint16_t>(1u << n);
        p[0] = static_cast<uint8_t>(map & 0xFF);
        p[1] = static_cast<uint8_t>((map >> 8) & 0xFF);
        for (int n = 0; n < 12; ++n) {
            const float off = std::clamp(src.notes[n].offset,
                                         engine::kScaleOffsetMin,
                                         engine::kScaleOffsetMax);
            const int v = static_cast<int>(std::lround(off * 100.0f));
            p[2 + n * 2] = static_cast<uint8_t>(v & 0xFF);
            p[3 + n * 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        }
        // The name goes back as the 16 raw bytes it came in as, so a song we
        // did not edit round-trips byte for byte (test L4). The device pads it
        // with 0xFF, which survives this untouched. +42..+45 are deliberately
        // not written.
        std::memcpy(p + kScaleNameAt, src.name, 16);
    }
}

static void saveUnwrittenBlocks(const m8::Song& song,
                                const engine::EngineState& state,
                                std::vector<uint8_t>& bytes) {
    if (!song.version.at_least(4, 0)) return;
    const m8::Offsets& o = m8::V4_OFFSETS;

    // --- Tempo ---------------------------------------------------------------
    // Only rewritten when it actually changed. The engine holds tempo as
    // bpm + bpm_frac (hundredths), a lossy decomposition of the file's f32:
    // reassembling it can land a bit or two off the original for a tempo the
    // engine cannot represent exactly. Comparing at the engine's own resolution
    // leaves an untouched song bit-for-bit alone -- which the byte-identical
    // round-trip (L4) requires -- while a real edit still gets written.
    if (bytes.size() >= kTempoOffset + 4) {
        float original = 0.0f;
        std::memcpy(&original, bytes.data() + kTempoOffset, 4);
        if (std::round(original * 100.0f) != std::round(song.tempo * 100.0f)) {
            float t = song.tempo;
            std::memcpy(bytes.data() + kTempoOffset, &t, 4);
        }
    }

    // --- Global KEY ----------------------------------------------------------
    // Song::write does not emit the header, so an edited PROJECT KEY would
    // otherwise save "successfully" and reload unchanged -- the same class of
    // bug tempo, mixer and groove had.
    if (bytes.size() > kProjectKeyOffset)
        bytes[kProjectKeyOffset] = static_cast<uint8_t>(state.project.scale & 0xFF);

    // --- Mixer ---------------------------------------------------------------
    if (bytes.size() >= kMixerOffset + kMixerBlockSize) {
        uint8_t* p = bytes.data() + kMixerOffset;
        const auto& m = song.mixer_settings;
        p[kMixMasterVolume] = m.master_volume;
        p[kMixMasterLimit]  = m.master_limit;
        for (int i = 0; i < 8; ++i)
            p[kMixTrackVolume + i] = m.track_volume[i];
        p[kMixChorusVolume] = m.chorus_volume;
        p[kMixDelayVolume]  = m.delay_volume;
        p[kMixReverbVolume] = m.reverb_volume;
        p[kMixDjFilter]     = m.dj_filter;
        p[kMixDjPeak]       = m.dj_peak;
        p[kMixDjFilterType] = m.dj_filter_type;
        // OTT: ours to write, and the library has no field for it. ATK, REL and
        // SOFT CLIP sit beside it at +28..+30 and are deliberately not touched.
        p[kMixOtt]          = static_cast<uint8_t>(state.mixer.ott);
        p[kMixLimAtk]       = static_cast<uint8_t>(state.mixer.lim_atk);
        p[kMixLimRel]       = static_cast<uint8_t>(state.mixer.lim_rel);
        p[kMixSoftClip]     = static_cast<uint8_t>(state.mixer.soft_clip);
    }

    // --- Grooves (32 x 16 raw bytes) -----------------------------------------
    if (bytes.size() >= o.groove + song.grooves.size() * 16) {
        for (size_t g = 0; g < song.grooves.size(); ++g) {
            uint8_t* p = bytes.data() + o.groove + g * 16;
            for (int i = 0; i < 16; ++i) p[i] = song.grooves[g].steps[i];
        }
    }

    // Effects are patched by the caller via saveEffectsBlock(), which uses the
    // measured offsets rather than the library's (wrong) ones.
}

// ---- FxCmd mapping ----
// ---- FX command bytes -------------------------------------------------------
//
// Every entry in kFxBytes below was READ OFF A DEVICE. Nothing here is derived
// by walking the manual's command list and numbering it, which is how the old
// table was built and why it was wrong.
//
// MEASURED 2026-08-24, fw 6.5.2, DEMO2 (a v2.5.1 factory bundle) loaded on the
// device: ten of its phrases were captured with m8drv and each FX cell's printed
// command name was matched against the byte at that step in the file. The value
// columns agreed on every cell, which is what pins the alignment. That covered
// the 21 distinct command bytes DEMO2 uses. SCA/SCG come from a separate probe
// (2026-08-14): a phrase authored on the device with SCG 10 and SCA 20 saved as
// `11 10` and `10 20`. The two sets do not overlap and do not contradict.
//
// The bytes fall in two ranges -- sequencer commands below 0x40, instrument and
// modulation commands from 0x80 -- which the old single-run table could not
// represent at all.
//
// What this table is NOT: complete. Only bytes DEMO2 actually uses are here.
// Anything absent decodes to FxCmd::UNKNOWN and is preserved byte-for-byte on
// save (convertEngineToSong leaves UNKNOWN slots as the re-parsed original
// bytes, invariant #8), exactly as before. Adding an entry requires a device
// reading, not an inference from its neighbours -- the run is not contiguous and
// the gaps are real.
//
// Known-missing, in DEMO2 usage order: 0x91 SRV (16), 0x89 CUT (11), 0x83 PLY
// (7), 0x84 STA (4). Those are instrument-parameter commands with no engine
// counterpart yet; they need semantics, not just a byte.
//
// One migration detail, for whoever implements REP: the device rescales its
// value x4 when it loads a pre-4.0 song. File 0x02 displayed as 08, 0x08 as 20,
// 0xFF as FC (0x3FC truncated). The byte in the file is the pre-4.0 scale.
struct FxByte { uint8_t byte; engine::FxCmd cmd; };
static constexpr FxByte kFxBytes[] = {
    // sequencer commands
    { 0x04, engine::FxCmd::HOP },
    { 0x05, engine::FxCmd::KIL },
    { 0x08, engine::FxCmd::REP },   // the old table called this TIC
    { 0x0A, engine::FxCmd::PSL },
    { 0x0C, engine::FxCmd::PVB },
    { 0x10, engine::FxCmd::SCA },
    { 0x11, engine::FxCmd::SCG },
    { 0x12, engine::FxCmd::TBL },   // the old table put TBL at 0x06
    { 0x13, engine::FxCmd::THO },
    { 0x17, engine::FxCmd::VMV },
    { 0x21, engine::FxCmd::XRD },
    // instrument / modulation commands
    { 0x80, engine::FxCmd::VOL },   // the old table put VOL at 0x00
    { 0x92, engine::FxCmd::EA1 },
    { 0x94, engine::FxCmd::HO1 },
    { 0x95, engine::FxCmd::DE1 },
    { 0x9A, engine::FxCmd::DE2 },
    { 0x9C, engine::FxCmd::LA3 },
    { 0x9D, engine::FxCmd::LF3 },
    { 0x9E, engine::FxCmd::LT3 },
};

static engine::FxCmd libFxToEngine(uint8_t cmd) {
    if (cmd == 0xFF) return engine::FxCmd::NONE;
    for (const auto& e : kFxBytes) if (e.byte == cmd) return e.cmd;
    return engine::FxCmd::UNKNOWN;   // preserved, inert
}

static uint8_t engineFxToLib(engine::FxCmd cmd) {
    if (cmd == engine::FxCmd::NONE) return 0xFF;
    // UNKNOWN never routes through here -- the phrase save loop preserves the
    // original byte for UNKNOWN slots instead of calling this. Guard defensively.
    if (cmd == engine::FxCmd::UNKNOWN) return 0xFF;
    for (const auto& e : kFxBytes) if (e.cmd == cmd) return e.byte;
    return 0xFF;
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
//
// MEASURED 2026-08-19 (fw 6.5.2), and this mapping was WRONG until then: the
// device screen's AMP is the file's `amp_type` and its LIM is `amp_limit`. This
// file read `amp` from `volume` and `lim` from `amp_type` -- every field shifted
// one across -- so every loaded song played with the wrong amp value and the
// wrong limiter mode. Confirmed by loading a probe with a distinct signature
// byte in each slot and reading the device: volume=0x11 appeared nowhere on the
// INSTRUMENT screen, amp_type=0x22 showed as AMP, amp_limit=0x03 as LIM, and
// mixer_pan/dry/chorus/delay/reverb=0x44..0x88 as PAN/DRY/MFX/DEL/REV.
//
// `volume` is a separate level control. It is carried through load and save so a
// save cannot zero it, but the voice does NOT apply it yet -- its curve has not
// been measured, and guessing one is how the AMP model got into this state.
// See status.md.

static void libSynthParamsToEngine(const m8::SynthParams& sp,
                                    engine::SamplerState& s,
                                    engine::Modulator* mods) {
    s.volume = sp.volume;
    s.amp = sp.amp_type;
    s.filter_type = sp.filter_type;
    s.cutoff = sp.filter_cutoff;
    s.res = sp.filter_res;
    s.lim = sp.amp_limit;
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
    m.volume = sp.volume;
    m.amp = sp.amp_type;
    m.filter_type = sp.filter_type;
    m.cutoff = sp.filter_cutoff;
    m.res = sp.filter_res;
    m.lim = sp.amp_limit;
    m.pan = sp.mixer_pan;
    m.dry = sp.mixer_dry;
    m.cho = sp.mixer_chorus;
    m.del = sp.mixer_delay;
    m.rev = sp.mixer_reverb;
}

static void engineSamplerToLibSynthParams(const engine::SamplerState& s,
                                           m8::SynthParams& sp) {
    sp.volume = s.volume;
    sp.amp_type = s.amp;
    sp.filter_type = s.filter_type;
    sp.filter_cutoff = s.cutoff;
    sp.filter_res = s.res;
    sp.amp_limit = s.lim;
    sp.mixer_pan = s.pan;
    sp.mixer_dry = s.dry;
    sp.mixer_chorus = s.cho;
    sp.mixer_delay = s.del;
    sp.mixer_reverb = s.rev;
}

static void engineMacrosynToLibSynthParams(const engine::MacrosynState& ms,
                                            m8::SynthParams& sp) {
    sp.volume = ms.volume;
    sp.amp_type = ms.amp;
    sp.filter_type = ms.filter_type;
    sp.filter_cutoff = ms.cutoff;
    sp.filter_res = ms.res;
    sp.amp_limit = ms.lim;
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

    // Tables (library: 0..255, engine: 0..255)
    for (size_t t = 0; t < song.tables.size() && t < engine::Sequencer::NUM_TABLES; ++t) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = song.tables[t].steps[r];
            auto& dst = seq.tables[t][r];
            dst.transp = static_cast<int8_t>(src.transpose);
            dst.vol = src.velocity;
            dst.fx[0] = {libFxToEngine(src.fx1.command), src.fx1.value};
            dst.fx[1] = {libFxToEngine(src.fx2.command), src.fx2.value};
            dst.fx[2] = {libFxToEngine(src.fx3.command), src.fx3.value};
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
    // PROJECT > SCALE selects the ACTIVE scale index, measured on fw 6.5.2 --
    // stepping it to 08 made the SCALE view follow to scale 08 and the row read
    // "08 C MINOR PENTATON". It is NOT the key, which stayed C throughout; the
    // library's name for this byte is misleading.
    // OPEN: whether 0xBB holds the index alone or packs index and key into
    // nibbles. Every V4 file we have reads 0 there, so nothing distinguishes
    // them -- masking to the low nibble is safe under either reading. The key
    // half has no located byte at all, so project.key is not loaded and resets
    // to C. One device save with a non-zero scale index settles it.
    state.project.scale = song.key & 0x0F;
    state.project.key   = 0;
    state.project.groove = 0;

    // Tempo
    float t = song.tempo;
    state.bpm = static_cast<int>(t);
    state.bpm_frac = static_cast<int>(std::round((t - state.bpm) * 100.0f));

    // Mixer. The file's single master gain is the mixer's MIX control, not the
    // screen's top-line SPEAKER VOL -- that one is ours and is never persisted
    // (MIXER_SPEC.md §3). This was loading into out_vol, which is why mix_vol
    // was always its compile-time default (hw_findings.md §UI-4c).
    state.mixer.mix_vol = song.mixer_settings.master_volume;
    for (int i = 0; i < 8; ++i)
        state.mixer.track_vol[i] = song.mixer_settings.track_volume[i];
    state.mixer.cho_vol = song.mixer_settings.chorus_volume;
    state.mixer.del_vol = song.mixer_settings.delay_volume;
    state.mixer.rev_vol = song.mixer_settings.reverb_volume;
    state.mixer.lim_val = song.mixer_settings.master_limit;
    // The DJ filter is read at +25..+27, which holds from 2.5 on: `dj_filter`
    // is 0x80 -- the documented "off" -- in twelve of the thirteen v2.5 songs
    // here (MOVING BACK sets 0x90, a real value) and in every 2.7/3.0/4.x file.
    // v1.4 does not fit: its analog/USB input block is laid out differently, so
    // the same bytes carry no DJ filter and GOTEBORG read 0x2F there, engaging
    // a lowpass the song never asked for. 2.5 is already a format boundary in
    // this reader -- it is where scales start being stored. Below it the filter
    // stays off rather than being driven from bytes we cannot place.
    if (song.version.at_least(2, 5)) {
        state.mixer.djf_freq = song.mixer_settings.dj_filter;
        // dj_peak is the DJ filter's RESONANCE, not OTT (§UI-9). OTT lives at
        // 0xED, which the library does not model; loadMixerTail() picks it up.
        state.mixer.djf_res = song.mixer_settings.dj_peak;
        state.mixer.djf_typ = song.mixer_settings.dj_filter_type;
    }

    // Analog input (mono or stereo — engine takes left/mono channel)
    std::visit([&](auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, m8::InputMixerSettings>) {
            state.mixer.in_vol = arg.volume;
            state.mixer.in_cho = arg.chorus;
            state.mixer.in_del = arg.delay;
            state.mixer.in_rev = arg.reverb;
        } else {
            state.mixer.in_vol = arg.first.volume;
            state.mixer.in_cho = arg.first.chorus;
            state.mixer.in_del = arg.first.delay;
            state.mixer.in_rev = arg.first.reverb;
        }
    }, song.mixer_settings.analog_input);

    // USB input
    state.mixer.usb_vol = song.mixer_settings.usb_input.volume;
    state.mixer.usb_cho = song.mixer_settings.usb_input.chorus;
    state.mixer.usb_del = song.mixer_settings.usb_input.delay;
    state.mixer.usb_rev = song.mixer_settings.usb_input.reverb;

    // Effects
    // Effects are NOT read from song.effects_settings -- the library's offsets
    // for that block are wrong (see loadEffectsBlock). loadSong() fills
    // state.effects from the raw bytes instead, after this function returns.

    // EQ banks (EQ_SPEC.md §3-4). The library gives us however many the song's
    // version carries -- 32 on V4, 128 on V4.1+ -- so take what is there and
    // leave the rest at their factory defaults.
    state.eqBankCount = static_cast<int>(
        std::min(song.eqs.size(), static_cast<size_t>(engine::kMaxEqBanks)));
    for (size_t i = 0; i < song.eqs.size() && i < static_cast<size_t>(engine::kMaxEqBanks); ++i) {
        const auto& src = song.eqs[i];
        auto& dst = state.eqs[i];
        const m8::EqBand* srcBands[3] = { &src.low, &src.mid, &src.high };
        engine::EqBand* dstBands[3] = { &dst.low, &dst.mid, &dst.high };
        for (int b = 0; b < 3; ++b) {
            dstBands[b]->rawModeType = srcBands[b]->mode.value;
            // Decode the packed byte ourselves rather than via eq_type(): that
            // accessor clamps anything above 5 to Bell, which would silently
            // rewrite an ALLPASS band (EQ_SPEC.md §3).
            dstBands[b]->type = srcBands[b]->mode.value & 0x7;
            dstBands[b]->mode = (srcBands[b]->mode.value >> 5) & 0x7;
            dstBands[b]->freq = (int(srcBands[b]->freq) << 8) | int(srcBands[b]->freq_fin);
            dstBands[b]->gain = static_cast<int16_t>(
                (uint16_t(srcBands[b]->level) << 8) | uint16_t(srcBands[b]->level_fin));
            dstBands[b]->q = srcBands[b]->q;
        }
    }

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
                // DETUNE: the file byte and the engine field use the SAME
                // convention -- unsigned, 0x80 == centre -- so this is a
                // straight copy. M8_SAMPLER_COMPLETION_SPEC.md 1.3 says the file
                // is signed with 0x00 == centre and re-centres it; that was
                // wrong, and it detuned every device-written sampler by -8
                // semitones. Corrected 2026-08-24 against the files themselves:
                //   0x80  every instrument in all 13 v1.4/v2.5 factory bundles,
                //         in v3.0.4 TEST-FILE, and in every 6.0/6.5 device save
                //         (device_golden/Sampler.m8s, probe_ottA0, scope_rel_*,
                //         TEST01) -- i.e. an untouched DETUNE reads 0x80.
                //   0x00  only in files soft-mate itself wrote, where the old
                //         save path emitted detune-0x80. The spec's evidence was
                //         its own round-trip, which held under either reading.
                // The engine agrees independently: the REPITCH and BPM branches
                // in SynthVoice.cpp read this same field as STEPS/BPM with 0x80
                // the default, and their constants were measured on hardware
                // (fw 6.5.2) including a STEPS=0x40 point. A signed re-centre
                // would turn that 0x40 into 192 and stretch the loop 3x.
                s.detune  = inst.synth_params.fine_pitch;
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
                ms.volume = inst.synth_params.volume;
                ms.amp = inst.synth_params.amp_type;
                ms.filter_type = inst.synth_params.filter_type;
                ms.cutoff = inst.synth_params.filter_cutoff;
                ms.res = inst.synth_params.filter_res;
                ms.lim = inst.synth_params.amp_limit;
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
                h.volume = inst.synth_params.volume;
                h.amp = inst.synth_params.amp_type;
                h.filter_type = inst.synth_params.filter_type;
                h.cutoff = inst.synth_params.filter_cutoff;
                h.res = inst.synth_params.filter_res;
                h.lim = inst.synth_params.amp_limit;
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
                fm.volume      = inst.synth_params.volume;
                fm.amp         = inst.synth_params.amp_type;
                fm.filter_type = inst.synth_params.filter_type;
                fm.cutoff      = inst.synth_params.filter_cutoff;
                fm.res         = inst.synth_params.filter_res;
                fm.lim         = inst.synth_params.amp_limit;
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
                ws.volume      = inst.synth_params.volume;
                ws.amp         = inst.synth_params.amp_type;
                ws.filter_type = inst.synth_params.filter_type;
                ws.cutoff      = inst.synth_params.filter_cutoff;
                ws.res         = inst.synth_params.filter_res;
                ws.lim         = inst.synth_params.amp_limit;
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
            // Overlay modeled FX; leave UNKNOWN (unmodeled) slots as the re-parsed
            // original bytes so commands the engine doesn't model survive save.
            if (src.fx[0].cmd != engine::FxCmd::UNKNOWN)
                dst.fx1 = {engineFxToLib(src.fx[0].cmd), src.fx[0].val};
            if (src.fx[1].cmd != engine::FxCmd::UNKNOWN)
                dst.fx2 = {engineFxToLib(src.fx[1].cmd), src.fx[1].val};
            if (src.fx[2].cmd != engine::FxCmd::UNKNOWN)
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

    // Tables
    song.tables.resize(m8::Song::N_TABLES);
    for (size_t t = 0; t < m8::Song::N_TABLES; ++t) {
        for (int r = 0; r < 16; ++r) {
            const auto& src = seq.tables[t][r];
            auto& dst = song.tables[t].steps[r];
            dst.transpose = static_cast<uint8_t>(src.transp);
            dst.velocity = src.vol;
            dst.fx1 = {engineFxToLib(src.fx[0].cmd), src.fx[0].val};
            dst.fx2 = {engineFxToLib(src.fx[1].cmd), src.fx[1].val};
            dst.fx3 = {engineFxToLib(src.fx[2].cmd), src.fx[2].val};
        }
    }

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

    // Global KEY. saveNewSong writes the header itself and emits song.key, so
    // this is what carries the field down the from-a-template path; the
    // save-in-place path patches the same byte in saveUnwrittenBlocks.
    song.key = static_cast<uint8_t>(state.project.scale & 0xFF);

    // Mixer. master_volume is the MIX control; the screen's SPEAKER VOL
    // (state.mixer.out_vol) is ours alone and is deliberately not written.
    song.mixer_settings.master_volume = state.mixer.mix_vol;
    for (int i = 0; i < 8; ++i)
        song.mixer_settings.track_volume[i] = state.mixer.track_vol[i];
    song.mixer_settings.chorus_volume = state.mixer.cho_vol;
    song.mixer_settings.delay_volume = state.mixer.del_vol;
    song.mixer_settings.reverb_volume = state.mixer.rev_vol;
    song.mixer_settings.master_limit = state.mixer.lim_val;
    song.mixer_settings.dj_filter = state.mixer.djf_freq;
    song.mixer_settings.dj_peak = state.mixer.djf_res;   // RES, not OTT (§UI-9)
    song.mixer_settings.dj_filter_type = state.mixer.djf_typ;

    // analog_input / usb_input are deliberately NOT written. soft-mate has no
    // inputs and no UI for them, so the engine's copies can only ever be what
    // the file already said -- and rebuilding them here was actively lossy: the
    // engine has no right-channel fields, so a stereo analog input came back as
    // mono and its right-channel bytes were discarded on every save
    // (hw_findings.md §UI-4e). Leaving them alone lets save-by-overlay preserve
    // the original bytes exactly. See MIXER_SPEC.md §3.

    // Effects are NOT written through song.effects_settings -- the library's
    // offsets for that block are wrong (see saveEffectsBlock), and its writer
    // would lay the fields down in the wrong places. Both save paths patch the
    // block into the finished image instead.

    // EQ banks. Overlay onto the song's own array so its size (32 or 128, by
    // version) is preserved -- never resize it.
    for (size_t i = 0; i < song.eqs.size() && i < static_cast<size_t>(engine::kMaxEqBanks); ++i) {
        auto& dst = song.eqs[i];
        const auto& src = state.eqs[i];
        m8::EqBand* dstBands[3] = { &dst.low, &dst.mid, &dst.high };
        const engine::EqBand* srcBands[3] = { &src.low, &src.mid, &src.high };
        for (int b = 0; b < 3; ++b) {
            // Rebuild the packed byte on top of the original so bits 3-4 --
            // which have no known meaning -- survive untouched. Rebuilding it
            // from type|mode alone would zero them and break the byte-identical
            // round-trip on any file that uses them (EQ_SPEC.md §3).
            const uint8_t base = static_cast<uint8_t>(srcBands[b]->rawModeType);
            dstBands[b]->mode.value = static_cast<uint8_t>(
                (base & ~0xE7) | (srcBands[b]->type & 0x7) | ((srcBands[b]->mode & 0x7) << 5));
            dstBands[b]->freq     = static_cast<uint8_t>((srcBands[b]->freq >> 8) & 0xFF);
            dstBands[b]->freq_fin = static_cast<uint8_t>(srcBands[b]->freq & 0xFF);
            const uint16_t g = static_cast<uint16_t>(static_cast<int16_t>(srcBands[b]->gain));
            dstBands[b]->level     = static_cast<uint8_t>((g >> 8) & 0xFF);
            dstBands[b]->level_fin = static_cast<uint8_t>(g & 0xFF);
            dstBands[b]->q = static_cast<uint8_t>(srcBands[b]->q);
        }
    }

    // Instruments — overlay the fields our engine models onto the ORIGINAL song
    // instruments. We only touch modeled/screen-exposed fields; every other byte
    // (pitch, env_*_amt, lfo_*_amt, mods, associated_eq, sample_path, number) is
    // preserved from the file that was re-read at the start of saveSong().
    //
    // amp_limit LEFT this list on 2026-08-19: it is the device's LIM field and is
    // modelled now. See AGENTS.md 7's INSTRUMENT byte map -- AMP is amp_type and
    // LIM is amp_limit, and this file had both wired one field across.
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
            // DETUNE: same convention on both sides, 0x80 == centre. See the
            // load path for why the spec's signed re-centre was wrong.
            smp.synth_params.fine_pitch = static_cast<uint8_t>(s.detune);
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
            hyp.synth_params.volume = static_cast<uint8_t>(h.volume);
            hyp.synth_params.amp_type = static_cast<uint8_t>(h.amp);
            hyp.synth_params.filter_type = static_cast<uint8_t>(h.filter_type);
            hyp.synth_params.filter_cutoff = static_cast<uint8_t>(h.cutoff);
            hyp.synth_params.filter_res = static_cast<uint8_t>(h.res);
            hyp.synth_params.amp_limit = static_cast<uint8_t>(h.lim);
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
            fms.synth_params.volume        = static_cast<uint8_t>(fm.volume);
            fms.synth_params.amp_type      = static_cast<uint8_t>(fm.amp);
            fms.synth_params.filter_type   = static_cast<uint8_t>(fm.filter_type);
            fms.synth_params.filter_cutoff = static_cast<uint8_t>(fm.cutoff);
            fms.synth_params.filter_res    = static_cast<uint8_t>(fm.res);
            fms.synth_params.amp_limit     = static_cast<uint8_t>(fm.lim);
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
            wvs.synth_params.volume        = static_cast<uint8_t>(ws.volume);
            wvs.synth_params.amp_type      = static_cast<uint8_t>(ws.amp);
            wvs.synth_params.filter_type   = static_cast<uint8_t>(ws.filter_type);
            wvs.synth_params.filter_cutoff = static_cast<uint8_t>(ws.cutoff);
            wvs.synth_params.filter_res    = static_cast<uint8_t>(ws.res);
            wvs.synth_params.amp_limit     = static_cast<uint8_t>(ws.lim);
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
            wvs.size = 0x20;
            wvs.mult = 0x00;
            wvs.warp = 0x00;
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
        // The main mix and effect EQs are not part of the parsed Song, so they
        // come straight from the file bytes (EQ_SPEC.md §4c).
        loadBusEqs(song, data, res.state);
        loadEffectsBlock(data, res.state.effects, song.version); // library offsets are wrong
        loadMixerTail(data, res.state.mixer, song.version);      // OTT; library stops at +27
        loadScalesBlock(data, res.state, song.version);          // library reads offsets unsigned

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
        saveUnwrittenBlocks(song, state, out); // modeled, but Song::write never emits them
        saveEffectsBlock(state.effects, out); // measured offsets, not the library's
        saveBusEqs(song, state, out);        // not modeled by the library; patched in
        saveScalesBlock(state, out);         // Song::write never emits these either
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
        auto out = writer.finish();
        // Effects last, at the measured offsets. This used to be
        // `song.effects_settings.write(writer, ...)`, which lays the fields out
        // in the library's wrong positions -- so a song authored from a
        // template got a scrambled effects block. Everything this does not
        // touch (MOD TYPE, SHIMMER, the unknown runs) keeps the template's
        // bytes, which is the same preservation rule the rest of the file gets.
        saveEffectsBlock(state.effects, out);
        // Scales the same way: Song::write never emits them, so without this a
        // song authored from a template would carry the TEMPLATE's scales.
        saveScalesBlock(state, out);
        // The mixer tail likewise: MixerSettings::write zeroes +28..+31 on its
        // way past, so every one of these has to be written back or the
        // authored value is silently lost. SOFT CLIP is the one that bites --
        // it defaults ON and a zero here would switch it off, which for our own
        // songs means clipping, since their levels assume the saturation.
        if (out.size() > kMixerOffset + kMixerBlockSize) {
            uint8_t* m = out.data() + kMixerOffset;
            m[kMixLimAtk]   = static_cast<uint8_t>(state.mixer.lim_atk);
            m[kMixLimRel]   = static_cast<uint8_t>(state.mixer.lim_rel);
            m[kMixSoftClip] = static_cast<uint8_t>(state.mixer.soft_clip);
            m[kMixOtt]      = static_cast<uint8_t>(state.mixer.ott);
        }
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
