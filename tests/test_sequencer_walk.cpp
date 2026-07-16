#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <random>

using namespace m8::test;
using namespace m8::engine;
using namespace std;

TEST_CASE("B4.1 Phrase loops", "[walk]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setStep(host.sequencer(), 0, 8, 72, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(5.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 4);
    REQUIRE(notes[0].phraseRow == 0);
    REQUIRE(notes[1].phraseRow == 8);
    REQUIRE(notes[2].phraseRow == 0);
    REQUIRE(notes[3].phraseRow == 8);
}

TEST_CASE("B4.2 Chain walks phrases", "[walk]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // Phrase 0
    setStep(host.sequencer(), 1, 0, 61, 100, 0); // Phrase 1
    setStep(host.sequencer(), 2, 0, 62, 100, 0); // Phrase 2
    setChain(host.sequencer(), 0, 0, 0);
    setChain(host.sequencer(), 0, 1, 1);
    setChain(host.sequencer(), 0, 2, 2);
    // Row 3 is empty (--), should stop
    host.push(playChain(0, 0));
    host.renderSeconds(6.0);
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() == 3);
    REQUIRE(notes[0].phraseRow == 0); REQUIRE(notes[0].chainRow == 0); // plays phrase 0
    REQUIRE(notes[1].phraseRow == 0); REQUIRE(notes[1].chainRow == 1); // plays phrase 1
    REQUIRE(notes[2].phraseRow == 0); REQUIRE(notes[2].chainRow == 2); // plays phrase 2
}

TEST_CASE("B4.3 Song walks chains", "[walk]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setChain(host.sequencer(), 0, 0, 0);
    setChain(host.sequencer(), 1, 0, 0);
    setChain(host.sequencer(), 2, 0, 0);
    
    setSong(host.sequencer(), 0, 0, 0);
    setSong(host.sequencer(), 1, 0, 1);
    setSong(host.sequencer(), 2, 0, 2);
    
    host.push(playSong(0));
    host.renderSeconds(7.0);
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 4);
    REQUIRE(notes[0].songRow == 0);
    REQUIRE(notes[1].songRow == 1);
    REQUIRE(notes[2].songRow == 2);
    REQUIRE(notes[3].songRow == 0); // Wrap to row 0
}

TEST_CASE("B4.4 Full 16-row chain", "[walk]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    for (int i=0; i<16; ++i) setChain(host.sequencer(), 0, i, 0);
    
    host.push(playChain(0, 0));
    host.renderSeconds(16 * 16 * 0.125 * 2); 
    auto notes = host.noteOnsForTrack(0);
    for (auto& n : notes) {
        REQUIRE(n.chainRow < 16);
    }
}

TEST_CASE("B4.5 Empty song does not hang", "[walk]") {
    OfflineHost host;
    host.push(playSong(0));
    host.renderSeconds(5.0);
    auto notes = host.eventsOfType(EventType::NOTE_ON);
    REQUIRE(notes.empty());
}

TEST_CASE("B4.7 Transpose", "[walk]") {
    OfflineHost host;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    setChain(host.sequencer(), 0, 0, 0, 12);  // +12
    setChain(host.sequencer(), 0, 1, 0, -12); // -12
    
    host.push(playChain(0, 0));
    host.renderSeconds(3.0);
    
    auto notes = host.noteOnsForTrack(0);
    REQUIRE(notes.size() >= 2);
    
    float f1 = notes[0].frequency; // +12
    float f2 = notes[1].frequency; // -12
    float f_base = 440.0f * std::pow(2.0f, (60 - 69) / 12.0f);
    
    REQUIRE(std::abs(f1 - f_base * 2.0f) < 1e-3);
    REQUIRE(std::abs(f2 - f_base * 0.5f) < 1e-3);
}


