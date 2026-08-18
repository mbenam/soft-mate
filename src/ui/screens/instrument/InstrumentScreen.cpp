#include "InstrumentScreen.h"
#include "InstrumentSamplerLayout.h"
#include "InstrumentMacrosynLayout.h"
#include "InstrumentFmsynthLayout.h"
#include "InstrumentHypersynLayout.h"
#include "InstrumentWavsynthLayout.h"
#include "ui/Theme.h"
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace m8 {
namespace ui {
namespace instrument {

struct FMOpClipboard {
    bool hasData = false;
    m8::engine::FMSynthState::FMOp op;
};
static FMOpClipboard s_fmOpClipboard;

struct HypChordClipboard {
    bool hasData = false;
    int notes[6] = {};
};
static HypChordClipboard s_hypChordClipboard;

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "SLIDER_BG") return GetThemeColor("LABEL_DIM");
    return GetThemeColor(colorName);
}

// Helper: Formats an integer as a 2-digit uppercase hex string
static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (static_cast<unsigned int>(value) & 0xFF);
    return ss.str();
}

static const char* const kMacroShapes[44] = {
    "CSAW", "MORPH", "SAW SQUARE", "SINE TRIANGLE", "BUZZ",
    "SQUARE SUB", "SAW SUB", "SQUARE SYNC", "SAW SYNC",
    "TRIPLE SAW", "TRIPLE SQUARE", "TRIPLE TRIANGLE", "TRIPLE SIN", "TRIPLE RNG",
    "SAW SWARM", "SAW COMB", "TOY",
    "DIGITAL FILTER LP", "DIGITAL FILTER PK", "DIGITAL FILTER BP", "DIGITAL FILTER HP",
    "VOSIM", "VOWEL", "VOWEL FOF", "HARMONICS",
    "FM", "FEEDBACK FM", "CHAOTIC FEEDBACK FM",
    "PLUCKED", "BOWED", "BLOWN", "FLUTED",
    "STRUCK BELL", "STRUCK DRUM", "KICK", "CYMBAL", "SNARE",
    "WAVETABLES", "WAVE MAP", "WAV LINE", "WAV PARAPHONIC",
    "FILTERED NOISE", "TWIN PEAKS NOISE", "CLOCKED NOISE"
};

static const char* const kFilterModes[8] = {
    "OFF", "LP ", "HP ", "BP ", "BS ", "LP>HP", "ZDF LP", "ZDF HP"
};

static const char* const kWavFilterModes[12] = {
    "OFF    ", "LOWPASS", "HIGHPAS", "BANDPAS", "BANDSTP", "LP>HP  ",
    "ZDF LP ", "ZDF HP ", "WAV LP ", "WAV HP ", "WAV BP ", "WAV BS "
};

static const char* const kLimModes[9] = {
    "CLIP", "SIN ", "FOLD", "WRAP", "POST", "POST:AD", "POST:W1", "POST:W2", "POST:W3"
};

static const char* const kPlayModes[15] = {
    "FWD", "REV", "FWDLOOP", "REVLOOP", "FWD PP", "REV PP", "OSC", "OSC REV",
    "OSC PP", "REPITCH", "REP.REV", "REP.PP", "REP.BPM", "BPM.REV", "BPM.PP"
};

static const char* const kFMShapeNames[12] = {
    "SIN", "SW2", "SW3", "SW4", "SW5", "SW6", "TRI", "SAW", "SQU", "PUL", "IMP", "NOISE"
};

static const char* const kFMAlgoNames[12] = {
    "A > B > C > D",
    "[A + B] > C > D",
    "[A > B + C] > D",
    "[A > B + A > C] > D",
    "[A + B + C] > D",
    "[A > B > C] + D",
    "[A > B > C] + [A > B > D]",
    "[A > B] + [C > D]",
    "[A > B] + [A > C] + [A > D]",
    "[A > B] + [A > C] + D",
    "[A > B] + C + D",
    "A + B + C + D"
};

static const char* const kFMModNames[17] = {
    "-----",
    "1LEV", "1RAT", "1PIT", "1FBK",
    "2LEV", "2RAT", "2PIT", "2FBK",
    "3LEV", "3RAT", "3PIT", "3FBK",
    "4LEV", "4RAT", "4PIT", "4FBK"
};

static uint8_t ModIndexToByte(int idx) {
    if (idx <= 0 || idx > 16) return 0;
    int zeroBased = idx - 1;
    int src = zeroBased / 4;
    int dest = (zeroBased % 4) + 1;
    return static_cast<uint8_t>((src << 4) | dest);
}

static int ByteToModIndex(uint8_t b) {
    int dest = b & 0x0F;
    int src = (b >> 4) & 0x0F;
    if (dest < 1 || dest > 4 || src < 0 || src > 3) return 0;
    return 1 + src * 4 + (dest - 1);
}

static int GetOpIndexFromCursor(CursorId cid) {
    using C = CursorId;
    switch (cid) {
    case C::FM_OP_A_SHAPE: case C::FM_OP_A_RATIO: case C::FM_OP_A_LEV: case C::FM_OP_A_FB: case C::FM_OP_A_MOD1: case C::FM_OP_A_MOD2: return 0;
    case C::FM_OP_B_SHAPE: case C::FM_OP_B_RATIO: case C::FM_OP_B_LEV: case C::FM_OP_B_FB: case C::FM_OP_B_MOD1: case C::FM_OP_B_MOD2: return 1;
    case C::FM_OP_C_SHAPE: case C::FM_OP_C_RATIO: case C::FM_OP_C_LEV: case C::FM_OP_C_FB: case C::FM_OP_C_MOD1: case C::FM_OP_C_MOD2: return 2;
    case C::FM_OP_D_SHAPE: case C::FM_OP_D_RATIO: case C::FM_OP_D_LEV: case C::FM_OP_D_FB: case C::FM_OP_D_MOD1: case C::FM_OP_D_MOD2: return 3;
    default: return -1;
    }
}

