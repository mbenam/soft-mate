#pragma once
#include "engine/Engine.h"
#include "engine/EngineEvents.h"
#include <memory>
#include <vector>

namespace m8::test {

class OfflineHost {
public:
    static constexpr double kSampleRate = 48000.0;
    
    OfflineHost();

    const engine::EngineState& state() const { return m_engine->getStateForInit(); }
    
    void forcePublishPlayhead(int t) { m_engine->publishPlayhead(t); }

    engine::Engine&    engine()    { return *m_engine; }
    engine::Sequencer& sequencer() { return m_engine->getSequencerForInit(); }

    const engine::SamplePool& pool() const { return m_engine->m_samplePool; }

    // Render `frames` in chunks of `chunk`. Drains the event ring after every chunk.
    // Asserts on every chunk: no NaN, no Inf, |sample| <= 1.0
    void render(int frames, int chunk = 512);
    void renderSeconds(double s, int chunk = 512) {
        render(static_cast<int>(s * kSampleRate), chunk);
    }

    bool push(const engine::EngineCommand& cmd);

    const std::vector<engine::EngineEvent>& events() const { return m_events; }
    const std::vector<float>&               audio()  const { return m_audio; }  // interleaved stereo

    std::vector<engine::EngineEvent> eventsOfType(engine::EventType t) const;
    std::vector<engine::EngineEvent> noteOnsForTrack(int track) const;
    std::vector<engine::EngineEvent> noteOffsForTrack(int track) const;
    void clearEvents() { m_events.clear(); }

private:
    engine::CommandRing<engine::EngineCommand, 1024> m_ring;
    std::unique_ptr<engine::Engine> m_engine;
    std::vector<engine::EngineEvent> m_events;
    std::vector<float> m_audio;
};

// Fixture helpers used across tests
void setStep (engine::Sequencer& seq, int phrase, int row, uint8_t note, uint8_t vol, uint8_t inst);
void setFx   (engine::Sequencer& seq, int phrase, int row, int slot, engine::FxCmd cmd, uint8_t val);
void setChain(engine::Sequencer& seq, int chain,  int row, uint8_t phrase, int8_t tsp = 0);
void setSong (engine::Sequencer& seq, int songRow, int track, uint8_t chain);

engine::EngineCommand playSong  (int startRow);
engine::EngineCommand playChain (int chainId, int track);
engine::EngineCommand playPhrase(int phraseId, int row, int track);
engine::EngineCommand stop();

} // namespace m8::test
