#pragma once
#include <cstdint>
#include <type_traits>

namespace m8::engine {

enum class EventType : uint8_t { NOTE_ON, NOTE_OFF, ROW_ADVANCE, TICK };

struct EngineEvent {
    EventType type;
    uint8_t   track;
    uint8_t   phraseRow;
    uint8_t   chainRow;
    uint8_t   songRow;
    uint8_t   instrument;
    uint8_t   _pad[2];
    uint64_t  sampleTime;   // absolute frame index since engine construction
    float     frequency;
    float     volume;
};
static_assert(sizeof(EngineEvent) == 24);
static_assert(std::is_trivially_copyable_v<EngineEvent>);

} // namespace m8::engine
