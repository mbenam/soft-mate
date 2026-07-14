#pragma once

#include "CommandRing.h"
#include "Sequencer.h"
#include "EngineEvents.h"
#include "SynthVoice.h"
#include "SamplePool.h"
#include "Envelopes.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace m8 {
namespace engine {

enum class InstType { INST_SAMPLER, INST_MACROSYN, INST_MIDI, INST_NONE };

enum class ModType { 
    AHD_ENV = 0, ADSR_ENV, DRUM_ENV, LFO, TRIG_ENV, TRACKING 
};

struct Modulator {
    int type = 0;   // 0=AHD, 1=ADSR, 2=DRUM, 3=LFO, 4=TRIG, 5=TRACK
    int dest = 0;   // 0=OFF, 1=VOLUME, 2=PITCH, 3=CUTOFF, etc.
    int amt = 0xFF; // 0x00 to 0xFF
    
    // Generic polymorphic parameter slots
    int p1 = 0x00;  // ATK, PEAK, OSC, SRC
    int p2 = 0x00;  // HOLD, DEC, BODY, TRIG, LVAL
    int p3 = 0x80;  // DEC, SUS, FREQ, HVAL
    int p4 = 0x80;  // REL, SRC
};

struct SamplerState {
    SampleHandle sample = -1;
    char samplePath[128] = {};
    int transp = 1;        // 0 = OFF, 1 = ON
    int tbl_tic = 0xFF;
    int eq = 0;            // 0 = --
    int slice = 0;
    int play = 0;          // 0 = FWD, 1 = REV, etc.
    int start = 0x00;
    int loop_st = 0x00;
    int length = 0xFF;
    int detune = 0x80;
    int degrade = 0x00;
    int filter_type = 0;   // 0 = OFF, 1 = LP, 2 = HP, 3 = BP
    int cutoff = 0xFF;
    int res = 0x00;
    int amp = 0x00;
    int lim = 0;           // 0 = CLIP, 1 = SIN, etc.
    int pan = 0x80;
    int dry = 0xC0;
    int cho = 0x00;
    int del = 0x00;
    int rev = 0x00;
};

struct MacrosynState {
    int transp = 1;        // 0 = OFF, 1 = ON
    int tbl_tic = 0xFF;
    int eq = 0;            // 0 = --
    int shape = 0;         // 0 = CSAW, 1 = TRI, etc.
    int timbre = 0x80;
    int color = 0x80;
    int degrade = 0x00;
    int redux = 0x00;
    int filter_type = 0;   // 0 = OFF, 1 = LP, 2 = HP, 3 = BP
    int cutoff = 0xFF;
    int res = 0x00;
    int amp = 0x00;
    int lim = 0;           // 0 = CLIP, 1 = SIN, etc.
    int pan = 0x80;
    int dry = 0xC0;
    int cho = 0x00;
    int del = 0x00;
    int rev = 0x00;
};

template <size_t N>
inline void setName(char (&dst)[N], const char* src) {
    size_t i = 0;
    for (; i + 1 < N && src[i]; ++i) dst[i] = src[i];
    for (; i + 1 < N; ++i) dst[i] = ' ';
    dst[N - 1] = '\0';
}

struct Playhead {
    uint8_t songRow;
    uint8_t chainRow;
    uint8_t phraseRow;
    uint8_t playMode;
    uint8_t activeCol;

    bool is(PlayMode m) const { return playMode == static_cast<uint8_t>(m); }
};
struct Instrument {
    InstType type = InstType::INST_SAMPLER;
    char name[13] = "------------";
    SamplerState sampler;
    MacrosynState macrosyn;
    Modulator mods[4];
};

struct ProjectSettings {
    char name[13] = "DEMO2-------";
    int transpose = 0;
    int groove = 0;
    int scale = 0;
    int live_quantize = 0;
};

struct ScaleNote {
    bool enable = true;
    float offset = 0.0f; // Range usually -99.99 to 99.99
};

struct Scale {
    int key = 0; // 0 = C, 1 = C#, 2 = D, etc.
    ScaleNote notes[12];
    float tune = 440.00f;
    char name[17] = "CHROMATIC-------";
};

struct MixerState {
    int out_vol = 0xE0;
    int track_vol[8] = { 0xE0, 0xE0, 0xF3, 0xD0, 0xE0, 0xBF, 0xE0, 0xE0 }; 
    
    int cho_vol = 0xE0;
    int del_vol = 0xE0;
    int rev_vol = 0xE0;
    
