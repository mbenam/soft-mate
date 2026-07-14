#pragma once
#include <cstdint>
#include <cstring>

namespace m8::engine {

struct SampleData {
    float*   data = nullptr;      // interleaved, owned by the pool
    uint32_t frames = 0;
    uint8_t  channels = 1;
    uint32_t sampleRate = 44100;
    uint16_t refs = 0;            // how many instruments point at this slot
    char     path[128] = {};      // key; POD, so it can cross the ring
};

using SampleHandle = int16_t;     // -1 = none

class SamplePool {
public:
    static constexpr int MAX = 128;

    const SampleData* get(SampleHandle h) const;
    SampleHandle find(const char* path) const;   // audio thread: linear scan of 128 is fine

    // Audio thread: pointer swaps and refcount arithmetic only. Never allocates or frees.
    SampleHandle install(SampleData d);          // -1 if pool full
    void         addRef(SampleHandle h);
    SampleData   release(SampleHandle h);        // returns the buffer when refs hits 0, else {}

private:
    SampleData m_slots[MAX]{};
};

} // namespace m8::engine