// Helper: Resolves the live string value for a given Field ID
static std::string ResolveInstrumentValue(CursorId fieldId, const engine::Instrument& inst) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    bool isHyp = (inst.type == engine::InstType::INST_HYPERSYN);
    bool isFm  = (inst.type == engine::InstType::INST_FMSYNTH);
    bool isWav = (inst.type == engine::InstType::INST_WAVSYNTH);

    if (fieldId == C::TYPE) {
        if (isMac) return "MACROSYN";
        if (isHyp) return "HYPERSYN";
        if (isFm)  return "FMSYNTH ";
        if (isWav) return "WAVSYNTH";
        return "SAMPLER ";
    }
    if (fieldId == C::NAME) return inst.name;
    if (fieldId == C::CMD_LOAD) return "LOAD";
    if (fieldId == C::CMD_SAVE) return "SAVE";
    if (fieldId == C::SAMPLE_LOAD) {
        if (inst.sampler.samplePath[0] != '\0') {
            std::string p = inst.sampler.samplePath;
            size_t slash = p.find_last_of("/\\");
            std::string name = (slash != std::string::npos) ? p.substr(slash + 1) : p;
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) name = name.substr(0, dot);
            return name;
        }
        return "LOAD";
    }
    if (fieldId == C::SAMPLE_REC) {
        return (inst.sampler.samplePath[0] != '\0') ? "EDIT" : "REC.";
    }

    if (fieldId == C::TRANSP) return (isWav ? inst.wav.transp : (isHyp ? inst.hyper.transp : (isMac ? inst.macrosyn.transp : (isFm ? inst.fm.transp : inst.sampler.transp)))) ? "ON" : "OFF";
    int eq = isWav ? inst.wav.eq : (isHyp ? inst.hyper.eq : (isMac ? inst.macrosyn.eq : (isFm ? inst.fm.eq : inst.sampler.eq)));
    if (fieldId == C::EQ) return eq == 0 ? "--" : ToHex(eq);

    // Enums that have separate string accents
    if (fieldId == C::FILTER) return ToHex(isWav ? inst.wav.filter_type : (isHyp ? inst.hyper.filter_type : (isMac ? inst.macrosyn.filter_type : (isFm ? inst.fm.filter_type : inst.sampler.filter_type))));
    if (fieldId == C::PLAY) return ToHex(inst.sampler.play);
    if (fieldId == C::LIM) return ToHex(isWav ? inst.wav.lim : (isHyp ? inst.hyper.lim : (isMac ? inst.macrosyn.lim : (isFm ? inst.fm.lim : inst.sampler.lim))));
    if (fieldId == C::SLICE) return ToHex(inst.sampler.slice);

    // Standard Hex values
    if (fieldId == C::TBL_TIC) return ToHex(isWav ? inst.wav.tbl_tic : (isHyp ? inst.hyper.tbl_tic : (isMac ? inst.macrosyn.tbl_tic : (isFm ? inst.fm.tbl_tic : inst.sampler.tbl_tic))));
    if (fieldId == C::START) return ToHex(inst.sampler.start);
    if (fieldId == C::LOOP_ST) return ToHex(inst.sampler.loop_st);
    if (fieldId == C::LENGTH) return ToHex(inst.sampler.length);
    if (fieldId == C::DETUNE) return ToHex(inst.sampler.detune);
    if (fieldId == C::DEGRADE) return ToHex(isMac ? inst.macrosyn.degrade : inst.sampler.degrade);
    if (fieldId == C::CUTOFF) return ToHex(isWav ? inst.wav.cutoff : (isHyp ? inst.hyper.cutoff : (isMac ? inst.macrosyn.cutoff : (isFm ? inst.fm.cutoff : inst.sampler.cutoff))));
    if (fieldId == C::RES) return ToHex(isWav ? inst.wav.res : (isHyp ? inst.hyper.res : (isMac ? inst.macrosyn.res : (isFm ? inst.fm.res : inst.sampler.res))));
    if (fieldId == C::AMP) return ToHex(isWav ? inst.wav.amp : (isHyp ? inst.hyper.amp : (isMac ? inst.macrosyn.amp : (isFm ? inst.fm.amp : inst.sampler.amp))));
    if (fieldId == C::PAN) return ToHex(isWav ? inst.wav.pan : (isHyp ? inst.hyper.pan : (isMac ? inst.macrosyn.pan : (isFm ? inst.fm.pan : inst.sampler.pan))));
    if (fieldId == C::DRY) return ToHex(isWav ? inst.wav.dry : (isHyp ? inst.hyper.dry : (isMac ? inst.macrosyn.dry : (isFm ? inst.fm.dry : inst.sampler.dry))));
    if (fieldId == C::CHO) return ToHex(isWav ? inst.wav.cho : (isHyp ? inst.hyper.cho : (isMac ? inst.macrosyn.cho : (isFm ? inst.fm.cho : inst.sampler.cho))));
    if (fieldId == C::DEL) return ToHex(isWav ? inst.wav.del : (isHyp ? inst.hyper.del : (isMac ? inst.macrosyn.del : (isFm ? inst.fm.del : inst.sampler.del))));
    if (fieldId == C::REV) return ToHex(isWav ? inst.wav.rev : (isHyp ? inst.hyper.rev : (isMac ? inst.macrosyn.rev : (isFm ? inst.fm.rev : inst.sampler.rev))));

    if (isWav && fieldId == C::SHAPE) return ToHex(inst.wav.shape);
    if (fieldId == C::WAV_SIZE) return ToHex(inst.wav.size);
    if (fieldId == C::WAV_MULT) return ToHex(inst.wav.mult);
    if (fieldId == C::WAV_WARP) return ToHex(inst.wav.warp);
    if (fieldId == C::WAV_SCAN) return ToHex(inst.wav.scan);

    if (fieldId == C::SHAPE) return ToHex(inst.macrosyn.shape);
    if (fieldId == C::TIMBRE) return ToHex(inst.macrosyn.timbre);
    if (fieldId == C::COLOR) return ToHex(inst.macrosyn.color);
    if (fieldId == C::REDUX) return ToHex(inst.macrosyn.redux);

    // FM Synth
    if (fieldId == C::FM_ALGO) return ToHex(inst.fm.algo);
    if (fieldId == C::FM_OP_A_SHAPE) return kFMShapeNames[std::clamp(inst.fm.ops[0].shape, 0, 11)];
    if (fieldId == C::FM_OP_B_SHAPE) return kFMShapeNames[std::clamp(inst.fm.ops[1].shape, 0, 11)];
    if (fieldId == C::FM_OP_C_SHAPE) return kFMShapeNames[std::clamp(inst.fm.ops[2].shape, 0, 11)];
    if (fieldId == C::FM_OP_D_SHAPE) return kFMShapeNames[std::clamp(inst.fm.ops[3].shape, 0, 11)];

    char rBuf[16];
    if (fieldId == C::FM_OP_A_RATIO) { std::snprintf(rBuf, sizeof(rBuf), "%02X.%02X", inst.fm.ops[0].ratio & 0xFF, inst.fm.ops[0].ratio_fine & 0xFF); return rBuf; }
    if (fieldId == C::FM_OP_B_RATIO) { std::snprintf(rBuf, sizeof(rBuf), "%02X.%02X", inst.fm.ops[1].ratio & 0xFF, inst.fm.ops[1].ratio_fine & 0xFF); return rBuf; }
    if (fieldId == C::FM_OP_C_RATIO) { std::snprintf(rBuf, sizeof(rBuf), "%02X.%02X", inst.fm.ops[2].ratio & 0xFF, inst.fm.ops[2].ratio_fine & 0xFF); return rBuf; }
    if (fieldId == C::FM_OP_D_RATIO) { std::snprintf(rBuf, sizeof(rBuf), "%02X.%02X", inst.fm.ops[3].ratio & 0xFF, inst.fm.ops[3].ratio_fine & 0xFF); return rBuf; }

    if (fieldId == C::FM_OP_A_LEV) return ToHex(inst.fm.ops[0].level);
    if (fieldId == C::FM_OP_A_FB)  return ToHex(inst.fm.ops[0].feedback);
    if (fieldId == C::FM_OP_B_LEV) return ToHex(inst.fm.ops[1].level);
    if (fieldId == C::FM_OP_B_FB)  return ToHex(inst.fm.ops[1].feedback);
    if (fieldId == C::FM_OP_C_LEV) return ToHex(inst.fm.ops[2].level);
    if (fieldId == C::FM_OP_C_FB)  return ToHex(inst.fm.ops[2].feedback);
    if (fieldId == C::FM_OP_D_LEV) return ToHex(inst.fm.ops[3].level);
    if (fieldId == C::FM_OP_D_FB)  return ToHex(inst.fm.ops[3].feedback);

    if (fieldId == C::FM_OP_A_MOD1) return kFMModNames[ByteToModIndex(inst.fm.ops[0].mod_a)];
    if (fieldId == C::FM_OP_B_MOD1) return kFMModNames[ByteToModIndex(inst.fm.ops[1].mod_a)];
    if (fieldId == C::FM_OP_C_MOD1) return kFMModNames[ByteToModIndex(inst.fm.ops[2].mod_a)];
    if (fieldId == C::FM_OP_D_MOD1) return kFMModNames[ByteToModIndex(inst.fm.ops[3].mod_a)];

    if (fieldId == C::FM_OP_A_MOD2) return kFMModNames[ByteToModIndex(inst.fm.ops[0].mod_b)];
    if (fieldId == C::FM_OP_B_MOD2) return kFMModNames[ByteToModIndex(inst.fm.ops[1].mod_b)];
    if (fieldId == C::FM_OP_C_MOD2) return kFMModNames[ByteToModIndex(inst.fm.ops[2].mod_b)];
    if (fieldId == C::FM_OP_D_MOD2) return kFMModNames[ByteToModIndex(inst.fm.ops[3].mod_b)];

    if (fieldId == C::FM_MOD1) return ToHex(inst.fm.mod1);
    if (fieldId == C::FM_MOD2) return ToHex(inst.fm.mod2);
    if (fieldId == C::FM_MOD3) return ToHex(inst.fm.mod3);
    if (fieldId == C::FM_MOD4) return ToHex(inst.fm.mod4);

    // HyperSynth
    if (fieldId == C::HYP_SCALE) return ToHex(inst.hyper.scale);
    if (fieldId == C::HYP_CHORD_BANK) return ToHex(inst.hyper.chord_bank);
    int hBank = std::clamp(inst.hyper.chord_bank, 0, 15);
    if (fieldId == C::HYP_CHORD_N1) return ToHex(inst.hyper.chords[hBank][0]);
    if (fieldId == C::HYP_CHORD_N2) return ToHex(inst.hyper.chords[hBank][1]);
    if (fieldId == C::HYP_CHORD_N3) return ToHex(inst.hyper.chords[hBank][2]);
    if (fieldId == C::HYP_CHORD_N4) return ToHex(inst.hyper.chords[hBank][3]);
    if (fieldId == C::HYP_CHORD_N5) return ToHex(inst.hyper.chords[hBank][4]);
    if (fieldId == C::HYP_CHORD_N6) return ToHex(inst.hyper.chords[hBank][5]);
    if (fieldId == C::HYP_SHIFT) return ToHex(inst.hyper.shift);
    if (fieldId == C::HYP_SWARM) return ToHex(inst.hyper.swarm);
    if (fieldId == C::HYP_WIDTH) return ToHex(inst.hyper.width);
    if (fieldId == C::HYP_SUBOSC) return ToHex(inst.hyper.subosc);

    return "--";
}