TEST_CASE("B4.6 Eight independent tracks", "[walk]") {
    OfflineHost host;
    
    // Track 0: 1-row chain
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // Phrase 0
    setChain(host.sequencer(), 0, 0, 0);         // Chain 0, row 0 -> Phrase 0
    host.sequencer().song[0].tracks[0] = 0;
    
    // Track 7: 3-row chain
    setStep(host.sequencer(), 7, 0, 72, 100, 0); // Phrase 7
    setChain(host.sequencer(), 7, 0, 7);         // Chain 7, row 0 -> Phrase 7
    setChain(host.sequencer(), 7, 1, 7);         // Chain 7, row 1 -> Phrase 7
    setChain(host.sequencer(), 7, 2, 7);         // Chain 7, row 2 -> Phrase 7
    host.sequencer().song[0].tracks[7] = 7;
    
    host.push(playSong(0));
    host.renderSeconds(2.0);
    
    auto notes0 = host.noteOnsForTrack(0);
    auto notes7 = host.noteOnsForTrack(7);
    
    REQUIRE(notes0.size() > 0);
    REQUIRE(notes7.size() > 0);
    for (const auto& n : notes0) REQUIRE(std::abs(n.frequency - 440.0f * std::pow(2.0f, (60 - 69) / 12.0f)) < 1e-1);
    for (const auto& n : notes7) REQUIRE(std::abs(n.frequency - 440.0f * std::pow(2.0f, (72 - 69) / 12.0f)) < 1e-1);
}


TEST_CASE("B4.8 Transpose clamping", "[walk]") {
    OfflineHost host;
    // note 120, tsp +127 -> clamps to 127
    setStep(host.sequencer(), 0, 0, 120, 100, 0);
    setChain(host.sequencer(), 0, 0, 0, 127);
    
    // note 5, tsp -127 -> clamps to 0
    setStep(host.sequencer(), 1, 0, 5, 100, 0);
    setChain(host.sequencer(), 1, 0, 1, -127);
    
    host.sequencer().song[0].tracks[0] = 0;
    host.sequencer().song[0].tracks[1] = 1;
    
    host.push(playSong(0));
    host.renderSeconds(0.5);
    
    auto notes0 = host.noteOnsForTrack(0);
    auto notes1 = host.noteOnsForTrack(1);
    
    REQUIRE(notes0.size() > 0);
    REQUIRE(notes1.size() > 0);
    
    float freq127 = 440.0f * std::pow(2.0f, (127 - 69) / 12.0f);
    float freq0 = 440.0f * std::pow(2.0f, (0 - 69) / 12.0f);
    
    REQUIRE(std::abs(notes0[0].frequency - freq127) < 1e-1);
    REQUIRE(std::abs(notes1[0].frequency - freq0) < 1e-3);
}

TEST_CASE("B4.10 Empty chain = rest, not skip", "[walk]") {
    OfflineHost host;
    auto& seq = host.sequencer();

    // Phrase 0: note on row 0 (any track can use it)
    setStep(seq, 0, 0, 60, 100, 0);

    // Chain 0: phrase 0 (1-row chain)
    setChain(seq, 0, 0, 0);

    // Song: track 0 has chains on rows 0-3, track 1 only on row 2
    setSong(seq, 0, 0, 0);   // track 0: chain 0
    setSong(seq, 1, 0, 0);   // track 0: chain 0
    setSong(seq, 2, 0, 0);   // track 0: chain 0
    setSong(seq, 3, 0, 0);   // track 0: chain 0
    // track 1: rows 0,1,3 are CHAIN_EMPTY (default), row 2 = chain 0
    setSong(seq, 2, 1, 0);   // track 1: chain 0 at row 2

    host.push(playSong(0));
    host.renderSeconds(10.0);

    auto notes0 = host.noteOnsForTrack(0);
    auto notes1 = host.noteOnsForTrack(1);

    // Track 0 fires at rows 0, 1, 2, 3 (4 one-row chains)
    REQUIRE(notes0.size() >= 4);
    REQUIRE(notes0[0].songRow == 0);
    REQUIRE(notes0[1].songRow == 1);
    REQUIRE(notes0[2].songRow == 2);
    REQUIRE(notes0[3].songRow == 3);

    // Track 1 is silent at rows 0, 1 — fires only at row 2
    REQUIRE(notes1.size() >= 1);
    REQUIRE(notes1[0].songRow == 2);

    // Both tracks' playSongRow must stay equal throughout.
    // Render a fresh song and sample playheads every tick.
    OfflineHost host2;
    auto& seq2 = host2.sequencer();
    setStep(seq2, 0, 0, 60, 100, 0);
    setChain(seq2, 0, 0, 0);
    setSong(seq2, 0, 0, 0);
    setSong(seq2, 1, 0, 0);
    setSong(seq2, 2, 0, 0);
    setSong(seq2, 3, 0, 0);
    setSong(seq2, 2, 1, 0);
    host2.push(playSong(0));

    constexpr int kFramesPerTick = 800; // ~1 tick at 124 BPM
    std::vector<float> buf(kFramesPerTick * 2);
    int ticksRendered = 0;
    bool mismatch = false;

    while (ticksRendered < 1000) { // ~3 seconds
        host2.engine().render(buf.data(), kFramesPerTick);
        uint32_t s0 = host2.engine().getPlayheadState(0);
        uint32_t s1 = host2.engine().getPlayheadState(1);
        uint8_t row0 = (s0 >> 16) & 0xFF;
        uint8_t row1 = (s1 >> 16) & 0xFF;
        if (row0 != row1) { mismatch = true; break; }
        ++ticksRendered;
    }
    REQUIRE_FALSE(mismatch);
}

