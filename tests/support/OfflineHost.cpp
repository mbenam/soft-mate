#include "OfflineHost.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iostream>

extern std::atomic<bool> g_inAudioThread;

namespace m8::test {

OfflineHost::OfflineHost() {
    m_engine = std::make_unique<engine::Engine>(m_ring);
    m_engine->getSequencerForInit().clear();
}

void OfflineHost::render(int frames, int chunk) {
    int framesDone = 0;
    while (framesDone < frames) {
        int toRender = std::min(chunk, frames - framesDone);
        
        std::vector<float> tmp(toRender * 2);
        
        bool wasAudio = g_inAudioThread.load();
        g_inAudioThread = true;
        m_engine->render(tmp.data(), toRender);
        g_inAudioThread = wasAudio;
        
        for (int i = 0; i < toRender * 2; ++i) {
            m_audio.push_back(tmp[i]);
        }
        
        bool bad = false;
        for (int i = 0; i < toRender * 2; ++i) {
            float val = tmp[i];
            if (!std::isfinite(val) || std::abs(val) > 1.0f) { bad = true; break; }
        }
        REQUIRE_FALSE(bad);
        
        engine::EngineEvent ev;
        while (m_engine->getEventRing().pop(ev)) {
            m_events.push_back(ev);
        }
        
        framesDone += toRender;
    }
}

bool OfflineHost::push(const engine::EngineCommand& cmd) {
    return m_ring.push(cmd);
}

std::vector<engine::EngineEvent> OfflineHost::eventsOfType(engine::EventType t) const {
    std::vector<engine::EngineEvent> res;
    for (const auto& ev : m_events) {
        if (ev.type == t) res.push_back(ev);
    }
    return res;
}

std::vector<engine::EngineEvent> OfflineHost::noteOnsForTrack(int track) const {
    std::vector<engine::EngineEvent> res;
    for (const auto& ev : m_events) {
        if (ev.type == engine::EventType::NOTE_ON && ev.track == track) res.push_back(ev);
    }
    return res;
}

std::vector<engine::EngineEvent> OfflineHost::noteOffsForTrack(int track) const {
    std::vector<engine::EngineEvent> res;
    for (const auto& ev : m_events) {
        if (ev.type == engine::EventType::NOTE_OFF && ev.track == track) res.push_back(ev);
    }
    return res;
}

void setStep(engine::Sequencer& seq, int phrase, int row, uint8_t note, uint8_t vol, uint8_t inst) {
    seq.phrases[phrase][row].note = note;
    seq.phrases[phrase][row].vol = vol;
    seq.phrases[phrase][row].instr = inst;
}

void setFx(engine::Sequencer& seq, int phrase, int row, int slot, engine::FxCmd cmd, uint8_t val) {
    seq.phrases[phrase][row].fx[slot] = {cmd, val};
}

void setChain(engine::Sequencer& seq, int chain, int row, uint8_t phrase, int8_t tsp) {
    seq.chains[chain][row].phrase = phrase;
    seq.chains[chain][row].tsp = tsp;
}

void setSong(engine::Sequencer& seq, int songRow, int track, uint8_t chain) {
    seq.song[songRow].tracks[track] = chain;
}

engine::EngineCommand playSong(int startRow) {
    engine::EngineCommand cmd;
    cmd.type = engine::CommandType::PLAY_START;
    cmd.targetId = startRow;
    cmd.row = 0;
    cmd.value = 3;
    return cmd;
}

engine::EngineCommand playChain(int chainId, int track) {
    engine::EngineCommand cmd;
    cmd.type = engine::CommandType::PLAY_START;
    cmd.targetId = chainId;
    cmd.row = 0;
    cmd.value = 2;
    cmd.col = track;
    return cmd;
}

engine::EngineCommand playPhrase(int phraseId, int row, int track) {
    engine::EngineCommand cmd;
    cmd.type = engine::CommandType::PLAY_START;
    cmd.targetId = phraseId;
    cmd.row = row;
    cmd.col = track;
    cmd.value = 1;
    return cmd;
}

engine::EngineCommand stop() {
    engine::EngineCommand cmd;
    cmd.type = engine::CommandType::PLAY_STOP;
    return cmd;
}

} // namespace m8::test
