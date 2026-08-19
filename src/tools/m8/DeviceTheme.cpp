// ===========================================================================
// DeviceTheme.cpp — theme accent table implementation.
// ===========================================================================

#include "DeviceTheme.h"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace m8 {
namespace dev {

static ThemeTable s_theme;

ThemeTable& getTheme() { return s_theme; }

const char* accentSourceName(AccentSource s) {
    switch (s) {
    case AccentSource::BUILTIN_DEFAULT: return "builtin-default";
    case AccentSource::FILE:            return "hw_theme.json";
    case AccentSource::FLAG:            return "--cursor-color";
    case AccentSource::CALIBRATED:      return "calibrated";
    }
    return "unknown";
}

bool ThemeTable::matches(const uint8_t rgb[3]) const {
    for (int i = 0; i < 3; ++i) {
        const int d = static_cast<int>(rgb[i]) - static_cast<int>(accent[i]);
        if (d > tolerance || d < -tolerance) return false;
    }
    return true;
}

// Same minimal JSON helpers as Gestures.cpp. Kept local rather than shared:
// two small readers are cheaper than a dependency between the two tables, and
// neither file needs a general parser.
static bool jsonInt(const std::string& json, const std::string& key, int& out) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;
    if (pos >= json.size()) return false;
    char* end = nullptr;
    long val = std::strtol(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) return false;
    out = static_cast<int>(val);
    return true;
}

static bool jsonString(const std::string& json, const std::string& key, std::string& out) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    pos++;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return false;
    out = json.substr(pos, end - pos);
    return true;
}

// Reads the three ints of "accent": [r, g, b].
static bool jsonRgb(const std::string& json, const std::string& key, uint8_t out[3]) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return false;
    const auto close = json.find(']', pos);
    if (close == std::string::npos) return false;

    int v[3] = {-1, -1, -1};
    if (std::sscanf(json.c_str() + pos, "[ %d , %d , %d", &v[0], &v[1], &v[2]) != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        if (v[i] < 0 || v[i] > 255) return false;
        out[i] = static_cast<uint8_t>(v[i]);
    }
    return true;
}

bool ThemeTable::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    const std::string json((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

    uint8_t rgb[3];
    if (!jsonRgb(json, "accent", rgb)) return false;

    // Only adopt the file's value once it claims to have been confirmed against
    // hardware. An unpinned file is a draft, and silently trusting one would
    // reintroduce exactly the "we assumed a colour" failure this table exists to
    // end -- just with an extra step.
    std::string confirmed;
    const bool isPinnedFile =
        (jsonString(json, "pinned", confirmed) && confirmed == "true") ||
        json.find("\"pinned\": true") != std::string::npos ||
        json.find("\"pinned\":true") != std::string::npos;
    if (!isPinnedFile) return false;

    accent[0] = rgb[0];
    accent[1] = rgb[1];
    accent[2] = rgb[2];

    int tol = 0;
    if (jsonInt(json, "tolerance", tol) && tol >= 0 && tol <= 128) tolerance = tol;

    std::string fwStr;
    if (jsonString(json, "firmware", fwStr)) {
        int major = 0, minor = 0, patch = 0;
        if (std::sscanf(fwStr.c_str(), "%d.%d.%d", &major, &minor, &patch) >= 2) {
            pinnedFwMajor = major;
            pinnedFwMinor = minor;
            pinnedFwPatch = patch;
        }
    }
    jsonString(json, "theme_id", themeId);
    jsonString(json, "method", method);

    pinned = true;
    source = AccentSource::FILE;
    return true;
}

bool ThemeTable::saveToFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"pinned\": " << (pinned ? "true" : "false") << ",\n";
    ss << "  \"firmware\": \"" << pinnedFwMajor << "." << pinnedFwMinor << "."
       << pinnedFwPatch << "\",\n";
    ss << "  \"theme_id\": \"" << themeId << "\",\n";
    ss << "  \"accent\": [" << (int)accent[0] << ", " << (int)accent[1] << ", "
       << (int)accent[2] << "],\n";
    ss << "  \"tolerance\": " << tolerance << ",\n";
    ss << "  \"method\": \"" << method << "\",\n";
    ss << "  \"notes\": \"The cursor accent, pinned by observation rather than "
          "assumption: a direction key is pressed and the palette entry whose "
          "cells move is the cursor. Re-pin with m8_nav --pin-theme after "
          "changing the device theme. See hw_findings.md UI-14.\"\n";
    ss << "}\n";

    f << ss.str();
    return f.good();
}

} // namespace dev
} // namespace m8
