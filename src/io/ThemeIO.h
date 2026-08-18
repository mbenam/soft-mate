#pragma once
#include "../ui/Theme.h"
#include <string>

namespace m8 {
namespace io {

bool loadTheme(const std::string& path, ui::Theme& theme, std::string& err);
bool saveTheme(const std::string& path, const ui::Theme& theme, std::string& err);

} // namespace io
} // namespace m8
