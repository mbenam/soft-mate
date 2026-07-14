#include "SamplerEngine.h"
#include "Engine.h"

namespace m8::engine {

void SamplerEngine::noteOn(const SamplerState& s, const SampleData* d) {
    m_data = d;
    computeRegion(s);
}

void SamplerEngine::computeRegion(const SamplerState& s) {
    if (!m_data || m_data->frames == 0) { m_finished = true; return; }
    const int32_t frames = int32_t(m_data->frames);
    m_startFrame = int32_t((s.start   / 255.0) * frames);
    m_loopStart  = int32_t((s.loop_st / 255.0) * frames);
    const int32_t len = int32_t((s.length / 255.0) * frames);
    m_loopEnd    = m_loopStart + len;
    m_startFrame = clampi(m_startFrame, 0, frames - 1);
    m_loopStart  = clampi(m_loopStart,  0, frames - 1);
    m_loopEnd    = clampi(m_loopEnd, m_loopStart + 1, frames);
    m_mode = static_cast<SamplePlayMode>(s.play);
    if (isOscMode(m_mode)) m_startFrame = m_loopStart;
    m_phase = double(m_startFrame);
    m_reverse = isReverseMode(m_mode);
    if (m_reverse) m_phase = double(m_loopEnd - 1);
    m_finished = false;
}

void SamplerEngine::setLoop(int32_t loopStart, int32_t loopEnd) {
    if (!m_data) return;
    const int32_t frames = int32_t(m_data->frames);
    m_loopStart = clampi(loopStart, 0, frames - 1);
    m_loopEnd = clampi(loopEnd, m_loopStart + 1, frames);
}

void SamplerEngine::readFrame(double phase, float out[2]) const {
    if (!m_data || !m_data->data || m_data->frames == 0) { out[0] = out[1] = 0.0f; return; }
    const int32_t frames = int32_t(m_data->frames);
    const int32_t i0 = clampi(int32_t(phase), 0, frames - 1);
    const int32_t i1 = clampi(i0 + 1, 0, frames - 1);
    const float frac = float(phase - double(i0));
    const int ch = m_data->channels;
    const float* d = m_data->data;
    if (ch >= 2) {
        out[0] = lerp(d[i0 * ch + 0], d[i1 * ch + 0], frac);
        out[1] = lerp(d[i0 * ch + 1], d[i1 * ch + 1], frac);
    } else {
        out[0] = out[1] = lerp(d[i0], d[i1], frac);
    }
}

void SamplerEngine::render(float ratio, float out[2]) {
    out[0] = out[1] = 0.0f;
    if (!m_data || m_finished || m_loopEnd <= m_loopStart) return;
    readFrame(m_phase, out);
    const double dir = m_reverse ? -1.0 : 1.0;
    m_phase += double(ratio) * dir;
    const bool looping  = isLoopMode(m_mode);
    const bool pingpong = isPingPongMode(m_mode);
    if (m_phase >= m_loopEnd) {
        if (!looping) m_finished = true;
        else if (pingpong) { m_reverse = true; m_phase = m_loopEnd - (m_phase - m_loopEnd); }
        else m_phase = m_loopStart + (m_phase - m_loopEnd);
    } else if (m_phase < m_loopStart) {
        if (!looping) m_finished = true;
        else if (pingpong) { m_reverse = false; m_phase = m_loopStart + (m_loopStart - m_phase); }
        else m_phase = m_loopEnd - (m_loopStart - m_phase);
    }
    m_phase = clampd(m_phase, m_loopStart, m_loopEnd - 1);
}

} // namespace m8::engine
