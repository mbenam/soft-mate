#pragma once
#include <string>
#include <vector>
#include "M8Device.h"
#include "ScreenModel.h"

namespace m8 {
namespace dev {

struct SemanticState {
    Screen      screen        = Screen::UNKNOWN;
    std::string screenName;
    bool        isModal       = false;
    bool        isLiveMode    = false;
    bool        settled       = false;
    std::string cursorField;
    std::string cursorValue;
    int         cursorRow     = -1;
    std::vector<std::pair<int, std::string>> rows;

    std::string toJson() const;
};

SemanticState semanticState(M8Device& dev);

} // namespace dev
} // namespace m8
