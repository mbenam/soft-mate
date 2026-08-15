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
    // Is the transport running? MEASURED 2026-08-15 on fw 6.5.2: the M8 draws a
    // '>' (0x3E) in the row-label gutter of a grid screen marking the step it is
    // playing, and removes it on stop. Nothing else on screen changes -- no
    // colour moves at all, which is why `inspect` reports "the press is not
    // landing" for a PLAY that worked perfectly. Its absence used to make any
    // playback-dependent probe unverifiable.
    //
    // GRID SCREENS ONLY. SONG, CHAIN, PHRASE and TABLE draw the playhead; form
    // screens (PROJECT, INSTRUMENT, SCALE, MIXER...) do not, so this reads false
    // there whatever the transport is doing. `playheadObservable` says which
    // case you are in, so a caller can tell "stopped" from "cannot tell".
    bool        isPlaying         = false;
    bool        playheadObservable = false;
    std::string cursorField;
    std::string cursorValue;
    int         cursorRow     = -1;
    // Pixel X of the cursor's leading cell. cursorField() has always computed
    // this and it was never reported, which left form-screen layout unreadable:
    // on MIXER, RIGHT and DOWN visit the same ROWS but different fields, and
    // without a column there is no way to tell whether a press moved sideways or
    // to a different block entirely.
    int         cursorCol     = -1;
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
