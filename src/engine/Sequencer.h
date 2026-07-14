#pragma once

#include "SeqTypes.h"

namespace m8 {
namespace engine {

// TableStep moved to SeqTypes.h

enum class PlayMode {
    NONE,
    PHRASE,
    CHAIN,
    SONG
};

class Sequencer {
public:
    static constexpr int NUM_PHRASES = 256;
    static constexpr int NUM_CHAINS  = 256;
    static constexpr int NUM_TABLES  = 256;
    static constexpr int NUM_GROOVES = 32;
    static constexpr int SONG_ROWS   = 256;
    static constexpr int ROWS        = 16;

    Sequencer();
    ~Sequencer() = default;

    void loadDemoSong();
    void clear();

    // Track state arrays
    Step      phrases[NUM_PHRASES][ROWS];
    ChainStep chains [NUM_CHAINS ][ROWS];
    TableStep tables [NUM_TABLES ][ROWS];
    Groove    grooves[NUM_GROOVES];
    SongStep  song   [SONG_ROWS];
};

} // namespace engine
} // namespace m8