// Helper: Resolves the accent string for enums
static std::string ResolveInstrumentAccent(CursorId fieldId, const engine::Instrument& inst, const std::string& fallback) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    bool isHyp = (inst.type == engine::InstType::INST_HYPERSYN);
    bool isFm  = (inst.type == engine::InstType::INST_FMSYNTH);
    bool isWav = (inst.type == engine::InstType::INST_WAVSYNTH);

    if (fieldId == C::FILTER) {
        if (isWav) {
            int f = inst.wav.filter_type;
            if (f >= 0 && f < 12) return kWavFilterModes[f];
        } else {
            int filter_type = isHyp ? inst.hyper.filter_type : (isMac ? inst.macrosyn.filter_type : (isFm ? inst.fm.filter_type : inst.sampler.filter_type));
            if (filter_type >= 0 && filter_type < 8) return kFilterModes[filter_type];
        }
    }
    if (fieldId == C::PLAY) {
        if (inst.sampler.play >= 0 && inst.sampler.play < 15) return kPlayModes[inst.sampler.play];
    }
    if (fieldId == C::LIM) {
        int lim = isWav ? inst.wav.lim : (isHyp ? inst.hyper.lim : (isMac ? inst.macrosyn.lim : (isFm ? inst.fm.lim : inst.sampler.lim)));
        if (lim >= 0 && lim < 9) return kLimModes[lim];
    }
    if (fieldId == C::SLICE) {
        return inst.sampler.slice == 0 ? "OFF" : "ON ";
    }
    if (fieldId == C::SHAPE) {
        if (isWav) {
            return WavShapeName(inst.wav.shape);
        }
        if (inst.macrosyn.shape >= 0 && inst.macrosyn.shape < 44) {
            return kMacroShapes[inst.macrosyn.shape];
        }
    }
    if (fieldId == C::FM_ALGO) {
        if (inst.fm.algo >= 0 && inst.fm.algo < 12) {
            return kFMAlgoNames[inst.fm.algo];
        }
    }
    if (fieldId == C::HYP_SCALE) {
        return (inst.hyper.scale == 0) ? "DEFAULT" : "       ";
    }
    return fallback;
}