    int in_vol = 0x00;
    int in_cho = 0x00;
    int in_del = 0x00;
    int in_rev = 0x00;
    
    int usb_vol = 0x00;
    int usb_cho = 0x00;
    int usb_del = 0x00;
    int usb_rev = 0x00;
    
    int mix_vol = 0xDC; 
    int lim_val = 0x40; 
    int djf_freq = 0x80;
    int djf_res = 0x80;
    int djf_typ = 0x00;
};

struct EffectsState {
    int cho_mod_depth = 0x50;
    int cho_mod_freq = 0x80;
    int cho_width = 0xFF;
    int cho_reverb = 0x00;

    int del_time_l = 0x30;
    int del_time_r = 0x30;
    int del_feedback = 0x80;
    int del_width = 0xFF;
    int del_reverb = 0x00;

    int rev_size = 0xFF;
    int rev_decay = 0xC0;
    int rev_mod_depth = 0x20;
    int rev_mod_freq = 0xFF;
    int rev_width = 0xFF;
};

struct EngineState {
    int bpm = 120;
    int bpm_frac = 0;
    
    PlayMode playMode = PlayMode::NONE;
    int playSongRow[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int playChainRow[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int playPhraseRow[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    
    // Specifically for phrase/chain playback contexts
    int currentPhrase = 0;
    int currentChain = 0;
    int activeCol = 0;

    int playTick[8] = {0};
    int playGrooveIndex[8] = {0};
    float pendingFreq[8] = {0};
    float pendingVol[8] = {0};
    bool pendingVolValid[8] = {false};
    const Instrument* pendingInst[8] = {nullptr};
    int pendingDel[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int pendingKil[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int nextHop[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    std::vector<Instrument> instruments;
    ProjectSettings project;
    MixerState mixer;
    EffectsState effects;
    Scale scales[16];

    EngineState() {
        instruments.resize(128);
    }
};

} // namespace engine
namespace test { class OfflineHost; }
namespace engine {

class Engine {
    friend class m8::test::OfflineHost;
public:
    Sequencer&   getSequencerForInit() { return m_sequencer; }
    EngineState& getStateForInit()     { return m_state; }
    CommandRing<EngineEvent, 1024>& getEventRing() { return m_eventRing; }
    
    // Call this explicitly before SDL audio starts
    void loadDemoSong() {
        m_sequencer.loadDemoSong();
        setupDemoSamples();
        setupDemoInstruments();
        m_state.bpm = 124;
        m_state.project.groove = 1;          // the swing groove
        setName(m_state.project.name, "NIGHTDRIVE");
        recalcBPM();
    }

    Engine(CommandRing<EngineCommand, 1024>& commandRing);
    CommandRing<SampleData, 64>& getGcRing() { return m_gcRing; }
    ~Engine() = default;

    // Called by the audio thread driver
    void render(float* buffer, int frames);

    const Sequencer& getSequencer() const { return m_sequencer; }
    void notifyTrigSource(uint8_t instrumentIndex);
    

    uint32_t getPlayheadState(int track) const {
        if (track < 0 || track >= 8) return 0;
        return m_playheadState[track].load(std::memory_order_acquire);
    }
    
    Playhead getPlayhead(int track) const {
        const uint32_t s = getPlayheadState(track);
        return Playhead{ 
            uint8_t((s >> 16) & 0xFF), 
            uint8_t((s >> 8) & 0xFF), 
            uint8_t(s & 0xFF),
            uint8_t((s >> 24) & 0x03),
            uint8_t((s >> 26) & 0x07)
        };
    }

private:
    // -----------------------------------------------------------------------
    // Generated drum samples. Runs before the audio thread starts, so touching
    // the pool directly is safe. These are deliberately crude — they exist so
    // the app makes noise without shipping WAVs, and they go away once the
    // persistence layer can load real samples.
    // -----------------------------------------------------------------------
    void setupDemoSamples() {
        auto install = [&](const char* name, float seconds,
                           float (*gen)(float t, float dur)) -> SampleHandle {
            const int frames = static_cast<int>(kSampleRate * seconds);
            float* buf = new float[frames];
            for (int i = 0; i < frames; ++i) {
                const float t = static_cast<float>(i) / kSampleRate;
                buf[i] = gen(t, seconds);
            }
            SampleData sd{};
            sd.data       = buf;
            sd.frames     = static_cast<uint32_t>(frames);
            sd.channels   = 1;
            sd.sampleRate = static_cast<uint32_t>(kSampleRate);
            std::strncpy(sd.path, name, sizeof(sd.path) - 1);
            return m_samplePool.install(sd);
        };

        // --- KICK: pitch-swept sine with a click transient -------------------
        m_demoKick = install("demo_kick.wav", 0.28f, [](float t, float) {
            const float pitch = 55.0f + 160.0f * std::exp(-t * 32.0f);
            const float amp   = std::exp(-t * 9.0f);
            const float click = std::exp(-t * 400.0f) * 0.35f;
            float s = amp * std::sin(6.2831853f * pitch * t) + click;
            return std::tanh(s * 1.6f);            // a bit of drive
        });

        // --- SNARE: tone + noise ---------------------------------------------
        m_demoSnare = install("demo_snare.wav", 0.22f, [](float t, float) {
            uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 2654435761u;
            s ^= s >> 13;
            const float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
            const float body  = std::exp(-t * 30.0f) * std::sin(6.2831853f * 190.0f * t);
            const float crack = std::exp(-t * 18.0f) * n;
            return std::tanh((body * 0.6f + crack * 0.9f) * 1.3f);
        });

        // --- HAT: short bright noise ------------------------------------------
        m_demoHat = install("demo_hat.wav", 0.09f, [](float t, float) {
            uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 2246822519u;
            s ^= s >> 11;
            float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
            // crude high-pass: difference of successive noise values
            uint32_t s2 = (static_cast<uint32_t>(t * 48000.0f) - 1) * 2246822519u;
            s2 ^= s2 >> 11;
            const float n2 = (static_cast<float>(s2 >> 8) / 8388608.0f) - 1.0f;
            n = (n - n2) * 0.5f;
            return n * std::exp(-t * 60.0f);
        });

        // --- CLAP: three noise bursts then a tail -----------------------------
        m_demoClap = install("demo_clap.wav", 0.30f, [](float t, float) {
            uint32_t s = static_cast<uint32_t>(t * 48000.0f) * 3266489917u;
            s ^= s >> 15;
            const float n = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
            float env = 0.0f;
            const float bursts[3] = { 0.000f, 0.011f, 0.022f };
            for (float b : bursts)
                if (t >= b) env = std::max(env, std::exp(-(t - b) * 260.0f));
            env = std::max(env, 0.55f * std::exp(-(t - 0.022f) * 22.0f) * (t > 0.022f ? 1.0f : 0.0f));
            return n * env * 0.9f;
        });
    }

    // -----------------------------------------------------------------------
    // Instruments. Mod slot 0 is always the amp envelope (AHD -> VOLUME), which
    // is how the hardware does it. Times are in TICKS, so they follow tempo.
    // -----------------------------------------------------------------------
    void setupDemoInstruments() {
        auto amp = [&](int i, int atk, int hold, int dec) {
            auto& m = m_state.instruments[i].mods[0];
            m.type = 0;                       // AHD ENV
            m.dest = 1;                       // VOLUME
            m.amt  = 0xFF;                    // full positive (bipolar, 0x80 = neutral)
            m.p1 = atk; m.p2 = hold; m.p3 = dec;
        };
        auto mod = [&](int i, int slot, int type, int dest, int amt,
                       int p1, int p2, int p3) {
            auto& m = m_state.instruments[i].mods[slot];
            m.type = type; m.dest = dest; m.amt = amt;
            m.p1 = p1; m.p2 = p2; m.p3 = p3;
        };

        // --- 00 KICK -------------------------------------------------------
        {
            auto& in = m_state.instruments[0];
            in.type = InstType::INST_SAMPLER;
            setName(in.name, "KICK");
            in.sampler.sample      = m_demoKick;
            std::strncpy(in.sampler.samplePath, "demo_kick.wav",
                         sizeof(in.sampler.samplePath) - 1);
            in.sampler.play        = 0;       // FWD one-shot
            in.sampler.filter_type = 1;       // LP
            in.sampler.cutoff      = 0x60;
            in.sampler.res         = 0x10;
            in.sampler.amp         = 0x30;    // drive
            in.sampler.lim         = 1;       // SIN — soft saturation
            in.sampler.dry         = 0xF0;
            in.sampler.rev         = 0x08;
            amp(0, 0, 0x40, 0x20);            // long enough not to gate the sample
        }

        // --- 01 SNARE ------------------------------------------------------
        {
            auto& in = m_state.instruments[1];
            in.type = InstType::INST_SAMPLER;
            setName(in.name, "SNARE");
            in.sampler.sample      = m_demoSnare;
            std::strncpy(in.sampler.samplePath, "demo_snare.wav",
                         sizeof(in.sampler.samplePath) - 1);
            in.sampler.play        = 0;
            in.sampler.filter_type = 2;       // HP — get it out of the kick's way
            in.sampler.cutoff      = 0x30;
            in.sampler.amp         = 0x20;
            in.sampler.dry         = 0xD0;
            in.sampler.del         = 0x28;
            in.sampler.rev         = 0x40;
            in.sampler.pan         = 0x88;    // just off centre
            amp(1, 0, 0x30, 0x18);
        }

        // --- 02 HAT --------------------------------------------------------
        {
            auto& in = m_state.instruments[2];
            in.type = InstType::INST_SAMPLER;
            setName(in.name, "HAT");
            in.sampler.sample      = m_demoHat;
            std::strncpy(in.sampler.samplePath, "demo_hat.wav",
                         sizeof(in.sampler.samplePath) - 1);
            in.sampler.play        = 0;
            in.sampler.filter_type = 2;       // HP
            in.sampler.cutoff      = 0x50;
            in.sampler.dry         = 0xB0;
            in.sampler.rev         = 0x18;
            in.sampler.pan         = 0x70;    // opposite the snare
            amp(2, 0, 0x10, 0x08);            // short — the KIL in the phrase tightens it further
        }

        // --- 03 BASS -------------------------------------------------------
        {
            auto& in = m_state.instruments[3];
            in.type = InstType::INST_MACROSYN;
            setName(in.name, "BASS");
            in.macrosyn.filter_type = 1;      // LP
            in.macrosyn.cutoff      = 0x28;
            in.macrosyn.res         = 0x90;
            in.macrosyn.amp         = 0x28;
            in.macrosyn.lim         = 1;      // SIN
            in.macrosyn.dry         = 0xE8;
            amp(3, 0, 0x02, 0x0C);
            // Filter envelope — the thing that makes a bass line move.
            mod(3, 1, 0 /*AHD*/, 6 /*CUTOFF*/, 0xD0, 0x00, 0x01, 0x08);
        }

        // --- 04 PAD --------------------------------------------------------
        {
            auto& in = m_state.instruments[4];
            in.type = InstType::INST_MACROSYN;
            setName(in.name, "PAD");
            in.macrosyn.filter_type = 1;      // LP
            in.macrosyn.cutoff      = 0x58;
            in.macrosyn.res         = 0x30;
            in.macrosyn.dry         = 0x90;
            in.macrosyn.cho         = 0xA0;
            in.macrosyn.rev         = 0xB0;
            // Slow swell, long tail. 24 ticks ~= one bar at this tempo.
            amp(4, 0x18, 0x30, 0x40);
            // Slow filter drift so it never sits still.
            mod(4, 1, 3 /*LFO*/, 6 /*CUTOFF*/, 0xB0,
                0x00 /*TRI*/, 0x00 /*FREE*/, 0x08 /*freq*/);
        }

        // --- 05 ARP --------------------------------------------------------
        {
            auto& in = m_state.instruments[5];
            in.type = InstType::INST_MACROSYN;
            setName(in.name, "ARP");
            in.macrosyn.filter_type = 1;      // LP
            in.macrosyn.cutoff      = 0x88;
            in.macrosyn.res         = 0x50;
            in.macrosyn.dry         = 0xA8;
            in.macrosyn.del         = 0x90;   // heavy delay — this is the glue
            in.macrosyn.rev         = 0x30;
            in.macrosyn.pan         = 0x60;
            amp(5, 0x00, 0x01, 0x04);         // plucky
        }

        // --- 06 LEAD -------------------------------------------------------
        {
            auto& in = m_state.instruments[6];
            in.type = InstType::INST_MACROSYN;
            setName(in.name, "LEAD");
            in.macrosyn.filter_type = 1;      // LP
            in.macrosyn.cutoff      = 0x78;
            in.macrosyn.res         = 0x40;
            in.macrosyn.amp         = 0x18;
            in.macrosyn.dry         = 0xC0;
            in.macrosyn.del         = 0x60;
            in.macrosyn.rev         = 0x50;
            in.macrosyn.pan         = 0x94;
            amp(6, 0x01, 0x08, 0x14);
            // Vibrato: a sine LFO on pitch, retriggered per note, small amount.
            // amt is bipolar around 0x80 — 0x88 is a gentle +.
            mod(6, 1, 3 /*LFO*/, 2 /*PITCH*/, 0x88,
                0x01 /*SIN*/, 0x01 /*RETRIG*/, 0x02 /*freq*/);
        }

        // --- 07 PERC (clap) -------------------------------------------------
        {
            auto& in = m_state.instruments[7];
            in.type = InstType::INST_SAMPLER;
            setName(in.name, "CLAP");
            in.sampler.sample      = m_demoClap;
            std::strncpy(in.sampler.samplePath, "demo_clap.wav",
                         sizeof(in.sampler.samplePath) - 1);
            in.sampler.play        = 0;
            in.sampler.filter_type = 2;       // HP
            in.sampler.cutoff      = 0x28;
            in.sampler.dry         = 0xB8;
            in.sampler.rev         = 0x70;    // wet — sits behind the snare
            in.sampler.del         = 0x20;
            in.sampler.pan         = 0x9C;
            amp(7, 0x00, 0x30, 0x20);
        }

        // Mixer: drums pulled back, melodic parts pushed up.
        m_state.mixer.track_vol[0] = 0x98;   // kick   — still the anchor, but not the whole song
        m_state.mixer.track_vol[1] = 0x80;   // snare
        m_state.mixer.track_vol[2] = 0x58;   // hat    — was far too loud for what it is
        m_state.mixer.track_vol[3] = 0x90;   // bass
        m_state.mixer.track_vol[4] = 0xA8;   // pad    — needs to be heard, not implied
        m_state.mixer.track_vol[5] = 0xA0;   // arp
        m_state.mixer.track_vol[6] = 0xB8;   // lead   — it's the melody, let it lead
        m_state.mixer.track_vol[7] = 0x90;   // perc
        m_state.mixer.out_vol      = 0x98;

        // Effects: moderate returns, not slamming the bus.
        m_state.effects.del_time_l   = 0x2C;
        m_state.effects.del_time_r   = 0x3A;   // offset R for width
        m_state.effects.del_feedback = 0x9C;
        m_state.effects.rev_size     = 0xE0;
        m_state.effects.rev_decay    = 0xB0;
    }

    SampleHandle m_demoKick  = -1;
    SampleHandle m_demoSnare = -1;
    SampleHandle m_demoHat   = -1;
    SampleHandle m_demoClap  = -1;

    void processCommands();
    void applyParameterUpdate(const EngineCommand& cmd);
    void doTick();
    void tickTrack(int t);
public:
    void publishPlayhead(int t);

    float getSamplePhase(int t) const { return m_voices[t].getSamplePhase(); }

private:
    void recalcBPM();
    void syncSongRow();

    static inline float dcBlock(float x, float& state) {
        constexpr float c = 0.9998f;
        float y = x - state;
        state = x - c * y;
        return y;
    }

    CommandRing<EngineCommand, 1024>& m_commandRing;
    CommandRing<EngineEvent, 1024> m_eventRing;
    uint64_t m_frameCounter = 0;
    void emit(const EngineEvent& e) { m_eventRing.push(e); }
    CommandRing<SampleData, 64> m_gcRing;
    SamplePool m_samplePool;
    Sequencer m_sequencer;
    SynthVoice m_voices[8];
    EngineState m_state;
    EnvContext m_envCtx;
    std::atomic<uint32_t> m_playheadState[8]{0};

    double m_tickPhase = 0.0;
    double m_samplesPerTick = 0.0;
    
    int m_trackInstrument[8] = {0}; // Currently active instrument index per track
    int m_songRow = 0;               // Shared song row for SONG mode
    bool m_songRowAdvance = false;   // True when any chain ended this tick
    
    float m_smoothChoFreq = 0.0f;
    float m_smoothChoDepth = 0.0f;
    float m_smoothDelL = 0.0f;
    float m_smoothDelR = 0.0f;
    float m_dcDelL = 0.0f;
    float m_dcDelR = 0.0f;
    float m_dcRevL = 0.0f;
    float m_dcRevR = 0.0f;
    float m_dcMixL = 0.0f;
    float m_dcMixR = 0.0f;
    daisysp::Chorus m_chorus;
    daisysp::DelayLine<float, 96000> m_delayL;
    daisysp::DelayLine<float, 96000> m_delayR;
    daisysp::ReverbSc m_reverb;
};

} // namespace engine
} // namespace m8


