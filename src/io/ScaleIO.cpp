#include "ScaleIO.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>
#include "types.hpp"

namespace fs = std::filesystem;

namespace m8::io {

bool loadScale(const std::string& path, engine::Scale& outScale, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open scale file: " + path;
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.size() < 42) {
        error = "Scale file is too small (" + std::to_string(buffer.size()) + " bytes)";
        return false;
    }

    try {
        BinaryReader reader(buffer);
        if (buffer.size() >= 56) {
            reader.read_bytes(14); // 10 byte prefix + 4 byte version
        } else if (buffer.size() >= 46) {
            reader.read_bytes(4);  // 4 byte version
        }

        uint16_t map = reader.read_u16_le();
        for (int i = 0; i < 12; ++i) {
            outScale.notes[i].enable = ((map >> i) & 1) != 0;
            int8_t base = static_cast<int8_t>(reader.read());
            uint8_t cents = reader.read();
            if (base < 0) {
                outScale.notes[i].offset = static_cast<float>(base) - (cents / 100.0f);
            } else {
                outScale.notes[i].offset = static_cast<float>(base) + (cents / 100.0f);
            }
        }

        std::string nameStr = reader.read_string(16);
        std::memset(outScale.name, '-', 16);
        outScale.name[16] = '\0';
        size_t nameLen = std::min<size_t>(nameStr.length(), 16);
        for (size_t i = 0; i < nameLen; ++i) {
            outScale.name[i] = nameStr[i];
        }

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool saveScale(const std::string& dirPath, const engine::Scale& scale, std::string& outPath, std::string& error) {
    std::string safeName = scale.name;
    while (!safeName.empty() && (safeName.back() == '-' || safeName.back() == ' ')) {
        safeName.pop_back();
    }
    if (safeName.empty()) {
        safeName = "SCALE";
    }

    // Replace invalid filename characters
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    std::error_code ec;
    fs::path dir(dirPath.empty() ? "Scales" : dirPath);
    if (!fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
    }

    fs::path targetFile = dir / (safeName + ".m8n");
    outPath = targetFile.generic_string();

    try {
        BinaryWriter writer(std::vector<uint8_t>{});
        std::vector<uint8_t> prefix(10, 0);
        std::memcpy(prefix.data(), "M8VERSION", 9);
        writer.write_bytes(prefix);
        m8::Version{2, 5, 0}.write(writer);

        // 1. Bitmask
        uint16_t map = 0;
        for (int i = 0; i < 12; ++i) {
            if (scale.notes[i].enable) {
                map |= (1 << i);
            }
        }
        writer.write_u16_le(map);

        // 2. 12 Notes
        for (int i = 0; i < 12; ++i) {
            float off = scale.notes[i].offset;
            int8_t base = static_cast<int8_t>(off >= 0 ? std::floor(off) : std::ceil(off));
            float frac = std::abs(off - static_cast<float>(base));
            uint8_t cents = static_cast<uint8_t>(std::round(frac * 100.0f));
            if (cents >= 100) cents = 99;
            writer.write(static_cast<uint8_t>(base));
            writer.write(cents);
        }

        // 3. Name (16 bytes)
        writer.write_string(std::string(scale.name, 16), 16);

        std::vector<uint8_t> bytes = writer.finish();
        std::ofstream out(targetFile, std::ios::binary);
        if (!out.is_open()) {
            error = "Could not open file for writing: " + outPath;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!out.good()) {
            error = "Failed to write scale bytes to: " + outPath;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void ensureFactoryScales(const std::string& baseDir) {
    std::error_code ec;
    fs::path scalesPath(baseDir);
    fs::path factoryPath = scalesPath / "Factory";

    if (!fs::exists(factoryPath, ec)) {
        fs::create_directories(factoryPath, ec);
    }

    // Populate standard factory scales if factory folder is empty
    struct FactoryPreset {
        const char* name;
        uint16_t mask; // 12-bit interval mask
    };

    static const FactoryPreset presets[] = {
        {"CHROMATIC-------", 0b111111111111},
        {"MAJOR-----------", 0b101010110101}, // 0,2,4,5,7,9,11
        {"NATURAL MINOR---", 0b010110101101}, // 0,2,3,5,7,8,10
        {"HARMONIC MINOR--", 0b100110101101}, // 0,2,3,5,7,8,11
        {"MELODIC MINOR---", 0b101010101101}, // 0,2,3,5,7,9,11
        {"DORIAN----------", 0b010101101101}, // 0,2,3,5,7,9,10
        {"PHRYGIAN--------", 0b010110101011}, // 0,1,3,5,7,8,10
        {"LYDIAN----------", 0b101010101011}, // 0,2,4,6,7,9,11
        {"MIXOLYDIAN------", 0b011010110101}, // 0,2,4,5,7,9,10
        {"LOCRIAN---------", 0b010110110011}, // 0,1,3,5,6,8,10
        {"PENTATONIC MAJ--", 0b001000101001}, // 0,2,4,7,9
        {"PENTATONIC MIN--", 0b010001001001}, // 0,3,5,7,10
        {"BLUES-----------", 0b010011001001}, // 0,3,5,6,7,10
        {"WHOLE TONE------", 0b010101010101}, // 0,2,4,6,8,10
    };

    for (const auto& p : presets) {
        std::string fname = p.name;
        while (!fname.empty() && (fname.back() == '-' || fname.back() == ' ')) fname.pop_back();
        fs::path fpath = factoryPath / (fname + ".m8n");
        if (!fs::exists(fpath, ec)) {
            engine::Scale scale;
            std::strncpy(scale.name, p.name, 16);
            scale.name[16] = '\0';
            for (int i = 0; i < 12; ++i) {
                scale.notes[i].enable = ((p.mask >> i) & 1) != 0;
                scale.notes[i].offset = 0.0f;
            }
            std::string outPath, err;
            saveScale(factoryPath.generic_string(), scale, outPath, err);
        }
    }
}

} // namespace m8::io
