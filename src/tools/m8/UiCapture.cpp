// ===========================================================================
// UiCapture.cpp — normalized screen capture serialization and palette building.
// ===========================================================================

#include "UiCapture.h"
#include <algorithm>
#include <sstream>
#include <cstdio>

namespace m8 {
namespace dev {

void buildPalette(UiCapture& c) {
    // Collect distinct RGB colors in c.palette and sort canonically (r, g, b)
    std::vector<std::array<uint8_t, 3>> colors = c.palette;

    std::sort(colors.begin(), colors.end(), [](const std::array<uint8_t, 3>& a, const std::array<uint8_t, 3>& b) {
        if (a[0] != b[0]) return a[0] < b[0];
        if (a[1] != b[1]) return a[1] < b[1];
        return a[2] < b[2];
    });

    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
    c.palette = std::move(colors);
}

UiCapture captureFromGrid(const ScreenGrid& grid, bool settled,
                          const std::string& screenName,
                          const std::string& firmware, int fontMode,
                          const std::string& themeId) {
    UiCapture cap;
    cap.screen = screenName.empty() ? grid.topHeader() : screenName;
    cap.firmware = firmware;
    cap.fontMode = fontMode;
    cap.pitchX = 8;
    cap.pitchY = 10;
    cap.settled = settled;
    cap.themeId = themeId;

    auto addColor = [&](const std::array<uint8_t, 3>& col) -> int {
        for (size_t i = 0; i < cap.palette.size(); ++i) {
            if (cap.palette[i] == col) return static_cast<int>(i);
        }
        cap.palette.push_back(col);
        return static_cast<int>(cap.palette.size() - 1);
    };

    for (const auto& [pos, cell] : grid.cells) {
        UiCell uc;
        uc.col = pos.first / cap.pitchX;
        uc.row = pos.second / cap.pitchY;
        uc.ch = static_cast<char>(cell.ch);
        uc.fgStyle = addColor({cell.fg[0], cell.fg[1], cell.fg[2]});
        uc.bgStyle = addColor({cell.bg[0], cell.bg[1], cell.bg[2]});
        cap.cells.push_back(uc);
    }

    for (const auto& rect : grid.highlights) {
        UiRect ur;
        ur.col = rect.x / cap.pitchX;
        ur.row = rect.y / cap.pitchY;
        ur.offsetX = rect.x % cap.pitchX;
        ur.offsetY = rect.y % cap.pitchY;
        ur.wPx = rect.w;
        ur.hPx = rect.h;
        ur.style = addColor({rect.c[0], rect.c[1], rect.c[2]});
        cap.rects.push_back(ur);
    }

    buildPalette(cap);
    return cap;
}

std::string toJson(const UiCapture& c) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"screen\": \"" << c.screen << "\",\n";
    ss << "  \"firmware\": \"" << c.firmware << "\",\n";
    ss << "  \"font_mode\": " << c.fontMode << ",\n";
    ss << "  \"pitch_x\": " << c.pitchX << ",\n";
    ss << "  \"pitch_y\": " << c.pitchY << ",\n";
    ss << "  \"settled\": " << (c.settled ? "true" : "false") << ",\n";
    ss << "  \"theme_id\": \"" << c.themeId << "\",\n";

