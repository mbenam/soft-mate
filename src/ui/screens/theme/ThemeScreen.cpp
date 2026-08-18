#include "ThemeScreen.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace m8 {
namespace ui {
namespace theme {

static std::string ToHex(uint8_t value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(value);
    return ss.str();
}

static void RgbToHsv(uint8_t r, uint8_t g, uint8_t b, uint8_t& h, uint8_t& s, uint8_t& v) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;
    float maxv = std::max({rf, gf, bf});
    float minv = std::min({rf, gf, bf});
    float delta = maxv - minv;

    v = static_cast<uint8_t>(std::clamp(std::round(maxv * 255.0f), 0.0f, 255.0f));

    if (maxv < 1e-5f || delta < 1e-5f) {
        s = 0;
        h = 0;
        return;
    }

    s = static_cast<uint8_t>(std::clamp(std::round((delta / maxv) * 255.0f), 0.0f, 255.0f));

    float hf = 0.0f;
    if (rf >= maxv) {
        hf = (gf - bf) / delta;
    } else if (gf >= maxv) {
        hf = 2.0f + (bf - rf) / delta;
    } else {
        hf = 4.0f + (rf - gf) / delta;
    }
    hf *= 60.0f;
    if (hf < 0.0f) hf += 360.0f;
    h = static_cast<uint8_t>(std::clamp(std::round((hf / 360.0f) * 255.0f), 0.0f, 255.0f));
}

static void HsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float hf = (h / 255.0f) * 360.0f;
    float sf = s / 255.0f;
    float vf = v / 255.0f;

    if (sf <= 0.0f) {
        r = g = b = v;
        return;
    }

    float c = vf * sf;
    float x = c * (1.0f - std::fabs(std::fmod(hf / 60.0f, 2.0f) - 1.0f));
    float m = vf - c;

    float rf = 0.0f, gf = 0.0f, bf = 0.0f;
    if (hf < 60.0f) { rf = c; gf = x; bf = 0.0f; }
    else if (hf < 120.0f) { rf = x; gf = c; bf = 0.0f; }
    else if (hf < 180.0f) { rf = 0.0f; gf = 0.0f; bf = x; }
    else if (hf < 240.0f) { rf = 0.0f; gf = x; bf = c; }
    else if (hf < 300.0f) { rf = x; gf = 0.0f; bf = c; }
    else { rf = c; gf = 0.0f; bf = x; }

    r = static_cast<uint8_t>(std::clamp(std::round((rf + m) * 255.0f), 0.0f, 255.0f));
    g = static_cast<uint8_t>(std::clamp(std::round((gf + m) * 255.0f), 0.0f, 255.0f));
    b = static_cast<uint8_t>(std::clamp(std::round((bf + m) * 255.0f), 0.0f, 255.0f));
}

static void NudgeThemeHue(int delta) {
    for (size_t i = 1; i < static_cast<size_t>(ThemeSlot::COUNT); ++i) {
        ThemeColor& col = g_currentTheme.colors[i];
        uint8_t h = 0, s = 0, v = 0;
        RgbToHsv(col.r, col.g, col.b, h, s, v);
        int newH = (static_cast<int>(h) + delta) & 0xFF;
        HsvToRgb(static_cast<uint8_t>(newH), s, v, col.r, col.g, col.b);
    }
}