// Helper: Gets the raw integer value to calculate slider width
static int GetSliderValue(CursorId fieldId, const engine::Instrument& inst) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    bool isHyp = (inst.type == engine::InstType::INST_HYPERSYN);
    bool isFm  = (inst.type == engine::InstType::INST_FMSYNTH);
    bool isWav = (inst.type == engine::InstType::INST_WAVSYNTH);

    if (fieldId == C::START) return inst.sampler.start;
    if (fieldId == C::LOOP_ST) return inst.sampler.loop_st;
    if (fieldId == C::LENGTH) return inst.sampler.length;
    if (fieldId == C::DETUNE) return inst.sampler.detune;

    if (fieldId == C::DEGRADE) return isMac ? inst.macrosyn.degrade : inst.sampler.degrade;
    if (fieldId == C::CUTOFF) return isWav ? inst.wav.cutoff : (isHyp ? inst.hyper.cutoff : (isMac ? inst.macrosyn.cutoff : (isFm ? inst.fm.cutoff : inst.sampler.cutoff)));
    if (fieldId == C::RES) return isWav ? inst.wav.res : (isHyp ? inst.hyper.res : (isMac ? inst.macrosyn.res : (isFm ? inst.fm.res : inst.sampler.res)));
    if (fieldId == C::AMP) return isWav ? inst.wav.amp : (isHyp ? inst.hyper.amp : (isMac ? inst.macrosyn.amp : (isFm ? inst.fm.amp : inst.sampler.amp)));
    if (fieldId == C::PAN) return isWav ? inst.wav.pan : (isHyp ? inst.hyper.pan : (isMac ? inst.macrosyn.pan : (isFm ? inst.fm.pan : inst.sampler.pan)));
    if (fieldId == C::DRY) return isWav ? inst.wav.dry : (isHyp ? inst.hyper.dry : (isMac ? inst.macrosyn.dry : (isFm ? inst.fm.dry : inst.sampler.dry)));
    if (fieldId == C::CHO) return isWav ? inst.wav.cho : (isHyp ? inst.hyper.cho : (isMac ? inst.macrosyn.cho : (isFm ? inst.fm.cho : inst.sampler.cho)));
    if (fieldId == C::DEL) return isWav ? inst.wav.del : (isHyp ? inst.hyper.del : (isMac ? inst.macrosyn.del : (isFm ? inst.fm.del : inst.sampler.del)));
    if (fieldId == C::REV) return isWav ? inst.wav.rev : (isHyp ? inst.hyper.rev : (isMac ? inst.macrosyn.rev : (isFm ? inst.fm.rev : inst.sampler.rev)));

    if (fieldId == C::TIMBRE) return inst.macrosyn.timbre;
    if (fieldId == C::COLOR) return inst.macrosyn.color;
    if (fieldId == C::REDUX) return inst.macrosyn.redux;

    if (fieldId == C::HYP_SWARM) return inst.hyper.swarm;
    if (fieldId == C::HYP_WIDTH) return inst.hyper.width;

    if (fieldId == C::WAV_SIZE) return inst.wav.size;
    if (fieldId == C::WAV_MULT) return inst.wav.mult;
    if (fieldId == C::WAV_WARP) return inst.wav.warp;
    if (fieldId == C::WAV_SCAN) return inst.wav.scan;

    return 0;
}

