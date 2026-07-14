#pragma once

#include "Engine.h"
#include <algorithm>

namespace m8 {
namespace engine {

struct EngineStateUpdater {
    static void applyParameterUpdate(EngineState& state, const EngineCommand& cmd) {
        auto& inst = state.instruments[cmd.targetId];
        auto& mod = inst.mods[cmd.row];
        auto& scale = state.scales[cmd.targetId];
        auto& mx = state.mixer;
        auto& fx = state.effects;

        auto setName = [](char* dest, const char* src) {
            for (int i = 0; i < 12; ++i) {
                dest[i] = src[i];
                if (src[i] == '\0') break;
            }
        };

        switch (cmd.paramId) {
            case ParamID::BPM_INT: state.bpm = std::clamp((int)cmd.value, 20, 400); break;
            case ParamID::BPM_FRAC: state.bpm_frac = std::clamp((int)cmd.value, 0, 99); break;
            
            // Mixer
            case ParamID::MIX_OUT_VOL: mx.out_vol = cmd.value; break;
            case ParamID::MIX_TRK_VOL: mx.track_vol[cmd.row] = cmd.value; break;
            case ParamID::MIX_CHO_VOL: mx.cho_vol = cmd.value; break;
            case ParamID::MIX_DEL_VOL: mx.del_vol = cmd.value; break;
            case ParamID::MIX_REV_VOL: mx.rev_vol = cmd.value; break;
            case ParamID::MIX_IN_VOL: mx.in_vol = cmd.value; break;
            case ParamID::MIX_IN_CHO: mx.in_cho = cmd.value; break;
            case ParamID::MIX_IN_DEL: mx.in_del = cmd.value; break;
            case ParamID::MIX_IN_REV: mx.in_rev = cmd.value; break;
            case ParamID::MIX_USB_VOL: mx.usb_vol = cmd.value; break;
            case ParamID::MIX_USB_CHO: mx.usb_cho = cmd.value; break;
            case ParamID::MIX_USB_DEL: mx.usb_del = cmd.value; break;
            case ParamID::MIX_USB_REV: mx.usb_rev = cmd.value; break;
            case ParamID::MIX_MIX_VOL: mx.mix_vol = cmd.value; break;
            case ParamID::MIX_LIM_VAL: mx.lim_val = cmd.value; break;
            case ParamID::MIX_DJF_FREQ: mx.djf_freq = cmd.value; break;
            case ParamID::MIX_DJF_RES: mx.djf_res = cmd.value; break;
            case ParamID::MIX_DJF_TYP: mx.djf_typ = cmd.value; break;

            // Effects
            case ParamID::FX_CHO_MOD_DEPTH: fx.cho_mod_depth = cmd.value; break;
            case ParamID::FX_CHO_MOD_FREQ: fx.cho_mod_freq = cmd.value; break;
            case ParamID::FX_CHO_WIDTH: fx.cho_width = cmd.value; break;
            case ParamID::FX_CHO_REVERB: fx.cho_reverb = cmd.value; break;
            case ParamID::FX_DEL_TIME_L: fx.del_time_l = cmd.value; break;
            case ParamID::FX_DEL_TIME_R: fx.del_time_r = cmd.value; break;
            case ParamID::FX_DEL_FEEDBACK: fx.del_feedback = cmd.value; break;
            case ParamID::FX_DEL_WIDTH: fx.del_width = cmd.value; break;
            case ParamID::FX_DEL_REVERB: fx.del_reverb = cmd.value; break;
            case ParamID::FX_REV_SIZE: fx.rev_size = cmd.value; break;
            case ParamID::FX_REV_DECAY: fx.rev_decay = cmd.value; break;
            case ParamID::FX_REV_MOD_DEPTH: fx.rev_mod_depth = cmd.value; break;
            case ParamID::FX_REV_MOD_FREQ: fx.rev_mod_freq = cmd.value; break;
            case ParamID::FX_REV_WIDTH: fx.rev_width = cmd.value; break;

            // Instrument Global
            case ParamID::INST_TYPE: 
                inst.type = static_cast<InstType>(cmd.value);
                if (inst.type == InstType::INST_SAMPLER) setName(inst.name, "SAMPLER     ");
                else if (inst.type == InstType::INST_MACROSYN) setName(inst.name, "MACROSYN    ");
                else setName(inst.name, "------------");
                break;
            case ParamID::INST_TRANSP: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.transp = cmd.value; else inst.sampler.transp = cmd.value; break;
            case ParamID::INST_TBL_TIC: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.tbl_tic = cmd.value; else inst.sampler.tbl_tic = cmd.value; break;
            case ParamID::INST_EQ: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.eq = cmd.value; else inst.sampler.eq = cmd.value; break;
            case ParamID::INST_AMP: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.amp = cmd.value; else inst.sampler.amp = cmd.value; break;
            case ParamID::INST_LIM: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.lim = cmd.value; else inst.sampler.lim = cmd.value; break;
            case ParamID::INST_PAN: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.pan = cmd.value; else inst.sampler.pan = cmd.value; break;
            case ParamID::INST_DRY: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.dry = cmd.value; else inst.sampler.dry = cmd.value; break;
            case ParamID::INST_CHO: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.cho = cmd.value; else inst.sampler.cho = cmd.value; break;
            case ParamID::INST_DEL: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.del = cmd.value; else inst.sampler.del = cmd.value; break;
            case ParamID::INST_REV: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.rev = cmd.value; else inst.sampler.rev = cmd.value; break;
            case ParamID::INST_DEGRADE: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.degrade = cmd.value; else inst.sampler.degrade = cmd.value; break;
            case ParamID::INST_FILTER: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.filter_type = cmd.value; else inst.sampler.filter_type = cmd.value; break;
            case ParamID::INST_CUTOFF: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.cutoff = cmd.value; else inst.sampler.cutoff = cmd.value; break;
            case ParamID::INST_RES: if (inst.type == InstType::INST_MACROSYN) inst.macrosyn.res = cmd.value; else inst.sampler.res = cmd.value; break;

            // Sampler
            case ParamID::SAMP_PLAY: inst.sampler.play = cmd.value; break;
            case ParamID::SAMP_START: inst.sampler.start = cmd.value; break;
            case ParamID::SAMP_LOOP_ST: inst.sampler.loop_st = cmd.value; break;
            case ParamID::SAMP_LENGTH: inst.sampler.length = cmd.value; break;
            case ParamID::SAMP_DETUNE: inst.sampler.detune = cmd.value; break;
            case ParamID::SAMP_SLICE: inst.sampler.slice = cmd.value; break;

            // Macrosyn
            case ParamID::MAC_SHAPE: inst.macrosyn.shape = cmd.value; break;
            case ParamID::MAC_TIMBRE: inst.macrosyn.timbre = cmd.value; break;
            case ParamID::MAC_COLOR: inst.macrosyn.color = cmd.value; break;
            case ParamID::MAC_REDUX: inst.macrosyn.redux = cmd.value; break;

            // Modulators
            case ParamID::MOD_TYPE: mod.type = cmd.value; break;
            case ParamID::MOD_DEST: mod.dest = cmd.value; break;
            case ParamID::MOD_AMT: mod.amt = cmd.value; break;
            case ParamID::MOD_P1: mod.p1 = cmd.value; break;
            case ParamID::MOD_P2: mod.p2 = cmd.value; break;
            case ParamID::MOD_P3: mod.p3 = cmd.value; break;
            case ParamID::MOD_P4: mod.p4 = cmd.value; break;

            // Scales
            case ParamID::SCALE_KEY: scale.key = cmd.value; break;
            case ParamID::SCALE_TUNE: scale.tune = cmd.fValue; break;
            case ParamID::SCALE_NOTE_EN: scale.notes[cmd.row].enable = (cmd.value != 0); break;
            case ParamID::SCALE_NOTE_OFFSET: scale.notes[cmd.row].offset = cmd.fValue; break;
            default: break;
        }
    }
};

} // engine
} // m8
