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
    // Grid screens only (-1 elsewhere). Without these, a caller on SONG/CHAIN/
    // PHRASE/TABLE could not tell where the cursor was: `cursorField` there is
    // just the row label concatenated with the cell text ("04 --"), which names
    // neither axis. gridStep/gridCol are the addressable coordinates -- the same
    // pair moveCursorToGrid navigates by, so reporting cannot drift from acting.
    int         gridStep      = -1;
    int         gridCol       = -1;
    int         gridColumns   = 0;
    std::vector<std::pair<int, std::string>> rows;

    std::string toJson() const;
};

SemanticState semanticState(M8Device& dev);

} // namespace dev
} // namespace m8
