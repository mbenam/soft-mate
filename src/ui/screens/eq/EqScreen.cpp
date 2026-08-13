#include "EqScreen.h"
#include "../../../engine/EqFilter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace m8 {
namespace ui {
namespace eq {

static SDL_Color GetColorFromString(const std::string& name) {
    if (name == "TITLE") return {255, 60, 60, 255};
    if (name == "LABEL_DIM") return {100, 100, 100, 255};
    if (name == "LABEL_LITE") return {0, 255, 255, 255};
    return {255, 255, 255, 255};
}

static const SDL_Color kCurveColor  = {0, 255, 255, 255};
static const SDL_Color kZeroLine    = {45, 60, 60, 255};
static const SDL_Color kValueColor  = {255, 255, 255, 255};
static const SDL_Color kSelColor    = {0, 255, 255, 255};

// Curve glyphs are a dash at one of seven pixel rows within a cell
// (font.h 0x08..0x0E), so a 12-cell graph has 84 vertical positions.
static constexpr int kSubRows = 7;
static constexpr char kDashBase = 0x08;
static constexpr int kTotalSteps = kCurveRows * kSubRows;

static const engine::EqBand& BandOf(const engine::EqBank& bank, int i) {
    return (i == 0) ? bank.low : (i == 1) ? bank.mid : bank.high;
}
static engine::EqBand& BandOf(engine::EqBank& bank, int i) {
    return (i == 0) ? bank.low : (i == 1) ? bank.mid : bank.high;
}

// Value text for one cell of the parameter table.
static std::string ValueText(const engine::EqBand& b, int param) {
    char buf[16];
    switch (param) {
    case P_GAIN: {
        // Two decimals with an explicit sign, as the device shows it.
        const float db = b.gain / 100.0f;
        std::snprintf(buf, sizeof(buf), "%s%05.2f", (b.gain < 0 ? "-" : " "), std::fabs(db));
        return buf;
    }
    case P_FREQ:
        std::snprintf(buf, sizeof(buf), "%d", b.freq);
        return buf;
    case P_Q:
        std::snprintf(buf, sizeof(buf), "%02d", b.q);
        return buf;
    case P_TYPE: return TypeName(b.type);
    case P_MODE: return ModeName(b.mode);
    default: return "";
    }
}

void RenderEqScreen(Renderer& renderer,
                    const engine::EngineState& engState,
                    const EqScreenState& st) {
    const int bankIdx = std::clamp(st.bank, 0, engine::kMaxEqBanks - 1);
    const engine::EqBank& bank = engState.eqs[bankIdx];

    char title[24];
    std::snprintf(title, sizeof(title), "EQ BANK %02X", bankIdx);
    renderer.drawString(title, 0, 0, GetColorFromString("TITLE"));

    for (const auto& cell : GetEqStaticText())
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));

    // ---- Curve ------------------------------------------------------------
    // Evaluated analytically from the bank rather than by running audio, so it
    // costs one magnitude calculation per column per frame.
    engine::EqProcessor probe;
    probe.configure(bank, engine::kSampleRate);

    const float stepDb = (2.0f * kDbRange) / float(kTotalSteps);
    const int centreStep = kTotalSteps / 2;

    for (int col = 0; col < kGridCols; ++col) {
        const float f = ColumnToFreq(col);
        const float db = probe.isBypass() ? 0.0f
                                          : probe.responseDbAt(f, engine::kSampleRate);
        // Step 0 is the top of the graph (+kDbRange).
        int step = centreStep - static_cast<int>(std::lround(db / stepDb));
        step = std::clamp(step, 0, kTotalSteps - 1);

        const int row = kCurveTop + step / kSubRows;
        const char glyph = static_cast<char>(kDashBase + (step % kSubRows));
        renderer.drawChar(glyph, col, row, kCurveColor);

        // 0 dB reference, drawn only where the curve isn't -- one character per
        // cell, so the curve always wins (EQ_SPEC.md §6).
        const int zeroRow = kCurveTop + centreStep / kSubRows;
        if (row != zeroRow) {
            const char zeroGlyph = static_cast<char>(kDashBase + (centreStep % kSubRows));
            renderer.drawChar(zeroGlyph, col, zeroRow, kZeroLine);
        }
    }

    // ---- Parameter table ---------------------------------------------------
    for (int b = 0; b < 3; ++b) {
        const engine::EqBand& band = BandOf(bank, b);
        for (int p = 0; p < P_COUNT; ++p) {
            const bool active = (st.band == b && st.param == p);
            const std::string text = ValueText(band, p);
            renderer.drawString(text, kBandCol[b], kTableTop + 1 + p,
                                active ? kSelColor : kValueColor);
            if (active)
                renderer.drawBracket(kBandCol[b], kTableTop + 1 + p,
                                     static_cast<int>(text.length()), kSelColor);
        }
    }
}

bool HandleEqInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                   engine::EngineState& uiEngineState, EqScreenState& st,
                   CommandSink& commandSink) {
    // OPTION leaves the editor, as on the device.
    if (event.key.key == SDLK_Z) return true;

    if (!editHeld) {
        if (event.key.key == SDLK_DOWN)       st.param = std::min(st.param + 1, int(P_COUNT) - 1);
        else if (event.key.key == SDLK_UP)    st.param = std::max(st.param - 1, 0);
        else if (event.key.key == SDLK_RIGHT) st.band  = std::min(st.band + 1, 2);
        else if (event.key.key == SDLK_LEFT)  st.band  = std::max(st.band - 1, 0);
        return false;
    }

    int step = 0;
    bool large = false;
    if (event.key.key == SDLK_RIGHT)     { step = 1;  }
    else if (event.key.key == SDLK_LEFT) { step = -1; }
    else if (event.key.key == SDLK_UP)   { step = 1;  large = true; }
    else if (event.key.key == SDLK_DOWN) { step = -1; large = true; }
    else return false;

    arrowPressedDuringEdit = true;

    const int bankIdx = std::clamp(st.bank, 0, engine::kMaxEqBanks - 1);
    const engine::EqBand& band = BandOf(uiEngineState.eqs[bankIdx], st.band);

    // Every edit goes through PushParam, which applies it to the UI mirror AND
    // sends the same mutation to the engine. Writing the mirror directly here
    // would desync the two copies (ARCHITECTURE.md invariant 4).
    auto push = [&](m8::engine::ParamID id, int value) {
        PushParam(commandSink, uiEngineState, id, value, bankIdx, st.band);
    };

    switch (st.param) {
    case P_GAIN:
        // Small steps are a quarter dB, large ones a whole dB. Range is +/-24,
        // which covers everything seen on the device; the real limit is unknown.
        push(m8::engine::ParamID::EQ_GAIN,
             std::clamp(band.gain + step * (large ? 100 : 25), -2400, 2400));
        break;
    case P_FREQ:
        // Linear steps. The device's own stepping is coarser at high
        // frequencies, but its law is not known, so this does not pretend to
        // match it (EQ_SPEC.md §8).
        push(m8::engine::ParamID::EQ_FREQ,
             std::clamp(band.freq + step * (large ? 100 : 1), 20, 20000));
        break;
    case P_Q:
        push(m8::engine::ParamID::EQ_Q, std::clamp(band.q + step * (large ? 10 : 1), 0, 99));
        break;
    case P_TYPE:
        push(m8::engine::ParamID::EQ_TYPE, (band.type + step + 7) % 7);
        break;
    case P_MODE:
        push(m8::engine::ParamID::EQ_MODE, (band.mode + step + 5) % 5);
        break;
    default: break;
    }
    return false;
}

} // namespace eq
} // namespace ui
} // namespace m8
