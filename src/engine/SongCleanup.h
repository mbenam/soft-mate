#pragma once
#include "Sequencer.h"
#include "Engine.h"

namespace m8::engine {

using ClearPhrasesResult = Sequencer::ClearPhrasesResult;

struct ClearInstrumentsResult {
    int clearedInstruments = 0;
    int deduplicatedInstruments = 0;
    int clearedTables = 0;
    int deduplicatedTables = 0;
};

ClearPhrasesResult ClearUnusedPhrasesAndChains(Sequencer& seq);
ClearInstrumentsResult ClearUnusedInstrumentsAndTables(Sequencer& seq, EngineState& state);

} // namespace m8::engine
