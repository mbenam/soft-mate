/*
Copyright (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor

Use of this source code is governed by the LGPL V2.1
license that can be found in the LICENSE file or at
https://opensource.org/license/lgpl-2-1/
*/

// -----------------------------------------------------------------------------
// MODIFIED COPY of DaisySP-LGPL's ReverbSc (LGPL 2.1 §2a notice).
//
// Vendored from DaisySP-LGPL/Source/Effects/reverbsc.{h,cpp} and changed by the
// soft-mate project on 2026-08-14. The original remains available from
// Electrosmith; this file and ReverbScM8.cpp are the modified versions, and the
// LGPL terms above continue to govern them.
//
// WHY: the M8's reverb exposes ROOM SIZE, MOD DEPTH and MOD FREQ. All three
// exist inside this algorithm -- they are columns of the `kReverbParams` table
// -- but upstream they are compile-time constants with no accessor, so the
// stock class offers only SetFeedback and SetLpFreq. Reaching them requires
// modifying the source, which requires vendoring it.
//
// CHANGES FROM UPSTREAM, in full:
//   1. Renamed to ReverbScM8 in namespace m8::engine, so it cannot collide with
//      the stock daisysp::ReverbSc that DaisySP still ships.
//   2. Added SetRoomSize / SetPitchMod / SetModRate, and the three members
//      behind them.
//   3. Delay-line delay times are multiplied by room_size_, and the random
//      line-segment rate is divided by mod_rate_scale_. Both take effect at the
//      next line segment, so the existing interpolation glides to the new delay
//      instead of clicking -- no re-Init, no reset of the tail.
//   4. i_pitch_mod_ is now settable rather than pinned to 1, and the delay
//      buffers are sized for kMaxPitchMod so raising it cannot read outside the
//      allocated region.
//   5. Fixed a pre-existing allocation bug: Init advanced its offset into the
//      float array `aux_` by a BYTE count (`samples * sizeof(float)`), spacing
//      the eight lines four times further apart than needed and very nearly
//      exhausting DSY_REVERBSC_MAX_SIZE. Now advanced by the sample count. This
//      changes no audio -- the lines never overlapped either way -- it just
//      recovers roughly 74k floats, which is what makes headroom for (4)
//      available at all.
// -----------------------------------------------------------------------------

#pragma once
#ifndef M8_REVERBSC_H
#define M8_REVERBSC_H

#define M8_REVERBSC_MAX_SIZE 98936

namespace m8 {
namespace engine {

/** Delay line for internal reverb use */
typedef struct
{
    int    write_pos;         /**< write position */
    int    buffer_size;       /**< buffer size */
    int    read_pos;          /**< read position */
    int    read_pos_frac;     /**< fractional component of read pos */
    int    read_pos_frac_inc; /**< increment for fractional */
    int    dummy;             /**<  dummy var */
    int    seed_val;          /**< randseed */
    int    rand_line_cnt;     /**< number of random lines */
    float  filter_state;      /**< state of filter */
    float *buf;               /**< buffer ptr */
} ReverbScDl;

/** Stereo Reverb */
class ReverbScM8
{
  public:
    // The largest modulation depth the buffers are sized for. Asking for more
    // than this is clamped rather than allowed to read past the allocation.
    static constexpr float kMaxPitchMod = 4.0f;

    ReverbScM8() {}
    ~ReverbScM8() {}

    int Init(float sample_rate);
    int Process(const float &in1, const float &in2, float *out1, float *out2);

    /** reverb time; tail becomes infinite at 1.0 */
    inline void SetFeedback(const float &fb) { feedback_ = fb; }
    /** internal dampening filter cutoff, 0 .. sample_rate/2 */
    inline void SetLpFreq(const float &freq) { lpfreq_ = freq; }

    /** ROOM SIZE, as a multiplier on every delay line's length. 1.0 is the
        stock tuning and the buffers are sized for it, so values above 1 are
        clamped. Takes effect at each line's next random segment, which glides
        rather than clicks. */
    inline void SetRoomSize(float scale)
    {
        room_size_ = scale < 0.05f ? 0.05f : (scale > 1.0f ? 1.0f : scale);
    }

    /** MOD DEPTH, as a multiplier on the random delay-time deviation. 1.0 is
        the stock tuning; 0 freezes the delay lines. */
    inline void SetPitchMod(float amount)
    {
        i_pitch_mod_ = amount < 0.0f ? 0.0f
                                     : (amount > kMaxPitchMod ? kMaxPitchMod : amount);
    }

    /** MOD FREQ, as a multiplier on how often each line picks a new random
        target. 1.0 is the stock tuning; lower is slower drift. */
    inline void SetModRate(float scale)
    {
        mod_rate_scale_ = scale < 0.01f ? 0.01f : (scale > 4.0f ? 4.0f : scale);
    }

  private:
    void       NextRandomLineseg(ReverbScDl *lp, int n);
    int        InitDelayLine(ReverbScDl *lp, int n);
    float      feedback_, lpfreq_;
    float      i_sample_rate_, i_pitch_mod_, i_skip_init_;
    float      sample_rate_;
    float      damp_fact_;
    float      prv_lpfreq_;
    int        init_done_;
    // soft-mate additions
    float      room_size_      = 1.0f;
    float      mod_rate_scale_ = 1.0f;
    ReverbScDl delay_lines_[8];
    float      aux_[M8_REVERBSC_MAX_SIZE];
};

} // namespace engine
} // namespace m8
#endif