void RenderInstrumentScreen(Renderer& renderer,
                            const engine::EngineState& engState,
                            int currentInstIndex,
                            CursorId active_cursor_id,
                            int nameCharIndex) {

    const engine::Instrument& currentInst = engState.instruments[currentInstIndex];
    bool isMac = (currentInst.type == engine::InstType::INST_MACROSYN);
    bool isHyp = (currentInst.type == engine::InstType::INST_HYPERSYN);
    bool isFm  = (currentInst.type == engine::InstType::INST_FMSYNTH);
    bool isWav = (currentInst.type == engine::InstType::INST_WAVSYNTH);

    const std::vector<UI_GridCell>& staticText = isWav ? GetWavsynthStaticText() : (isHyp ? GetHypersynStaticText() : (isFm ? GetFmsynthStaticText() : (isMac ? GetMacrosynStaticText() : GetSamplerStaticText())));
    const std::vector<UI_GridCell>& dynamicText = isWav ? GetWavsynthDynamicTextDefaults() : (isHyp ? GetHypersynDynamicTextDefaults() : (isFm ? GetFmsynthDynamicTextDefaults() : (isMac ? GetMacrosynDynamicTextDefaults() : GetSamplerDynamicTextDefaults())));
    const std::unordered_map<CursorId, std::vector<UI_GridCell>>& interactiveFields = isWav ? GetWavsynthInteractiveFields() : (isHyp ? GetHypersynInteractiveFields() : (isFm ? GetFmsynthInteractiveFields() : (isMac ? GetMacrosynInteractiveFields() : GetSamplerInteractiveFields())));

    // Render Static Background Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Render Dynamic Text (Title, Tempo)
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        if (cell.text == "13") {
            textToDraw = ToHex(currentInstIndex);
        } else if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        renderer.drawString(textToDraw, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveInstrumentValue(fieldId, currentInst);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);

            if (comp.role == "slider") {
                int px = comp.col * 8;
                int py = comp.row * 8;
                int max_pixels = comp.width * 8;

                int val = GetSliderValue(fieldId, currentInst);
                int fill_pixels = (val * max_pixels) / 255;

                renderer.fillRectPixel(px, py, fill_pixels, 8, color);
            }
            else {
                std::string drawText = comp.text;
                if (comp.role == "value") drawText = liveText;
                else if (comp.role == "accent") drawText = ResolveInstrumentAccent(fieldId, currentInst, comp.text);
                else if (comp.role == "label" && fieldId == CursorId::DETUNE && !isMac && !isHyp && !isFm && !isWav) {
                    if (currentInst.sampler.play >= 9 && currentInst.sampler.play <= 11) drawText = "STEPS ";
                    else if (currentInst.sampler.play >= 12 && currentInst.sampler.play <= 14) drawText = "BPM   ";
                }

                renderer.drawString(drawText, comp.col, comp.row, color);

                if (isActive && comp.has_cursor_box) {
                    if (fieldId == CursorId::NAME) {
                        renderer.drawBracket(comp.col + std::clamp(nameCharIndex, 0, 11), comp.row, 1, GetThemeColor("CURSOR"));
                    } else {
                        int bracketLen = static_cast<int>(drawText.length());
                        for (const auto& other : components) {
                            if (other.role == "accent") {
                                std::string acc = ResolveInstrumentAccent(fieldId, currentInst, other.text);
                                bracketLen += static_cast<int>(acc.length());
                                break;
                            }
                        }
                        renderer.drawBracket(comp.col, comp.row, bracketLen, GetThemeColor("CURSOR"));
                    }
                }
            }
        }
    }

    if (isHyp) {
        int bank = std::clamp(currentInst.hyper.chord_bank, 0, 15);
        bool activeNotes[24] = {};
        for (int n = 0; n < 6; ++n) {
            int interval = currentInst.hyper.chords[bank][n];
            if (interval >= 0 && interval < 24) {
                activeNotes[interval] = true;
            }
        }

        // Draw the 2-octave mini keyboard (col 8..31 on row 8)
        renderer.fillRectPixel(8 * 8, 8 * 8, 24 * 8, 1, {100, 100, 100, 255});
        renderer.fillRectPixel(8 * 8, 8 * 8 + 7, 24 * 8, 1, {100, 100, 100, 255});

        for (int k = 0; k < 24; ++k) {
            int px = (8 + k) * 8;
            int py = 8 * 8;
            bool isBlack = ((k % 12) == 1 || (k % 12) == 3 || (k % 12) == 6 || (k % 12) == 8 || (k % 12) == 10);

            if (activeNotes[k]) {
                renderer.fillRectPixel(px, py + 1, 8, 6, {0, 255, 255, 255});
            } else if (isBlack) {
                renderer.fillRectPixel(px + 1, py + 1, 6, 4, {100, 100, 100, 255});
                renderer.fillRectPixel(px, py + 1, 1, 6, {40, 40, 40, 255});
                renderer.fillRectPixel(px + 7, py + 1, 1, 6, {40, 40, 40, 255});
            } else {
                renderer.fillRectPixel(px, py + 1, 1, 6, {100, 100, 100, 255});
            }
        }
        renderer.fillRectPixel((8 + 24) * 8 - 1, 8 * 8, 1, 8, {100, 100, 100, 255});
    }
}

void HandleInstrumentInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                            engine::EngineState& uiEngineState, int currentInstIndex,
                            CursorId& cursor_id, int& nameCharIndex, CommandSink& commandSink,
                            ViewManager& viewManager, bool& browserForSongLoad,
                            ::FileBrowser& fileBrowser, InstrumentBrowserMode& instrumentBrowserMode) {
    using C = CursorId;
    const m8::engine::Instrument& inst = uiEngineState.instruments[currentInstIndex];
    bool isMac = (inst.type == m8::engine::InstType::INST_MACROSYN);
    bool isHyp = (inst.type == m8::engine::InstType::INST_HYPERSYN);
    bool isFm  = (inst.type == m8::engine::InstType::INST_FMSYNTH);
    bool isWav = (inst.type == m8::engine::InstType::INST_WAVSYNTH);
    auto navMap = isWav ? GetWavsynthNavMap() : (isHyp ? GetHypersynNavMap() : (isFm ? GetFmsynthNavMap() : (isMac ? GetMacrosynNavMap() : GetSamplerNavMap())));

    int opIdx = GetOpIndexFromCursor(cursor_id);
    bool shiftActive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

    if (isFm && opIdx >= 0 && shiftActive) {
        // [SHIFT] + [OPT] (SDLK_Z or SDLK_LALT) -> Copy OP
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_Z || event.key.key == SDLK_LALT)) {
            s_fmOpClipboard.hasData = true;
            s_fmOpClipboard.op = inst.fm.ops[opIdx];
            return;
        }
        // [SHIFT] + [EDIT] (SDLK_X) -> Paste OP
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_X)) {
            if (s_fmOpClipboard.hasData) {
                const auto& srcOp = s_fmOpClipboard.op;
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_SHAPE, srcOp.shape, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_RATIO, srcOp.ratio, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_RATIO_FINE, srcOp.ratio_fine, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_LEVEL, srcOp.level, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_FB, srcOp.feedback, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_MOD_A, srcOp.mod_a, currentInstIndex, opIdx);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_MOD_B, srcOp.mod_b, currentInstIndex, opIdx);
            }
            return;
        }
    }

    bool isHypChord = (cursor_id == C::HYP_CHORD_BANK || (cursor_id >= C::HYP_CHORD_N1 && cursor_id <= C::HYP_CHORD_N6));
    if (isHyp && isHypChord && shiftActive) {
        int b = std::clamp(inst.hyper.chord_bank, 0, 15);
        // [SHIFT] + [OPT] -> Copy Chord Bank
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_Z || event.key.key == SDLK_LALT)) {
            s_hypChordClipboard.hasData = true;
            for (int i = 0; i < 6; ++i) s_hypChordClipboard.notes[i] = inst.hyper.chords[b][i];
            return;
        }
        // [SHIFT] + [EDIT] -> Paste Chord Bank
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_X)) {
            if (s_hypChordClipboard.hasData) {
                for (int i = 0; i < 6; ++i) {
                    PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_CHORD_NOTE, s_hypChordClipboard.notes[i], currentInstIndex, b, i);
                }
            }
            return;
        }
    }

    if (editHeld && (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP || event.key.key == SDLK_LEFT || event.key.key == SDLK_DOWN)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;

        if (cursor_id == C::TYPE) {
            int newType = std::clamp<int>(static_cast<int>(inst.type) + step, 0, 4);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TYPE, newType, currentInstIndex);
            cursor_id = C::TYPE;
        }
        else if (cursor_id == C::TRANSP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TRANSP, std::clamp<int>((isWav ? inst.wav.transp : (isHyp ? inst.hyper.transp : (isMac ? inst.macrosyn.transp : (isFm ? inst.fm.transp : inst.sampler.transp)))) + step, 0, 1), currentInstIndex);
        else if (cursor_id == C::TBL_TIC) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TBL_TIC, std::clamp<int>((isWav ? inst.wav.tbl_tic : (isHyp ? inst.hyper.tbl_tic : (isMac ? inst.macrosyn.tbl_tic : (isFm ? inst.fm.tbl_tic : inst.sampler.tbl_tic)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::EQ) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_EQ, std::clamp<int>(inst.getEq() + step, 0, uiEngineState.eqBankCount - 1), currentInstIndex);
        else if (cursor_id == C::AMP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_AMP, std::clamp<int>((isWav ? inst.wav.amp : (isHyp ? inst.hyper.amp : (isMac ? inst.macrosyn.amp : (isFm ? inst.fm.amp : inst.sampler.amp)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::LIM) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_LIM, std::clamp<int>((isWav ? inst.wav.lim : (isHyp ? inst.hyper.lim : (isMac ? inst.macrosyn.lim : (isFm ? inst.fm.lim : inst.sampler.lim)))) + step, 0, 8), currentInstIndex);
        else if (cursor_id == C::PAN) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_PAN, std::clamp<int>((isWav ? inst.wav.pan : (isHyp ? inst.hyper.pan : (isMac ? inst.macrosyn.pan : (isFm ? inst.fm.pan : inst.sampler.pan)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DRY) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DRY, std::clamp<int>((isWav ? inst.wav.dry : (isHyp ? inst.hyper.dry : (isMac ? inst.macrosyn.dry : (isFm ? inst.fm.dry : inst.sampler.dry)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::CHO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_CHO, std::clamp<int>((isWav ? inst.wav.cho : (isHyp ? inst.hyper.cho : (isMac ? inst.macrosyn.cho : (isFm ? inst.fm.cho : inst.sampler.cho)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DEL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DEL, std::clamp<int>((isWav ? inst.wav.del : (isHyp ? inst.hyper.del : (isMac ? inst.macrosyn.del : (isFm ? inst.fm.del : inst.sampler.del)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_REV, std::clamp<int>((isWav ? inst.wav.rev : (isHyp ? inst.hyper.rev : (isMac ? inst.macrosyn.rev : (isFm ? inst.fm.rev : inst.sampler.rev)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DEGRADE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DEGRADE, std::clamp<int>((isMac ? inst.macrosyn.degrade : inst.sampler.degrade) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::FILTER) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_FILTER, std::clamp<int>((isWav ? inst.wav.filter_type : (isHyp ? inst.hyper.filter_type : (isMac ? inst.macrosyn.filter_type : (isFm ? inst.fm.filter_type : inst.sampler.filter_type)))) + step, 0, isWav ? 11 : 7), currentInstIndex);
        else if (cursor_id == C::CUTOFF) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_CUTOFF, std::clamp<int>((isWav ? inst.wav.cutoff : (isHyp ? inst.hyper.cutoff : (isMac ? inst.macrosyn.cutoff : (isFm ? inst.fm.cutoff : inst.sampler.cutoff)))) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::RES) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_RES, std::clamp<int>((isWav ? inst.wav.res : (isHyp ? inst.hyper.res : (isMac ? inst.macrosyn.res : (isFm ? inst.fm.res : inst.sampler.res)))) + step, 0, 255), currentInstIndex);

        // WavSynth-specific
        else if (isWav && cursor_id == C::SHAPE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::WAV_SHAPE, std::clamp<int>(inst.wav.shape + step, 0, 0x45), currentInstIndex);
        else if (isWav && cursor_id == C::WAV_SIZE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::WAV_SIZE, std::clamp<int>(inst.wav.size + step, 2, 255), currentInstIndex);
        else if (isWav && cursor_id == C::WAV_MULT) PushParam(commandSink, uiEngineState, m8::engine::ParamID::WAV_MULT, std::clamp<int>(inst.wav.mult + step, 0, 255), currentInstIndex);
        else if (isWav && cursor_id == C::WAV_WARP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::WAV_WARP, std::clamp<int>(inst.wav.warp + step, 0, 255), currentInstIndex);
        else if (isWav && cursor_id == C::WAV_SCAN) PushParam(commandSink, uiEngineState, m8::engine::ParamID::WAV_SCAN, std::clamp<int>(inst.wav.scan + step, 0, 255), currentInstIndex);

        // Sampler-specific
        else if (!isMac && !isFm && !isHyp && cursor_id == C::SLICE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_SLICE, std::clamp<int>(inst.sampler.slice + step, 0, 255), currentInstIndex);
        else if (!isMac && !isFm && !isHyp && cursor_id == C::PLAY) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_PLAY, std::clamp<int>(inst.sampler.play + step, 0, 14), currentInstIndex);
        else if (!isMac && !isFm && !isHyp && cursor_id == C::START) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_START, std::clamp<int>(inst.sampler.start + step, 0, 255), currentInstIndex);
        else if (!isMac && !isFm && !isHyp && cursor_id == C::LOOP_ST) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_LOOP_ST, std::clamp<int>(inst.sampler.loop_st + step, 0, 255), currentInstIndex);
        else if (!isMac && !isFm && !isHyp && cursor_id == C::LENGTH) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_LENGTH, std::clamp<int>(inst.sampler.length + step, 0, 255), currentInstIndex);
        else if (!isMac && !isFm && !isHyp && cursor_id == C::DETUNE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_DETUNE, std::clamp<int>(inst.sampler.detune + step, 0, 255), currentInstIndex);

        // Macrosyn-specific
        else if (isMac && cursor_id == C::SHAPE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_SHAPE, std::clamp<int>(inst.macrosyn.shape + step, 0, 43), currentInstIndex);
        else if (isMac && cursor_id == C::TIMBRE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_TIMBRE, std::clamp<int>(inst.macrosyn.timbre + step, 0, 255), currentInstIndex);
        else if (isMac && cursor_id == C::COLOR) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_COLOR, std::clamp<int>(inst.macrosyn.color + step, 0, 255), currentInstIndex);
        else if (isMac && cursor_id == C::REDUX) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_REDUX, std::clamp<int>(inst.macrosyn.redux + step, 0, 255), currentInstIndex);

        // HyperSynth-specific
        else if (isHyp && cursor_id == C::HYP_SCALE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_SCALE, std::clamp<int>(inst.hyper.scale + step, 0, 16), currentInstIndex);
        else if (isHyp && cursor_id == C::HYP_CHORD_BANK) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_CHORD_BANK, std::clamp<int>(inst.hyper.chord_bank + step, 0, 15), currentInstIndex);
        else if (isHyp && cursor_id >= C::HYP_CHORD_N1 && cursor_id <= C::HYP_CHORD_N6) {
            int noteIdx = static_cast<int>(cursor_id) - static_cast<int>(C::HYP_CHORD_N1);
            int b = std::clamp(inst.hyper.chord_bank, 0, 15);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_CHORD_NOTE, std::clamp<int>(inst.hyper.chords[b][noteIdx] + step, 0, 48), currentInstIndex, b, noteIdx);
        }
        else if (isHyp && cursor_id == C::HYP_SHIFT) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_SHIFT, std::clamp<int>(inst.hyper.shift + step, 0, 255), currentInstIndex);
        else if (isHyp && cursor_id == C::HYP_SWARM) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_SWARM, std::clamp<int>(inst.hyper.swarm + step, 0, 255), currentInstIndex);
        else if (isHyp && cursor_id == C::HYP_WIDTH) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_WIDTH, std::clamp<int>(inst.hyper.width + step, 0, 255), currentInstIndex);
        else if (isHyp && cursor_id == C::HYP_SUBOSC) PushParam(commandSink, uiEngineState, m8::engine::ParamID::HYP_SUBOSC, std::clamp<int>(inst.hyper.subosc + step, 0, 255), currentInstIndex);

        // FMSynth-specific
        else if (isFm && cursor_id == C::FM_ALGO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_ALGO, std::clamp<int>(inst.fm.algo + step, 0, 11), currentInstIndex);
        else if (isFm && opIdx >= 0) {
            int o = opIdx;
            if (cursor_id == C::FM_OP_A_SHAPE || cursor_id == C::FM_OP_B_SHAPE || cursor_id == C::FM_OP_C_SHAPE || cursor_id == C::FM_OP_D_SHAPE) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_SHAPE, std::clamp<int>(inst.fm.ops[o].shape + step, 0, 11), currentInstIndex, o);
            } else if (cursor_id == C::FM_OP_A_RATIO || cursor_id == C::FM_OP_B_RATIO || cursor_id == C::FM_OP_C_RATIO || cursor_id == C::FM_OP_D_RATIO) {
                if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
                    PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_RATIO, std::clamp<int>(inst.fm.ops[o].ratio + step, 0, 24), currentInstIndex, o);
                } else {
                    PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_RATIO_FINE, std::clamp<int>(inst.fm.ops[o].ratio_fine + step, 0, 255), currentInstIndex, o);
                }
            } else if (cursor_id == C::FM_OP_A_LEV || cursor_id == C::FM_OP_B_LEV || cursor_id == C::FM_OP_C_LEV || cursor_id == C::FM_OP_D_LEV) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_LEVEL, std::clamp<int>(inst.fm.ops[o].level + step, 0, 255), currentInstIndex, o);
            } else if (cursor_id == C::FM_OP_A_FB || cursor_id == C::FM_OP_B_FB || cursor_id == C::FM_OP_C_FB || cursor_id == C::FM_OP_D_FB) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_FB, std::clamp<int>(inst.fm.ops[o].feedback + step, 0, 255), currentInstIndex, o);
            } else if (cursor_id == C::FM_OP_A_MOD1 || cursor_id == C::FM_OP_B_MOD1 || cursor_id == C::FM_OP_C_MOD1 || cursor_id == C::FM_OP_D_MOD1) {
                int curIdx = ByteToModIndex(inst.fm.ops[o].mod_a);
                int newIdx = std::clamp<int>(curIdx + step, 0, 16);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_MOD_A, ModIndexToByte(newIdx), currentInstIndex, o);
            } else if (cursor_id == C::FM_OP_A_MOD2 || cursor_id == C::FM_OP_B_MOD2 || cursor_id == C::FM_OP_C_MOD2 || cursor_id == C::FM_OP_D_MOD2) {
                int curIdx = ByteToModIndex(inst.fm.ops[o].mod_b);
                int newIdx = std::clamp<int>(curIdx + step, 0, 16);
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_OP_MOD_B, ModIndexToByte(newIdx), currentInstIndex, o);
            }
        }
        else if (isFm && cursor_id == C::FM_MOD1) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_MOD1, std::clamp<int>(inst.fm.mod1 + step, 0, 255), currentInstIndex);
        else if (isFm && cursor_id == C::FM_MOD2) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_MOD2, std::clamp<int>(inst.fm.mod2 + step, 0, 255), currentInstIndex);
        else if (isFm && cursor_id == C::FM_MOD3) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_MOD3, std::clamp<int>(inst.fm.mod3 + step, 0, 255), currentInstIndex);
        else if (isFm && cursor_id == C::FM_MOD4) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FM_MOD4, std::clamp<int>(inst.fm.mod4 + step, 0, 255), currentInstIndex);
    } else {
        if (event.key.key == SDLK_DOWN) {
            if (navMap.count(cursor_id) && navMap[cursor_id].down != C::NONE) {
                cursor_id = navMap[cursor_id].down;
            }
        } else if (event.key.key == SDLK_UP) {
            if (navMap.count(cursor_id) && navMap[cursor_id].up != C::NONE) {
                cursor_id = navMap[cursor_id].up;
            }
        } else if (event.key.key == SDLK_RIGHT) {
            if (cursor_id == C::NAME) {
                nameCharIndex = (nameCharIndex + 1) % 12;
            } else if (navMap.count(cursor_id) && navMap[cursor_id].right != C::NONE) {
                cursor_id = navMap[cursor_id].right;
            }
        } else if (event.key.key == SDLK_LEFT) {
            if (cursor_id == C::NAME) {
                nameCharIndex = (nameCharIndex + 11) % 12;
            } else if (navMap.count(cursor_id) && navMap[cursor_id].left != C::NONE) {
                cursor_id = navMap[cursor_id].left;
            }
        } else if (event.key.key == SDLK_RETURN) {
            if (cursor_id == C::CMD_LOAD) {
                browserForSongLoad = false;
                fileBrowser.init("Instruments", ".m8i");
                fileBrowser.setTitle("LOAD INSTRUMENT");
                instrumentBrowserMode = InstrumentBrowserMode::LOAD;
                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
            } else if (cursor_id == C::CMD_SAVE) {
                browserForSongLoad = false;
                fileBrowser.init("Instruments", "");
                fileBrowser.setTitle("SAVE INSTRUMENT TO DIR");
                instrumentBrowserMode = InstrumentBrowserMode::SAVE_DIR;
                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
            } else if (cursor_id == C::SAMPLE_LOAD) {
                browserForSongLoad = false;
                fileBrowser.init("Samples", ".wav");
                fileBrowser.setTitle("LOAD SAMPLE");
                instrumentBrowserMode = InstrumentBrowserMode::NONE;
                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
            } else if (cursor_id == C::SAMPLE_REC) {
                viewManager.pushModal(m8::ui::ViewType::SAMPLE_EDITOR);
            }
        }
    }
}

