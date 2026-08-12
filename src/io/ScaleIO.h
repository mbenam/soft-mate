#pragma once

#include "engine/Engine.h"
#include <string>

namespace m8::io {

// Loads a scale from a .m8n file into an engine::Scale structure
bool loadScale(const std::string& path, engine::Scale& outScale, std::string& error);

// Saves an engine::Scale structure to a .m8n file in dirPath
bool saveScale(const std::string& dirPath, const engine::Scale& scale, std::string& outPath, std::string& error);

// Ensures the Scales and Scales/Factory directories exist and are populated with standard scales
void ensureFactoryScales(const std::string& baseDir = "Scales");

} // namespace m8::io
