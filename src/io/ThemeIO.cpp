#include "ThemeIO.h"
#include <fstream>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace m8 {
namespace io {

bool loadTheme(const std::string& path, ui::Theme& theme, std::string& err) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        err = "Failed to open theme file";
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < 51) { // 12 bytes header + 39 bytes RGB
        err = "Invalid theme file size";
        return false;
    }

    size_t offset = 12; // Skip version header
    for (size_t i = 0; i < static_cast<size_t>(ui::ThemeSlot::COUNT); ++i) {
        if (offset + 3 <= data.size()) {
            theme.colors[i].r = data[offset++];
            theme.colors[i].g = data[offset++];
            theme.colors[i].b = data[offset++];
        }
    }

    std::filesystem::path p(path);
    std::string stem = p.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
    if (stem.length() > 12) stem = stem.substr(0, 12);
    while (stem.length() < 12) stem += '-';
    std::strncpy(theme.name, stem.c_str(), 12);
    theme.name[12] = '\0';

    return true;
}

bool saveTheme(const std::string& path, const ui::Theme& theme, std::string& err) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        err = "Failed to create theme file";
        return false;
    }

    // Write 12-byte header: "M8VERSION\0\0\0" or 1.4.0 version bytes
    uint8_t header[12] = {1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    file.write(reinterpret_cast<const char*>(header), 12);

    for (size_t i = 0; i < static_cast<size_t>(ui::ThemeSlot::COUNT); ++i) {
        uint8_t rgb[3] = {theme.colors[i].r, theme.colors[i].g, theme.colors[i].b};
        file.write(reinterpret_cast<const char*>(rgb), 3);
    }

    return true;
}

} // namespace io
} // namespace m8
