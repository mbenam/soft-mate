#include "SongCleanup.h"
#include <cstring>
#include <vector>

namespace m8::engine {

namespace {

static bool stepsEqual(const Step& a, const Step& b) {
    if (a.note != b.note || a.vol != b.vol || a.instr != b.instr) return false;
    for (int i = 0; i < 3; ++i) {
        if (a.fx[i].cmd != b.fx[i].cmd || a.fx[i].val != b.fx[i].val) return false;
    }
    return true;
}

static bool phrasesEqual(const Step a[16], const Step b[16]) {
    for (int i = 0; i < 16; ++i) {
        if (!stepsEqual(a[i], b[i])) return false;
    }
    return true;
}

static bool isPhraseEmpty(const Step p[16]) {
    for (int i = 0; i < 16; ++i) {
        if (p[i].note != NOTE_EMPTY || p[i].vol != VOL_EMPTY || p[i].instr != INST_EMPTY) return false;
        for (int f = 0; f < 3; ++f) {
            if (p[i].fx[f].cmd != FxCmd::NONE || p[i].fx[f].val != 0) return false;
        }
    }
    return true;
}

static bool chainsEqual(const ChainStep a[16], const ChainStep b[16]) {
    for (int i = 0; i < 16; ++i) {
        if (a[i].phrase != b[i].phrase || a[i].tsp != b[i].tsp) return false;
    }
    return true;
}

static bool isChainEmpty(const ChainStep c[16]) {
    for (int i = 0; i < 16; ++i) {
        if (c[i].phrase != PHRASE_EMPTY || c[i].tsp != 0) return false;
    }
    return true;
}

static bool modulatorsEqual(const Modulator& a, const Modulator& b) {
    return a.type == b.type &&
           a.dest == b.dest &&
           a.amt == b.amt &&
           a.p1 == b.p1 &&
           a.p2 == b.p2 &&
           a.p3 == b.p3 &&
           a.p4 == b.p4;
}

static bool instrumentsEqual(const Instrument& a, const Instrument& b) {
    if (a.type != b.type) return false;
    if (std::strncmp(a.name, b.name, 12) != 0) return false;

    for (int i = 0; i < 4; ++i) {
        if (!modulatorsEqual(a.mods[i], b.mods[i])) return false;
    }

    switch (a.type) {
    case InstType::INST_SAMPLER:
        return std::strncmp(a.sampler.samplePath, b.sampler.samplePath, sizeof(a.sampler.samplePath)) == 0 &&
               a.sampler.transp == b.sampler.transp &&
               a.sampler.tbl_tic == b.sampler.tbl_tic &&
               a.sampler.eq == b.sampler.eq &&
               a.sampler.slice == b.sampler.slice &&
               a.sampler.play == b.sampler.play &&
               a.sampler.start == b.sampler.start &&
               a.sampler.loop_st == b.sampler.loop_st &&
               a.sampler.length == b.sampler.length &&
               a.sampler.detune == b.sampler.detune &&
               a.sampler.degrade == b.sampler.degrade &&
               a.sampler.filter_type == b.sampler.filter_type &&
               a.sampler.cutoff == b.sampler.cutoff &&
               a.sampler.res == b.sampler.res &&
               a.sampler.amp == b.sampler.amp &&
               a.sampler.lim == b.sampler.lim &&
               a.sampler.pan == b.sampler.pan &&
               a.sampler.dry == b.sampler.dry &&
               a.sampler.cho == b.sampler.cho &&
               a.sampler.del == b.sampler.del &&
               a.sampler.rev == b.sampler.rev;

    case InstType::INST_MACROSYN:
        return a.macrosyn.transp == b.macrosyn.transp &&
               a.macrosyn.tbl_tic == b.macrosyn.tbl_tic &&
               a.macrosyn.eq == b.macrosyn.eq &&
               a.macrosyn.shape == b.macrosyn.shape &&
               a.macrosyn.timbre == b.macrosyn.timbre &&
               a.macrosyn.color == b.macrosyn.color &&
               a.macrosyn.degrade == b.macrosyn.degrade &&
               a.macrosyn.redux == b.macrosyn.redux &&
               a.macrosyn.filter_type == b.macrosyn.filter_type &&
               a.macrosyn.cutoff == b.macrosyn.cutoff &&
               a.macrosyn.res == b.macrosyn.res &&
               a.macrosyn.amp == b.macrosyn.amp &&
               a.macrosyn.lim == b.macrosyn.lim &&
               a.macrosyn.pan == b.macrosyn.pan &&
               a.macrosyn.dry == b.macrosyn.dry &&
               a.macrosyn.cho == b.macrosyn.cho &&
               a.macrosyn.del == b.macrosyn.del &&
               a.macrosyn.rev == b.macrosyn.rev;

    case InstType::INST_WAVSYNTH:
        return a.wav.transp == b.wav.transp &&
               a.wav.tbl_tic == b.wav.tbl_tic &&
               a.wav.eq == b.wav.eq &&
               a.wav.shape == b.wav.shape &&
               a.wav.size == b.wav.size &&
               a.wav.mult == b.wav.mult &&
               a.wav.warp == b.wav.warp &&
               a.wav.scan == b.wav.scan &&
               a.wav.filter_type == b.wav.filter_type &&
               a.wav.cutoff == b.wav.cutoff &&
               a.wav.res == b.wav.res &&
               a.wav.amp == b.wav.amp &&
               a.wav.lim == b.wav.lim &&
               a.wav.pan == b.wav.pan &&
               a.wav.dry == b.wav.dry &&
               a.wav.cho == b.wav.cho &&
               a.wav.del == b.wav.del &&
               a.wav.rev == b.wav.rev;

    case InstType::INST_HYPERSYN:
        if (a.hyper.transp != b.hyper.transp ||
            a.hyper.tbl_tic != b.hyper.tbl_tic ||
            a.hyper.eq != b.hyper.eq ||
            a.hyper.scale != b.hyper.scale ||
            a.hyper.shift != b.hyper.shift ||
            a.hyper.swarm != b.hyper.swarm ||
            a.hyper.width != b.hyper.width ||
            a.hyper.subosc != b.hyper.subosc ||
            a.hyper.filter_type != b.hyper.filter_type ||
            a.hyper.cutoff != b.hyper.cutoff ||
            a.hyper.res != b.hyper.res ||
            a.hyper.amp != b.hyper.amp ||
            a.hyper.lim != b.hyper.lim ||
            a.hyper.pan != b.hyper.pan ||
            a.hyper.dry != b.hyper.dry ||
            a.hyper.cho != b.hyper.cho ||
            a.hyper.del != b.hyper.del ||
            a.hyper.rev != b.hyper.rev) return false;
        for (int i = 0; i < 7; ++i) {
            if (a.hyper.default_chord[i] != b.hyper.default_chord[i]) return false;
        }
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 6; ++j) {
                if (a.hyper.chords[i][j] != b.hyper.chords[i][j]) return false;
            }
        }
        return true;

