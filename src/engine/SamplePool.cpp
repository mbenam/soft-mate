#include "SamplePool.h"
#include <cstring>

namespace m8::engine {

const SampleData* SamplePool::get(SampleHandle h) const {
    if (h >= 0 && h < MAX && m_slots[h].data) return &m_slots[h];
    return nullptr;
}

SampleHandle SamplePool::find(const char* path) const {
    if (!path) return -1;
    for (int i = 0; i < MAX; ++i) {
        if (m_slots[i].data && std::strncmp(m_slots[i].path, path, 128) == 0)
            return SampleHandle(i);
    }
    return -1;
}

SampleHandle SamplePool::install(SampleData d) {
    for (int i = 0; i < MAX; ++i) {
        if (!m_slots[i].data) {
            m_slots[i] = d;
            m_slots[i].refs = 1;
            return SampleHandle(i);
        }
    }
    return -1;
}

void SamplePool::addRef(SampleHandle h) {
    if (h >= 0 && h < MAX && m_slots[h].data) {
        ++m_slots[h].refs;
    }
}

SampleData SamplePool::release(SampleHandle h) {
    if (h >= 0 && h < MAX && m_slots[h].data) {
        if (--m_slots[h].refs == 0) {
            SampleData out = m_slots[h];
            m_slots[h] = SampleData{};
            return out;
        }
    }
    return SampleData{};
}

} // namespace m8::engine
