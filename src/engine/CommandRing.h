#pragma once
#include <type_traits>

#include <atomic>
#include <cstdint>
#include "SeqTypes.h"
#include "SamplePool.h"

namespace m8 {
namespace engine {



enum class CommandType {
    NONE,
    PLAY_START,
    PLAY_STOP,
    LOAD_SAMPLE,
    UPDATE_PARAM,   // <-- NEW COMMAND TYPE
    SET_STEP,
    SET_CHAIN_STEP,
    SET_SONG_STEP,
    SET_GROOVE_STEP
};

enum class ParamID {
    // Project
    BPM_INT, BPM_FRAC,
    
    // Mixer
    MIX_OUT_VOL, MIX_TRK_VOL, MIX_CHO_VOL, MIX_DEL_VOL, MIX_REV_VOL,
    MIX_IN_VOL, MIX_IN_CHO, MIX_IN_DEL, MIX_IN_REV,
    MIX_USB_VOL, MIX_USB_CHO, MIX_USB_DEL, MIX_USB_REV,
    MIX_MIX_VOL, MIX_LIM_VAL, MIX_DJF_FREQ, MIX_DJF_RES, MIX_DJF_TYP,
    
    // Effects
    FX_CHO_MOD_DEPTH, FX_CHO_MOD_FREQ, FX_CHO_WIDTH, FX_CHO_REVERB,
    FX_DEL_TIME_L, FX_DEL_TIME_R, FX_DEL_FEEDBACK, FX_DEL_WIDTH, FX_DEL_REVERB,
    FX_REV_SIZE, FX_REV_DECAY, FX_REV_MOD_DEPTH, FX_REV_MOD_FREQ, FX_REV_WIDTH,

    // Instrument
    INST_TYPE, INST_TRANSP, INST_TBL_TIC, INST_EQ, INST_AMP, INST_LIM, INST_PAN,
    INST_DRY, INST_CHO, INST_DEL, INST_REV, INST_DEGRADE, INST_FILTER, INST_CUTOFF, INST_RES,
    
    // Sampler Specific
    SAMP_PLAY, SAMP_START, SAMP_LOOP_ST, SAMP_LENGTH, SAMP_DETUNE, SAMP_SLICE,
    
    // Macrosyn Specific
    MAC_SHAPE, MAC_TIMBRE, MAC_COLOR, MAC_REDUX,

    // Modulators
    MOD_TYPE, MOD_DEST, MOD_AMT, MOD_P1, MOD_P2, MOD_P3, MOD_P4,

    // Scale
    SCALE_KEY, SCALE_TUNE, SCALE_NOTE_EN, SCALE_NOTE_OFFSET
};

struct EngineCommand {
    CommandType type = CommandType::NONE;
    ParamID paramId;        // Which parameter to update
    int targetId = 0;       // Instrument ID, Scale ID, etc.
    int row = 0;            // Track ID, Modulator ID, Note ID, etc.
    int col = 0;
    int value = 0;          // Integer payload
    float fValue = 0.0f;    // Float payload (for tuning/offsets)
    union {
        Step step;
        ChainStep chainStep;
        SampleData sample;
    } u{};
};
static_assert(std::is_trivially_copyable_v<EngineCommand>);

template <typename T, size_t Capacity>
class CommandRing {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    
    CommandRing() : head(0), tail(0) {}

    bool push(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & (Capacity - 1);
        if (next_tail == head.load(std::memory_order_acquire)) return false; 
        buffer[current_tail] = item;
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) return false; 
        item = buffer[current_head];
        head.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    T buffer[Capacity];
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};

} // namespace engine
} // namespace m8