    case InstType::INST_FMSYNTH:
        if (a.fm.transp != b.fm.transp ||
            a.fm.tbl_tic != b.fm.tbl_tic ||
            a.fm.eq != b.fm.eq ||
            a.fm.algo != b.fm.algo ||
            a.fm.mod1 != b.fm.mod1 ||
            a.fm.mod2 != b.fm.mod2 ||
            a.fm.mod3 != b.fm.mod3 ||
            a.fm.mod4 != b.fm.mod4 ||
            a.fm.filter_type != b.fm.filter_type ||
            a.fm.cutoff != b.fm.cutoff ||
            a.fm.res != b.fm.res ||
            a.fm.amp != b.fm.amp ||
            a.fm.lim != b.fm.lim ||
            a.fm.pan != b.fm.pan ||
            a.fm.dry != b.fm.dry ||
            a.fm.cho != b.fm.cho ||
            a.fm.del != b.fm.del ||
            a.fm.rev != b.fm.rev) return false;
        for (int i = 0; i < 4; ++i) {
            if (a.fm.ops[i].shape != b.fm.ops[i].shape ||
                a.fm.ops[i].ratio != b.fm.ops[i].ratio ||
                a.fm.ops[i].ratio_fine != b.fm.ops[i].ratio_fine ||
                a.fm.ops[i].level != b.fm.ops[i].level ||
                a.fm.ops[i].feedback != b.fm.ops[i].feedback ||
                a.fm.ops[i].retrigger != b.fm.ops[i].retrigger ||
                a.fm.ops[i].mod_a != b.fm.ops[i].mod_a ||
                a.fm.ops[i].mod_b != b.fm.ops[i].mod_b) return false;
        }
        return true;

    default:
        return true;
    }
}

