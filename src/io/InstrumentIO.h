#pragma once

#include "engine/Engine.h"
#include <string>
#include <optional>

namespace m8::io {

// Loads an instrument from a .m8i file into an engine::Instrument structure and optional EqBank
bool loadInstrument(const std::string& path, engine::Instrument& outInst, std::optional<engine::EqBank>& outEq, std::string& error);

// Saves an engine::Instrument structure (and optional EqBank) to a .m8i file in dirPath
bool saveInstrument(const std::string& dirPath, const engine::Instrument& inst, const std::optional<engine::EqBank>& eq, std::string& outPath, std::string& error);

// Ensures the Instruments and Instruments/Factory directories exist and are populated with standard presets
void ensureFactoryInstruments(const std::string& baseDir = "Instruments");

} // namespace m8::io