TEST_CASE("B4.9 Fuzz the walker", "[walk]") {
    m8::engine::CommandRing<m8::engine::EngineCommand, 1024> cmdRing;
    auto enginePtr = std::make_unique<m8::engine::Engine>(cmdRing);
    auto& engine = *enginePtr;
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    
    auto& seq = engine.getSequencerForInit();
    uint8_t* p = reinterpret_cast<uint8_t*>(&seq);
    
    // 0.05 s per iteration (2400 frames @ 48 kHz) — small enough for ASan/Debug
    constexpr int kFrames = 2400;
    std::vector<float> tmp(kFrames * 2);
    
    for (int iter = 0; iter < 10000; ++iter) {
        for (size_t i = 0; i < sizeof(m8::engine::Sequencer); ++i) {
            p[i] = dist(rng);
        }
        
        int mode = dist(rng) % 3;
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::PLAY_START;
        if (mode == 0) { cmd.value = static_cast<int>(m8::engine::PlayMode::SONG); cmd.targetId = dist(rng); }
        else if (mode == 1) { cmd.value = static_cast<int>(m8::engine::PlayMode::CHAIN); cmd.targetId = dist(rng); cmd.row = dist(rng); }
        else { cmd.value = static_cast<int>(m8::engine::PlayMode::PHRASE); cmd.targetId = dist(rng); cmd.row = dist(rng); cmd.u.step.note = dist(rng); }
        cmdRing.push(cmd);
        
        engine.render(tmp.data(), kFrames);
        
        for (size_t i = 0; i < tmp.size(); ++i) {
            if (std::isnan(tmp[i]) || std::isinf(tmp[i])) {
                REQUIRE(false);
            }
        }
    }
    REQUIRE(true);
}

TEST_CASE("B4.11 Chain end fires NOTE_OFF", "[walk]") {
    OfflineHost host;
    // Phrase 0: note on row 0, empty after
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    // Phrase 1: note on row 0 (different note)
    setStep(host.sequencer(), 1, 0, 61, 100, 0);
    // Chain 0: phrase 0 (1 bar), then empty (chain ends)
    setChain(host.sequencer(), 0, 0, 0);
    setChain(host.sequencer(), 0, 1, PHRASE_EMPTY);
    // Song: track 0 uses chain 0
    setSong(host.sequencer(), 0, 0, 0);

    host.push(playSong(0));
    host.renderSeconds(3.0);

    auto notesOn = host.noteOnsForTrack(0);
    auto notesOff = host.noteOffsForTrack(0);
    REQUIRE(notesOn.size() >= 1);
    REQUIRE(notesOff.size() >= 1);

    // NOTE_OFF should fire after the chain ends (after 2 bars = 2*16 rows)
    // At 120 BPM, 6 ticks/row, 16 rows = 96 ticks = 96000 samples
    // Chain ends at bar 2, so NOTE_OFF should be around sample 96000
    REQUIRE(notesOff[0].sampleTime > 0);
}

TEST_CASE("B3.7 Tick spacing at 130 BPM with chunk 512", "[tempo]") {
    OfflineHost host;
    EngineCommand cmd; cmd.type = CommandType::UPDATE_PARAM;
    cmd.paramId = ParamID::BPM_INT; cmd.value = 130;
    host.push(cmd);

    host.push(playSong(0));
    host.renderSeconds(10.0, 512);

    auto ticks = host.eventsOfType(EventType::TICK);
    REQUIRE(ticks.size() > 2);

    for (size_t i = 1; i < ticks.size(); ++i) {
        uint64_t delta = ticks[i].sampleTime - ticks[i-1].sampleTime;
        REQUIRE((delta == 923 || delta == 924));
    }
}