static bool isInstrumentEmpty(const Instrument& inst) {
    if (inst.type == InstType::INST_SAMPLER && inst.sampler.samplePath[0] == '\0') {
        if (std::strncmp(inst.name, "------------", 12) == 0 ||
            std::strncmp(inst.name, "            ", 12) == 0 ||
            inst.name[0] == '\0') return true;
    }
    return false;
}

static bool tablesEqual(const TableStep a[16], const TableStep b[16]) {
    for (int i = 0; i < 16; ++i) {
        if (a[i].transp != b[i].transp || a[i].vol != b[i].vol) return false;
        for (int f = 0; f < 3; ++f) {
            if (a[i].fx[f].cmd != b[i].fx[f].cmd || a[i].fx[f].val != b[i].fx[f].val) return false;
        }
    }
    return true;
}

static bool isTableEmpty(const TableStep a[16]) {
    for (int i = 0; i < 16; ++i) {
        if (a[i].transp != 0 || a[i].vol != VOL_EMPTY) return false;
        for (int f = 0; f < 3; ++f) {
            if (a[i].fx[f].cmd != FxCmd::NONE || a[i].fx[f].val != 0) return false;
        }
    }
    return true;
}

} // namespace

ClearPhrasesResult ClearUnusedPhrasesAndChains(Sequencer& seq) {
    ClearPhrasesResult result;

    // 1. Deduplicate phrases: build mapping
    uint8_t remapPhrase[Sequencer::NUM_PHRASES];
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        remapPhrase[p] = static_cast<uint8_t>(p);
        for (int orig = 0; orig < p; ++orig) {
            if (phrasesEqual(seq.phrases[p], seq.phrases[orig])) {
                remapPhrase[p] = static_cast<uint8_t>(orig);
                result.deduplicatedPhrases++;
                break;
            }
        }
    }

    // Remap all chain steps
    for (int c = 0; c < Sequencer::NUM_CHAINS; ++c) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            if (seq.chains[c][s].phrase != PHRASE_EMPTY && seq.chains[c][s].phrase < Sequencer::NUM_PHRASES) {
                seq.chains[c][s].phrase = remapPhrase[seq.chains[c][s].phrase];
            }
        }
    }

    // 2. Deduplicate chains: build mapping
    uint8_t remapChain[Sequencer::NUM_CHAINS];
    for (int c = 0; c < Sequencer::NUM_CHAINS; ++c) {
        remapChain[c] = static_cast<uint8_t>(c);
        for (int orig = 0; orig < c; ++orig) {
            if (chainsEqual(seq.chains[c], seq.chains[orig])) {
                remapChain[c] = static_cast<uint8_t>(orig);
                result.deduplicatedChains++;
                break;
            }
        }
    }

    // Remap all song rows
    for (int r = 0; r < Sequencer::SONG_ROWS; ++r) {
        for (int t = 0; t < 8; ++t) {
            if (seq.song[r].tracks[t] != CHAIN_EMPTY && seq.song[r].tracks[t] < Sequencer::NUM_CHAINS) {
                seq.song[r].tracks[t] = remapChain[seq.song[r].tracks[t]];
            }
        }
    }

    // 3. Reachability analysis
    bool chainUsed[Sequencer::NUM_CHAINS] = {false};
    bool phraseUsed[Sequencer::NUM_PHRASES] = {false};

    for (int r = 0; r < Sequencer::SONG_ROWS; ++r) {
        for (int t = 0; t < 8; ++t) {
            uint8_t c = seq.song[r].tracks[t];
            if (c != CHAIN_EMPTY && c < Sequencer::NUM_CHAINS) {
                chainUsed[c] = true;
            }
        }
    }

    for (int c = 0; c < Sequencer::NUM_CHAINS; ++c) {
        if (chainUsed[c]) {
            for (int s = 0; s < Sequencer::ROWS; ++s) {
                uint8_t p = seq.chains[c][s].phrase;
                if (p != PHRASE_EMPTY && p < Sequencer::NUM_PHRASES) {
                    phraseUsed[p] = true;
                }
            }
        }
    }

    // 4. Sweep unused chains
    for (int c = 0; c < Sequencer::NUM_CHAINS; ++c) {
        if (!chainUsed[c]) {
            if (!isChainEmpty(seq.chains[c])) {
                result.clearedChains++;
            }
            for (int s = 0; s < Sequencer::ROWS; ++s) {
                seq.chains[c][s] = ChainStep{};
            }
        }
    }

    // 5. Sweep unused phrases
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        if (!phraseUsed[p]) {
            if (!isPhraseEmpty(seq.phrases[p])) {
                result.clearedPhrases++;
            }
            for (int s = 0; s < Sequencer::ROWS; ++s) {
                seq.phrases[p][s] = Step{};
            }
        }
    }

    return result;
}