void HandleInstrumentEditRelease(CursorId cursor_id, bool& browserForSongLoad,
                                  ::FileBrowser& fileBrowser, ViewManager& viewManager,
                                  InstrumentBrowserMode& instrumentBrowserMode) {
    if (cursor_id == CursorId::CMD_LOAD) {
        browserForSongLoad = false;
        fileBrowser.init("Instruments", ".m8i");
        fileBrowser.setTitle("LOAD INSTRUMENT");
        instrumentBrowserMode = InstrumentBrowserMode::LOAD;
        viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
    } else if (cursor_id == CursorId::CMD_SAVE) {
        browserForSongLoad = false;
        fileBrowser.init("Instruments", "");
        fileBrowser.setTitle("SAVE INSTRUMENT TO DIR");
        instrumentBrowserMode = InstrumentBrowserMode::SAVE_DIR;
        viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
    } else if (cursor_id == CursorId::SAMPLE_LOAD) {
        browserForSongLoad = false;
        fileBrowser.init("Samples", ".wav");
        fileBrowser.setTitle("LOAD SAMPLE");
        instrumentBrowserMode = InstrumentBrowserMode::NONE;
        viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
    } else if (cursor_id == CursorId::SAMPLE_REC) {
        viewManager.pushModal(m8::ui::ViewType::SAMPLE_EDITOR);
    } else if (cursor_id == CursorId::EQ) {
        viewManager.pushModal(m8::ui::ViewType::EQ);
    }
}

} // namespace instrument
} // namespace ui
} // namespace m8
