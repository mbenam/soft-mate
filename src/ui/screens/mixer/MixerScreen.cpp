#include "MixerScreen.h"
#include "MixerScreenLayout.h"
#include "ui/Theme.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace mixer {

static SDL_Color GetColorFromString(const std::string& colorName) {
    return GetThemeColor(colorName);
}

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
       << (static_cast<unsigned int>(value) & 0xFF);
    return ss.str();
}

static int ResolveMixerValue(CursorId fieldId, const engine::MixerState& mx) {
    using C = CursorId;
    if (fieldId == C::SPEAKER_VOL) return mx.out_vol;
    if (IsTrackVolCursor(fieldId)) return mx.track_vol[TrackIndexOf(fieldId)];
    if (fieldId == C::MST_CHO) return mx.cho_vol;
    if (fieldId == C::MST_DEL) return mx.del_vol;
    if (fieldId == C::MST_REV) return mx.rev_vol;
    if (fieldId == C::MIX_VOL) return mx.mix_vol;
    if (fieldId == C::LIM_VAL) return mx.lim_val;
    if (fieldId == C::DJF)     return mx.djf_freq;
    if (fieldId == C::OTT)     return mx.ott;
    return 0;
}

// ---------------------------------------------------------------------------
// Bars, drawn as font glyphs (MIXER_SPEC.md §5.1).
//
// Each cell holds one of the fill glyphs 0x01..0x07, so a column of cells is a
// bar with 8 levels per cell. One drawChar per cell means one write per cell --
// unlike the rectangle drawing this replaces, which tripped the overlap checker
// whenever a fill boundary landed mid-cell and forced MIXER to be exempted from
// assert_no_overlap.
//
// A cell is coloured by what reaches it: bright if the live level does, dim if
// only the volume setting does. That is a single pass with one colour decision,
// and it means the volume is still visible as a dim bar when nothing is
// playing -- otherwise a stopped mixer would show eight empty columns.
// ---------------------------------------------------------------------------

static constexpr char kFillFull = 0x07;

// Colour ramp bottom-to-top using theme meter colors
static SDL_Color LevelColor(float fractionOfHeight, bool clipped) {
    if (clipped || fractionOfHeight > 0.85f) return GetThemeColor("METER_PEAK");
    if (fractionOfHeight > 0.50f) return GetThemeColor("METER_MID");
    return GetThemeColor("METER_LOW");
}

static SDL_Color SettingColor() {
    return GetThemeColor("LABEL_DIM");
}

// Draw one bar. `level` and `setting` are 0..255; `level` is live audio and
// `setting` is the parameter behind it (pass 0 for a pure meter).
static void DrawGlyphBar(Renderer& renderer, int col, int rowTop, int rowBottom,
                         int level, int setting, bool clipped) {
    const int cells = rowBottom - rowTop + 1;
    if (cells <= 0) return;

    const int totalSteps = cells * 8;
    const int levelSteps   = (std::clamp(level, 0, 255) * totalSteps) / 255;
    const int settingSteps = (std::clamp(setting, 0, 255) * totalSteps) / 255;

    for (int c = 0; c < cells; ++c) {
        // Cell 0 is the bottom of the bar.
        const int cellBase = c * 8;
        const int row = rowBottom - c;

        const int liveInCell    = std::clamp(levelSteps   - cellBase, 0, 8);
        const int settingInCell = std::clamp(settingSteps - cellBase, 0, 8);

        const int fill = std::max(liveInCell, settingInCell);
        if (fill <= 0) continue;

        // 8 steps map onto 7 glyphs; a full cell is 0x07 (kFillFull).
        const char glyph = static_cast<char>(std::min(fill, static_cast<int>(kFillFull)));

        // Bright where live audio reaches, dim where only the setting does.
        const bool live = (liveInCell > 0) && (liveInCell >= settingInCell);
        const float frac = static_cast<float>(cellBase + fill) / static_cast<float>(totalSteps);
        renderer.drawChar(glyph, col, row, live ? LevelColor(frac, clipped) : SettingColor());
    }
}

void RenderMixerScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       CursorId active_cursor_id,
                       const MixerLevels& levels) {
    const engine::MixerState& mx = engState.mixer;

    static std::vector<UI_GridCell> staticText = GetMixerStaticText();
    static auto interactiveFields = GetMixerInteractiveFields();

    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Per-track stereo meters. Two adjacent columns, left and right. The voice
    // path is mono, so L and R differ only by the instrument's pan -- which is
    // exactly what you want to see on a mixer.
    for (int i = 0; i < 8; ++i) {
        const auto& lv = levels.track[i];
        DrawGlyphBar(renderer, kTrackCol(i),     kMeterTop, kMeterBottom,
                     lv.peakL, mx.track_vol[i], lv.clipped);
        DrawGlyphBar(renderer, kTrackCol(i) + 1, kMeterTop, kMeterBottom,
                     lv.peakR, mx.track_vol[i], lv.clipped);
    }

    // Send returns. No per-send metering yet, so these show their setting only.
    const int sendVals[3] = { mx.cho_vol, mx.del_vol, mx.rev_vol };
    for (int i = 0; i < 3; ++i) {
        DrawGlyphBar(renderer, kSendCol(i),     kSendMeterTop, kSendMeterBottom, 0, sendVals[i], false);
        DrawGlyphBar(renderer, kSendCol(i) + 1, kSendMeterTop, kSendMeterBottom, 0, sendVals[i], false);
    }

    // Master meter: the actual bus output, after every stage.
    DrawGlyphBar(renderer, kMasterMeterCol,     kMeterTop, kMeterBottom,
                 levels.master.peakL, mx.mix_vol, levels.master.clipped);
    DrawGlyphBar(renderer, kMasterMeterCol + 1, kMeterTop, kMeterBottom,
                 levels.master.peakR, mx.mix_vol, levels.master.clipped);

    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        int val = ResolveMixerValue(fieldId, mx);
        std::string liveText = ToHex(val);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);
            std::string drawText = (comp.role == "value") ? liveText : comp.text;

            renderer.drawString(drawText, comp.col, comp.row, color);

            if (isActive && comp.has_cursor_box && comp.role == "value") {
                renderer.drawBracket(comp.col, comp.row, drawText.length(), GetThemeColor("CURSOR"));
            }
        }
    }
}

bool HandleMixerEditRelease(CursorId cursor_id) {
    return cursor_id == CursorId::EQ;
}

void HandleMixerInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, CursorId& cursor_id,
                       CommandSink& commandSink) {
    using C = CursorId;
    auto navMap = GetMixerNavMap();
    const engine::MixerState& mx = uiEngineState.mixer;

    if (!editHeld) {
        if (event.key.key == SDLK_DOWN) {
            if (navMap.count(cursor_id) && navMap[cursor_id].down != C::NONE)
                cursor_id = navMap[cursor_id].down;
        } else if (event.key.key == SDLK_UP) {
            if (navMap.count(cursor_id) && navMap[cursor_id].up != C::NONE)
                cursor_id = navMap[cursor_id].up;
        } else if (event.key.key == SDLK_RIGHT) {
            if (navMap.count(cursor_id) && navMap[cursor_id].right != C::NONE)
                cursor_id = navMap[cursor_id].right;
        } else if (event.key.key == SDLK_LEFT) {
            if (navMap.count(cursor_id) && navMap[cursor_id].left != C::NONE)
                cursor_id = navMap[cursor_id].left;
        }
        return;
    }

    // Edit: LEFT/RIGHT nudge by 1, UP/DOWN by 16 -- the same feel as the other
    // hex screens.
    int step = 0;
    if (event.key.key == SDLK_RIGHT)     step = 1;
    else if (event.key.key == SDLK_LEFT) step = -1;
    else if (event.key.key == SDLK_UP)   step = 16;
    else if (event.key.key == SDLK_DOWN) step = -16;
    else return;

    arrowPressedDuringEdit = true;
    auto bump = [&](m8::engine::ParamID id, int current, int target = 0, int row = 0) {
        PushParam(commandSink, uiEngineState, id, std::clamp(current + step, 0, 255), target, row);
    };

    // EQ has no value of its own -- it is a doorway, handled on release.
    if (cursor_id == C::EQ) return;

    if (cursor_id == C::SPEAKER_VOL) {
        bump(m8::engine::ParamID::MIX_OUT_VOL, mx.out_vol);
    } else if (IsTrackVolCursor(cursor_id)) {
        const int t = TrackIndexOf(cursor_id);
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_TRK_VOL,
                  std::clamp(mx.track_vol[t] + step, 0, 255), 0, t);
    } else if (cursor_id == C::MST_CHO) {
        bump(m8::engine::ParamID::MIX_CHO_VOL, mx.cho_vol);
    } else if (cursor_id == C::MST_DEL) {
        bump(m8::engine::ParamID::MIX_DEL_VOL, mx.del_vol);
    } else if (cursor_id == C::MST_REV) {
        bump(m8::engine::ParamID::MIX_REV_VOL, mx.rev_vol);
    } else if (cursor_id == C::MIX_VOL) {
        bump(m8::engine::ParamID::MIX_MIX_VOL, mx.mix_vol);
    } else if (cursor_id == C::LIM_VAL) {
        bump(m8::engine::ParamID::MIX_LIM_VAL, mx.lim_val);
    } else if (cursor_id == C::DJF) {
        bump(m8::engine::ParamID::MIX_DJF_FREQ, mx.djf_freq);
    } else if (cursor_id == C::OTT) {
        bump(m8::engine::ParamID::MIX_OTT, mx.ott);
    }
}

} // namespace mixer
} // namespace ui
} // namespace m8