ClearInstrumentsResult ClearUnusedInstrumentsAndTables(Sequencer& seq, EngineState& state) {
    ClearInstrumentsResult result;

    const int numInsts = static_cast<int>(state.instruments.size());

    // 1. Deduplicate instruments
    // 1. Deduplicate instruments (only non-empty)
    uint8_t remapInst[128];
    for (int i = 0; i < 128 && i < numInsts; ++i) {
        remapInst[i] = static_cast<uint8_t>(i);
        if (isInstrumentEmpty(state.instruments[i])) continue;
        for (int orig = 0; orig < i; ++orig) {
            if (isInstrumentEmpty(state.instruments[orig])) continue;
            if (instrumentsEqual(state.instruments[i], state.instruments[orig])) {
                remapInst[i] = static_cast<uint8_t>(orig);
                result.deduplicatedInstruments++;
                break;
            }
        }
    }

    // Remap instrument references in all phrases
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            uint8_t inst = seq.phrases[p][s].instr;
            if (inst != INST_EMPTY && inst < 128) {
                seq.phrases[p][s].instr = remapInst[inst];
            }
        }
    }

    // 2. Reachability of instruments
    bool instUsed[128] = {false};
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            uint8_t inst = seq.phrases[p][s].instr;
            if (inst != INST_EMPTY && inst < 128) {
                instUsed[inst] = true;
            }
        }
    }

    // 3. Sweep unused instruments
    for (int i = 0; i < 128 && i < numInsts; ++i) {
        if (!instUsed[i]) {
            if (!isInstrumentEmpty(state.instruments[i])) {
                result.clearedInstruments++;
            }
            state.instruments[i] = Instrument{};
        }
    }

    // 4. Deduplicate tables (only non-empty)
    uint8_t remapTable[Sequencer::NUM_TABLES];
    for (int t = 0; t < Sequencer::NUM_TABLES; ++t) {
        remapTable[t] = static_cast<uint8_t>(t);
        if (isTableEmpty(seq.tables[t])) continue;
        for (int orig = 0; orig < t; ++orig) {
            if (isTableEmpty(seq.tables[orig])) continue;
            if (tablesEqual(seq.tables[t], seq.tables[orig])) {
                remapTable[t] = static_cast<uint8_t>(orig);
                result.deduplicatedTables++;
                break;
            }
        }
    }

    // Remap TBL fx commands in phrases and tables
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            for (int f = 0; f < 3; ++f) {
                if (seq.phrases[p][s].fx[f].cmd == FxCmd::TBL) {
                    seq.phrases[p][s].fx[f].val = remapTable[seq.phrases[p][s].fx[f].val];
                }
            }
        }
    }
    for (int t = 0; t < Sequencer::NUM_TABLES; ++t) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            for (int f = 0; f < 3; ++f) {
                if (seq.tables[t][s].fx[f].cmd == FxCmd::TBL) {
                    seq.tables[t][s].fx[f].val = remapTable[seq.tables[t][s].fx[f].val];
                }
            }
        }
    }

    // 5. Reachability of tables
    bool tableUsed[Sequencer::NUM_TABLES] = {false};
    for (int p = 0; p < Sequencer::NUM_PHRASES; ++p) {
        for (int s = 0; s < Sequencer::ROWS; ++s) {
            for (int f = 0; f < 3; ++f) {
                if (seq.phrases[p][s].fx[f].cmd == FxCmd::TBL) {
                    tableUsed[seq.phrases[p][s].fx[f].val] = true;
                }
            }
        }
    }

    // 6. Sweep unused tables
    for (int t = 0; t < Sequencer::NUM_TABLES; ++t) {
        if (!tableUsed[t]) {
            if (!isTableEmpty(seq.tables[t])) {
                result.clearedTables++;
            }
            for (int s = 0; s < Sequencer::ROWS; ++s) {
                seq.tables[t][s] = TableStep{};
            }
        }
    }

    return result;
}

} // namespace m8::engine