void RenderThemeScreen(Renderer& renderer, const ThemeScreenState& state) {
    // Title
    renderer.drawString("THEME SETTINGS", 0, 0, GetThemeColor("TITLE"));

    // Header Mode row (row 1)
    bool isModeRow = (state.cursorRow == 0);
    renderer.drawString("MODE", 0, 1, isModeRow ? GetThemeColor("LABEL_LITE") : GetThemeColor("LABEL_DIM"));

    if (state.isHsv) {
        renderer.drawString("H", 14, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString("S", 17, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString("V", 20, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    } else {
        renderer.drawString("R", 14, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString("G", 17, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString("B", 20, 1, isModeRow && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    }

    if (isModeRow && state.cursorCol == 0) {
        renderer.drawBracket(14, 1, 8, GetThemeColor("LABEL_LITE"));
    }

    // Nudge control `<>` at col 24
    renderer.drawString("<>", 24, 1, isModeRow && state.cursorCol == 1 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    if (isModeRow && state.cursorCol == 1) {
        renderer.drawBracket(24, 1, 2, GetThemeColor("LABEL_LITE"));
    }

    // 13 Color Rows
    for (size_t i = 0; i < sizeof(kThemeSlotInfos) / sizeof(kThemeSlotInfos[0]); ++i) {
        const auto& info = kThemeSlotInfos[i];
        int row = info.row;
        bool isRowActive = (state.cursorRow == static_cast<int>(i + 1));

        SDL_Color labelColor = isRowActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("LABEL_DIM");
        renderer.drawString(info.label, 0, row, labelColor);

        const ThemeColor& col = g_currentTheme.colors[static_cast<size_t>(info.slot)];
        uint8_t c1 = 0, c2 = 0, c3 = 0;
        if (state.isHsv) {
            RgbToHsv(col.r, col.g, col.b, c1, c2, c3);
        } else {
            c1 = col.r;
            c2 = col.g;
            c3 = col.b;
        }

        std::string s1 = ToHex(c1);
        std::string s2 = ToHex(c2);
        std::string s3 = ToHex(c3);

        renderer.drawString(s1, 14, row, isRowActive && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString(s2, 17, row, isRowActive && state.cursorCol == 1 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString(s3, 20, row, isRowActive && state.cursorCol == 2 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));

        if (isRowActive) {
            int colX = (state.cursorCol == 0) ? 14 : (state.cursorCol == 1) ? 17 : 20;
            renderer.drawBracket(colX, row, 2, GetThemeColor("LABEL_LITE"));
        }

        // Filled color preview bar (for rows 1..12, i.e. slots > BACKGROUND)
        if (info.slot != ThemeSlot::BACKGROUND) {
            renderer.fillRectPixel(24 * 8, row * 8 + 1, 24, 7, col.toSDL());
        }
    }

    // Theme Name (row 16)
    bool isNameActive = (state.cursorRow == 14);
    renderer.drawString("THEME NAME", 0, 16, isNameActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("LABEL_DIM"));
    renderer.drawString(g_currentTheme.name, 14, 16, isNameActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    if (isNameActive) {
        renderer.drawBracket(14 + state.nameCharIndex, 16, 1, GetThemeColor("LABEL_LITE"));
    }

    // Action buttons (row 17)
    bool isActionsActive = (state.cursorRow == 15);
    renderer.drawString("LOAD", 14, 17, isActionsActive && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    renderer.drawString("SAVE", 19, 17, isActionsActive && state.cursorCol == 1 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    renderer.drawString("RESET", 24, 17, isActionsActive && state.cursorCol == 2 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));

    if (isActionsActive) {
        int x = (state.cursorCol == 0) ? 14 : (state.cursorCol == 1) ? 19 : 24;
        int len = (state.cursorCol == 2) ? 5 : 4;
        renderer.drawBracket(x, 17, len, GetThemeColor("LABEL_LITE"));
    }
}

bool HandleThemeInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      ThemeScreenState& state, ViewManager& viewManager) {
    (void)viewManager;
    if (event.type != SDL_EVENT_KEY_DOWN) return false;

    // Option key (Z) or Escape exits back to System Settings
    if (event.key.key == SDLK_Z || event.key.key == SDLK_ESCAPE) {
        return true;
    }

    // Action buttons trigger (row 15)
    if ((event.key.key == SDLK_X || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && state.cursorRow == 15) {
        if (state.cursorCol == 2) {
            g_currentTheme.resetDefault();
        }
        return false;
    }

    // Mode toggle trigger on row 0
    if ((event.key.key == SDLK_X || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && state.cursorRow == 0 && state.cursorCol == 0) {
        state.isHsv = !state.isHsv;
        return false;
    }

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_UP || event.key.key == SDLK_RIGHT) ? 1 : -1;
        bool largeStep = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_LEFT);
        int delta = largeStep ? step * 0x10 : step;

        if (state.cursorRow == 0) {
            if (state.cursorCol == 0) {
                state.isHsv = !state.isHsv;
            } else if (state.cursorCol == 1) {
                NudgeThemeHue(delta);
            }
        } else if (state.cursorRow >= 1 && state.cursorRow <= 13) {
            size_t slotIdx = static_cast<size_t>(state.cursorRow - 1);
            ThemeColor& col = g_currentTheme.colors[slotIdx];

            if (!state.isHsv) {
                if (state.cursorCol == 0) col.r = static_cast<uint8_t>(std::clamp(static_cast<int>(col.r) + delta, 0, 255));
                else if (state.cursorCol == 1) col.g = static_cast<uint8_t>(std::clamp(static_cast<int>(col.g) + delta, 0, 255));
                else if (state.cursorCol == 2) col.b = static_cast<uint8_t>(std::clamp(static_cast<int>(col.b) + delta, 0, 255));
            } else {
                uint8_t h = 0, s = 0, v = 0;
                RgbToHsv(col.r, col.g, col.b, h, s, v);
                if (state.cursorCol == 0) h = static_cast<uint8_t>(std::clamp(static_cast<int>(h) + delta, 0, 255));
                else if (state.cursorCol == 1) s = static_cast<uint8_t>(std::clamp(static_cast<int>(s) + delta, 0, 255));
                else if (state.cursorCol == 2) v = static_cast<uint8_t>(std::clamp(static_cast<int>(v) + delta, 0, 255));
                HsvToRgb(h, s, v, col.r, col.g, col.b);
            }
        }
        return false;
    }

    if (!editHeld) {
        if (event.key.key == SDLK_UP) {
            state.cursorRow = std::max(0, state.cursorRow - 1);
            if (state.cursorRow == 0 && state.cursorCol > 1) state.cursorCol = 1;
        } else if (event.key.key == SDLK_DOWN) {
            state.cursorRow = std::min(15, state.cursorRow + 1);
        } else if (event.key.key == SDLK_LEFT) {
            if (state.cursorRow == 14) {
                state.nameCharIndex = (state.nameCharIndex + 11) % 12;
            } else {
                state.cursorCol = std::max(0, state.cursorCol - 1);
            }
        } else if (event.key.key == SDLK_RIGHT) {
            if (state.cursorRow == 14) {
                state.nameCharIndex = (state.nameCharIndex + 1) % 12;
            } else if (state.cursorRow == 0) {
                state.cursorCol = std::min(1, state.cursorCol + 1);
            } else {
                state.cursorCol = std::min(2, state.cursorCol + 1);
            }
        }
    }

    return false;
}

} // namespace theme
} // namespace ui
} // namespace m8