    ss << "  \"palette\": [\n";
    for (size_t i = 0; i < c.palette.size(); ++i) {
        const auto& col = c.palette[i];
        ss << "    [" << (int)col[0] << ", " << (int)col[1] << ", " << (int)col[2] << "]"
           << (i + 1 < c.palette.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    ss << "  \"cells\": [\n";
    for (size_t i = 0; i < c.cells.size(); ++i) {
        const auto& cl = c.cells[i];
        char escaped[8];
        if (cl.ch == '"') std::snprintf(escaped, sizeof(escaped), "\\\"");
        else if (cl.ch == '\\') std::snprintf(escaped, sizeof(escaped), "\\\\");
        else std::snprintf(escaped, sizeof(escaped), "%c", cl.ch);

        ss << "    {\"col\":" << cl.col << ",\"row\":" << cl.row
           << ",\"ch\":\"" << escaped << "\",\"fg\":" << cl.fgStyle << ",\"bg\":" << cl.bgStyle << "}"
           << (i + 1 < c.cells.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    ss << "  \"rects\": [\n";
    for (size_t i = 0; i < c.rects.size(); ++i) {
        const auto& r = c.rects[i];
        ss << "    {\"col\":" << r.col << ",\"row\":" << r.row
           << ",\"off_x\":" << r.offsetX << ",\"off_y\":" << r.offsetY
           << ",\"w_px\":" << r.wPx << ",\"h_px\":" << r.hPx << ",\"style\":" << r.style << "}"
           << (i + 1 < c.rects.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

bool fromJson(const std::string& text, UiCapture& out, std::string& err) {
    if (text.empty()) {
        err = "empty json input";
        return false;
    }
    // Minimal JSON parsing for UiCapture format
    auto findKeyString = [&](const std::string& key) -> std::string {
        size_t p = text.find("\"" + key + "\":");
        if (p == std::string::npos) return "";
        size_t b = text.find('"', p + key.size() + 3);
        if (b == std::string::npos) return "";
        size_t e = text.find('"', b + 1);
        if (e == std::string::npos) return "";
        return text.substr(b + 1, e - b - 1);
    };

    auto findKeyInt = [&](const std::string& key) -> int {
        size_t p = text.find("\"" + key + "\":");
        if (p == std::string::npos) return 0;
        size_t b = text.find_first_of("-0123456789", p + key.size() + 2);
        if (b == std::string::npos) return 0;
        return std::stoi(text.substr(b));
    };

    auto findKeyBool = [&](const std::string& key) -> bool {
        size_t p = text.find("\"" + key + "\":");
        if (p == std::string::npos) return false;
        return text.find("true", p) < text.find(",", p);
    };

    out.screen = findKeyString("screen");
    out.firmware = findKeyString("firmware");
    out.fontMode = findKeyInt("font_mode");
    out.pitchX = findKeyInt("pitch_x");
    out.pitchY = findKeyInt("pitch_y");
    out.settled = findKeyBool("settled");
    out.themeId = findKeyString("theme_id");

    // Parse cells array
    {
        size_t cb = text.find("\"cells\":");
        if (cb != std::string::npos) {
            size_t arrStart = text.find('[', cb);
            if (arrStart != std::string::npos) {
                size_t arrEnd = text.find(']', arrStart);
                if (arrEnd != std::string::npos) {
                    std::string arr = text.substr(arrStart + 1, arrEnd - arrStart - 1);
                    size_t pos = 0;
                    while (pos < arr.size()) {
                        size_t objStart = arr.find('{', pos);
                        if (objStart == std::string::npos) break;
                        size_t objEnd = arr.find('}', objStart);
                        if (objEnd == std::string::npos) break;
                        std::string obj = arr.substr(objStart + 1, objEnd - objStart - 1);

                        UiCell cell;
                        auto getField = [&](const std::string& key) -> std::string {
                            size_t p = obj.find("\"" + key + "\":");
                            if (p == std::string::npos) return "";
                            size_t v = p + key.size() + 3;
                            if (v >= obj.size()) return "";
                            if (obj[v] == '"') {
                                size_t e = obj.find('"', v + 1);
                                return (e != std::string::npos) ? obj.substr(v + 1, e - v - 1) : "";
                            }
                            size_t e = obj.find_first_of(",}", v);
                            return (e != std::string::npos) ? obj.substr(v, e - v) : obj.substr(v);
                        };

                        auto toInt = [](const std::string& s, int def = 0) -> int {
                            try { return std::stoi(s); } catch (...) { return def; }
                        };
                        std::string chStr = getField("ch");

                        cell.col = toInt(getField("col"));
                        cell.row = toInt(getField("row"));
                        cell.ch = chStr.empty() ? ' ' : chStr[0];
                        cell.fgStyle = toInt(getField("fg"), -1);
                        cell.bgStyle = toInt(getField("bg"), -1);
                        out.cells.push_back(cell);

                        pos = objEnd + 1;
                    }
                }
            }
        }
    }

    // Parse rects array
    {
        size_t rb = text.find("\"rects\":");
        if (rb != std::string::npos) {
            size_t arrStart = text.find('[', rb);
            if (arrStart != std::string::npos) {
                size_t arrEnd = text.find(']', arrStart);
                if (arrEnd != std::string::npos) {
                    std::string arr = text.substr(arrStart + 1, arrEnd - arrStart - 1);
                    size_t pos = 0;
                    while (pos < arr.size()) {
                        size_t objStart = arr.find('{', pos);
                        if (objStart == std::string::npos) break;
                        size_t objEnd = arr.find('}', objStart);
                        if (objEnd == std::string::npos) break;
                        std::string obj = arr.substr(objStart + 1, objEnd - objStart - 1);

                        UiRect rect;
                        auto getField = [&](const std::string& key) -> std::string {
                            size_t p = obj.find("\"" + key + "\":");
                            if (p == std::string::npos) return "";
                            size_t v = p + key.size() + 3;
                            if (v >= obj.size()) return "";
                            size_t e = obj.find_first_of(",}", v);
                            return (e != std::string::npos) ? obj.substr(v, e - v) : obj.substr(v);
                        };
                        auto toInt = [](const std::string& s, int def = 0) -> int {
                            try { return std::stoi(s); } catch (...) { return def; }
                        };

                        rect.col = toInt(getField("col"));
                        rect.row = toInt(getField("row"));
                        rect.offsetX = toInt(getField("off_x"));
                        rect.offsetY = toInt(getField("off_y"));
                        rect.wPx = toInt(getField("w_px"));
                        rect.hPx = toInt(getField("h_px"));
                        rect.style = toInt(getField("style"), -1);
                        out.rects.push_back(rect);

                        pos = objEnd + 1;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace dev
} // namespace m8
