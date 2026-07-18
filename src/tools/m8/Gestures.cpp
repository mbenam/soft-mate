// ===========================================================================
// Gestures.cpp — gesture table implementation.
// ===========================================================================

#include "Gestures.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace m8 {
namespace dev {

static GestureTable s_gestures;

GestureTable& getGestures() { return s_gestures; }

// Simple JSON helper: extract an integer value for a key.
static bool jsonInt(const std::string& json, const std::string& key, int& out) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    // Skip whitespace and colon.
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;
    if (pos >= json.size()) return false;
    // Check for null.
    if (json.compare(pos, 4, "null") == 0) { out = 0; return true; }
    // Parse integer.
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
    pos++; // skip opening quote
    auto end = json.find('"', pos);
    if (end == std::string::npos) return false;
    out = json.substr(pos, end - pos);
    return true;
}

bool GestureTable::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    // Parse firmware version.
    int major = 0, minor = 0, patch = 0;
    std::string fwStr;
    if (jsonString(json, "firmware", fwStr)) {
        // Parse "6.5.2"
        if (sscanf(fwStr.c_str(), "%d.%d.%d", &major, &minor, &patch) >= 2) {
            pinnedFwMajor = major;
            pinnedFwMinor = minor;
            pinnedFwPatch = patch;
        }
    }

    // Parse gesture masks.
    int v;
    if (jsonInt(json, "value_inc", v))   valueInc   = static_cast<uint8_t>(v);
    if (jsonInt(json, "value_dec", v))   valueDec   = static_cast<uint8_t>(v);
    if (jsonInt(json, "value_inc16", v)) valueInc16 = static_cast<uint8_t>(v);
    if (jsonInt(json, "value_dec16", v)) valueDec16 = static_cast<uint8_t>(v);
    if (jsonInt(json, "enum_next", v))   enumNext   = static_cast<uint8_t>(v);
    if (jsonInt(json, "enum_prev", v))   enumPrev   = static_cast<uint8_t>(v);
    if (jsonInt(json, "toggle", v))      toggle     = static_cast<uint8_t>(v);
    if (jsonInt(json, "note_enter", v))  noteEnter  = static_cast<uint8_t>(v);
    if (jsonInt(json, "note_inc", v))    noteInc    = static_cast<uint8_t>(v);
    if (jsonInt(json, "note_dec", v))    noteDec    = static_cast<uint8_t>(v);
    if (jsonInt(json, "note_oct_inc", v)) noteOctInc = static_cast<uint8_t>(v);
    if (jsonInt(json, "note_oct_dec", v)) noteOctDec = static_cast<uint8_t>(v);
    if (jsonInt(json, "insert_default", v)) insertDefault = static_cast<uint8_t>(v);
    if (jsonInt(json, "clear_cell", v))  clearCell  = static_cast<uint8_t>(v);

    // Mark populated if at least the core value-edit gestures are set.
    populated = (valueInc != 0 && valueDec != 0 && valueInc16 != 0 && valueDec16 != 0);

    return populated;
}

bool GestureTable::saveToFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;

    f << "{\n";
    f << "  \"confirmed\": " << (populated ? "true" : "false") << ",\n";
    f << "  \"firmware\": \"" << pinnedFwMajor << "." << pinnedFwMinor << "." << pinnedFwPatch << "\",\n";
    f << "  \"keys\": {\n";
    f << "    \"LEFT\": 128,\n";
    f << "    \"UP\": 64,\n";
    f << "    \"DOWN\": 32,\n";
    f << "    \"SHIFT\": 16,\n";
    f << "    \"PLAY\": 8,\n";
    f << "    \"RIGHT\": 4,\n";
    f << "    \"OPT\": 2,\n";
    f << "    \"EDIT\": 1\n";
    f << "  },\n";
    f << "  \"gestures\": {\n";
    f << "    \"value_inc\": " << (int)valueInc << ",\n";
    f << "    \"value_dec\": " << (int)valueDec << ",\n";
    f << "    \"value_inc16\": " << (int)valueInc16 << ",\n";
    f << "    \"value_dec16\": " << (int)valueDec16 << ",\n";
    f << "    \"enum_next\": " << (int)enumNext << ",\n";
    f << "    \"enum_prev\": " << (int)enumPrev << ",\n";
    f << "    \"toggle\": " << (toggle ? std::to_string((int)toggle) : "null") << ",\n";
    f << "    \"note_enter\": " << (noteEnter ? std::to_string((int)noteEnter) : "null") << ",\n";
    f << "    \"note_inc\": " << (noteInc ? std::to_string((int)noteInc) : "null") << ",\n";
    f << "    \"note_dec\": " << (noteDec ? std::to_string((int)noteDec) : "null") << ",\n";
    f << "    \"note_oct_inc\": " << (noteOctInc ? std::to_string((int)noteOctInc) : "null") << ",\n";
    f << "    \"note_oct_dec\": " << (noteOctDec ? std::to_string((int)noteOctDec) : "null") << ",\n";
    f << "    \"insert_default\": " << (insertDefault ? std::to_string((int)insertDefault) : "null") << ",\n";
    f << "    \"clear_cell\": " << (clearCell ? std::to_string((int)clearCell) : "null") << "\n";
    f << "  }\n";
    f << "}\n";
    return true;
}

} // namespace dev
} // namespace m8
