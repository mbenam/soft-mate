#include <SDL3/SDL.h>
#include "ui/Renderer.h"
#include "ui/ViewManager.h"
#include "engine/Engine.h"
#include "engine/EngineStateUpdater.h"
#include "ui/FileBrowser.h"
#include "ui/screens/song/SongScreen.h"
#include "ui/screens/chain/ChainScreen.h"
#include "ui/screens/phrase/PhraseScreen.h"
#include "ui/screens/instrument/InstrumentScreen.h"
#include "ui/screens/instrument/InstrumentSamplerLayout.h"
#include "ui/screens/instrument/InstrumentMacrosynLayout.h"
#include "ui/screens/table/TableScreen.h"
#include "ui/screens/project/ProjectScreen.h"
#include "ui/screens/project/ProjectScreenLayout.h"
#include "ui/screens/groove/GrooveScreen.h"
#include "ui/screens/scale/ScaleScreen.h"
#include "ui/screens/scale/ScaleScreenLayout.h"
#include "ui/screens/mods/ModScreen.h"
#include "ui/screens/mods/ModScreenLayout.h"
#include "ui/screens/inst_pool/InstPoolScreen.h"
#include "ui/screens/mixer/MixerScreen.h"
#include "ui/screens/mixer/MixerScreenLayout.h"
#include "ui/screens/effects/EffectsScreen.h"
#include "ui/screens/effects/EffectsScreenLayout.h"
#include <string>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <map>
#include <algorithm>

using namespace m8::engine;

#include "ui/HexFmt.h"

uint8_t AdjustU8(uint8_t val, int delta, int minVal, int maxVal, uint8_t emptyVal) {
    if (val == emptyVal) {
        return (delta > 0) ? minVal : maxVal;
    }
    int newVal = val + delta;
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;
    return newVal;
}

int8_t AdjustS8(int8_t val, int delta, int minVal, int maxVal, int8_t emptyVal) {
    if (val == emptyVal) {
        return (delta > 0) ? minVal : maxVal;
    }
    int newVal = val + delta;
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;
    return newVal;
}

void ModifyValue(Step& step, int col, int delta, bool largeStep) {
    if (col == 0) {
        if (step.note == NOTE_EMPTY) {
            step.note = 60; // C-4
            if (step.vol == VOL_EMPTY) step.vol = 0x64;
            if (step.instr == INST_EMPTY) step.instr = 0;
        } else {
            int midi = step.note;
            midi += (largeStep ? delta * 12 : delta);
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;
            step.note = midi;
        }
    } else if (col == 1) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        else step.vol = std::clamp((int)step.vol + d, 0, 127);
    } else if (col == 2) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.instr == INST_EMPTY) step.instr = 0;
        else step.instr = std::clamp((int)step.instr + d, 0, 127);
    } else if (col == 4 || col == 6 || col == 8) {
        int d = largeStep ? delta * 0x10 : delta;
        int idx = (col == 4) ? 0 : (col == 6) ? 1 : 2;
        if (step.fx[idx].cmd != FxCmd::NONE) {
            step.fx[idx].val = std::clamp((int)step.fx[idx].val + d, 0, 255);
        }
    } else if (col == 3 || col == 5 || col == 7) {
        int idx = (col == 3) ? 0 : (col == 5) ? 1 : 2;
        int cmd = static_cast<int>(step.fx[idx].cmd);
        cmd += delta;
        if (cmd < 0) cmd = 6;
        if (cmd > 6) cmd = 0;
        step.fx[idx].cmd = static_cast<FxCmd>(cmd);
    }
}

void InsertDefault(Step& step, int col) {
    if (col == 0 && step.note == NOTE_EMPTY) {
        step.note = 60; // C-4
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        if (step.instr == INST_EMPTY) step.instr = 0;
    } else if (col == 1 && step.vol == VOL_EMPTY) {
        step.vol = 0x64;
    } else if (col == 2 && step.instr == INST_EMPTY) {
        step.instr = 0;
    } else if (col == 3 && step.fx[0].cmd == FxCmd::NONE) {
        step.fx[0] = {FxCmd::VOL, 0};
    } else if (col == 5 && step.fx[1].cmd == FxCmd::NONE) {
        step.fx[1] = {FxCmd::VOL, 0};
    } else if (col == 7 && step.fx[2].cmd == FxCmd::NONE) {
        step.fx[2] = {FxCmd::VOL, 0};
    }
}

void DrawBracket(Renderer& renderer, int cx, int y, int cw, SDL_Color color) {
    int bpx = cx * 8 - 2;
    int bpy = y * 8 - 1;
    int bpw = cw * 8 + 3; 
    int bph = 8; 
    int len = 2;
    renderer.drawLinePixel(bpx, bpy, bpx + len, bpy, color);
    renderer.drawLinePixel(bpx, bpy, bpx, bpy + len, color);
    renderer.drawLinePixel(bpx + bpw, bpy, bpx + bpw - len, bpy, color);
    renderer.drawLinePixel(bpx + bpw, bpy, bpx + bpw, bpy + len, color);
    renderer.drawLinePixel(bpx, bpy + bph, bpx + len, bpy + bph, color);
    renderer.drawLinePixel(bpx, bpy + bph, bpx, bpy + bph - len, color);
    renderer.drawLinePixel(bpx + bpw, bpy + bph, bpx + bpw - len, bpy + bph, color);
    renderer.drawLinePixel(bpx + bpw, bpy + bph, bpx + bpw, bpy + bph - len, color);
}

// Legacy InstCursorPos removed, navigation is now handled by NavNode map in InstrumentSamplerLayout.h

int main(int argc, char* argv[]) {
    Renderer renderer;
    if (!renderer.init(320, 240, 3)) {
        return 1;
    }

    bool running = true;
    m8::ui::ViewManager viewManager;
    
    bool shiftHeld = false;
    bool editHeld = false;
    bool arrowPressedDuringEdit = false;
    
    int cursorRow = 0; 
    int cursorCol = 0; 
    
    std::string active_cursor = "TYPE";
    std::string project_cursor_id = "TEMPO_INT";
    std::string active_cursor_mod = "MOD_TYPE_0";
    std::string active_cursor_mixer = "TRK_VOL_0";
    std::string active_cursor_effects = "CHO_EQ";
    int currentInstIndex = 0;

    int songRow = 0;
    int songCol = 0;
    
    int chainRow = 0;
    int chainCol = 0;

    int currentPhrase = 0;
    int currentChain = 0;
    
    int table_cursor_x = 0;
    int table_cursor_y = 0;

    int currentGrooveIndex = 0;
    int groove_cursor_y = 0; // Ranges 0 to 15

    std::string scale_cursor_id = "KEY";
    int currentScaleIndex = 0; // Ranges 0 to 15

    int pool_cursor_x = 0; // Ranges 0 to 5
    int pool_cursor_y = 0; // Ranges 0 to 127

    static constexpr int kMaxFramesPerChunk = 2048;
    struct AudioCtx {
        m8::engine::Engine* engine;
        float scratch[kMaxFramesPerChunk * 2];
    };
    static AudioCtx g_audioCtx;

    CommandRing<EngineCommand, 1024> commandRing;
    
    struct CommandSink {
        CommandRing<EngineCommand, 1024>& ring;
        uint32_t dropped = 0;
        bool send(const EngineCommand& cmd) {
            if (ring.push(cmd)) return true;
            ++dropped;
            return false;
        }
    } commandSink{commandRing};

    struct PendingEdit { CommandType type; int targetId; int row; };
    std::vector<PendingEdit> pendingEdits;

    auto engine_ptr = std::make_unique<Engine>(commandRing);
    Engine& engine = *engine_ptr;
    
    g_audioCtx.engine = &engine;

    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, static_cast<int>(m8::engine::kSampleRate) };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, [](void* userdata, SDL_AudioStream* astream, int additional_amount, int total_amount) {
        auto* ctx = static_cast<AudioCtx*>(userdata);
        int framesLeft = additional_amount / (int)(sizeof(float) * 2);
        while (framesLeft > 0) {
            const int n = std::min(framesLeft, kMaxFramesPerChunk);
            ctx->engine->render(ctx->scratch, n);
            SDL_PutAudioStreamData(astream, ctx->scratch, n * 2 * (int)sizeof(float));
            framesLeft -= n;
        }
    }, &g_audioCtx);
    if (stream) { SDL_ResumeAudioStreamDevice(stream); }
    
    m8::engine::Sequencer uiSequencer;
    m8::engine::EngineState uiEngineState;
    auto& phrases = uiSequencer.phrases;
    auto& chains = uiSequencer.chains;
    auto& song = uiSequencer.song;
    auto& grooves = uiSequencer.grooves;

    auto pushStep = [&](int p, int r) {
        EngineCommand cmd; cmd.type = CommandType::SET_STEP; cmd.targetId = p; cmd.row = r; cmd.u.step = phrases[p][r];
        if (!commandSink.send(cmd)) pendingEdits.push_back({cmd.type, p, r});
    };
    auto pushChainStep = [&](int c, int r) {
        EngineCommand cmd; cmd.type = CommandType::SET_CHAIN_STEP; cmd.targetId = c; cmd.row = r; cmd.u.chainStep = chains[c][r];
        if (!commandSink.send(cmd)) pendingEdits.push_back({cmd.type, c, r});
    };
    auto pushSongStep = [&](int s, int t) {
        EngineCommand cmd; cmd.type = CommandType::SET_SONG_STEP; cmd.targetId = s; cmd.row = t; cmd.value = song[s].tracks[t];
        if (!commandSink.send(cmd)) pendingEdits.push_back({cmd.type, s, t});
    };
    auto pushGrooveStep = [&](int g, int r) {
        EngineCommand cmd; cmd.type = CommandType::SET_GROOVE_STEP; cmd.targetId = g; cmd.row = r; cmd.value = grooves[g].steps[r];
        if (!commandSink.send(cmd)) pendingEdits.push_back({cmd.type, g, r});
    };
    // Song data is now initialized by the Engine's Sequencer.
    FileBrowser fileBrowser;
    fileBrowser.init("Samples");

    SDL_Color colorBg = {0, 0, 0, 255};
    SDL_Color colorRed = {255, 60, 60, 255};
    SDL_Color colorCyan = {0, 255, 255, 255};
    SDL_Color colorWhite = {255, 255, 255, 255};
    SDL_Color colorGrey = {100, 100, 100, 255};
    SDL_Color colorGreen = {0, 255, 100, 255};
    SDL_Color colorBar = {120, 170, 170, 255};

    auto pushParam = [&](m8::engine::ParamID id, int val, int target = 0, int row = 0, float fVal = 0.0f) {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::UPDATE_PARAM;
        cmd.paramId = id;
        cmd.targetId = target;
        cmd.row = row;
        cmd.value = val;
        cmd.fValue = fVal;
        commandSink.send(cmd);
        m8::engine::EngineStateUpdater::applyParameterUpdate(uiEngineState, cmd);
    };

    engine.loadDemoSong();
    uiSequencer = engine.getSequencerForInit();
    uiEngineState = engine.getStateForInit();
    while (running) {
        if (!pendingEdits.empty()) {
            std::vector<PendingEdit> oldEdits = std::move(pendingEdits);
            pendingEdits.clear();
            for (const auto& pe : oldEdits) {
                if (pe.type == CommandType::SET_STEP) pushStep(pe.targetId, pe.row);
                else if (pe.type == CommandType::SET_CHAIN_STEP) pushChainStep(pe.targetId, pe.row);
                else if (pe.type == CommandType::SET_SONG_STEP) pushSongStep(pe.targetId, pe.row);
                else if (pe.type == CommandType::SET_GROOVE_STEP) pushGrooveStep(pe.targetId, pe.row);
            }
        }
        

        m8::engine::Playhead playheads[8];
        for (int i=0; i<8; i++) playheads[i] = engine.getPlayhead(i);

        const bool isPlaying = playheads[0].playMode != static_cast<uint8_t>(m8::engine::PlayMode::NONE);
        m8::engine::EngineEvent ev;
        while (engine.getEventRing().pop(ev)) {}

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_LSHIFT) {
                    shiftHeld = true;
                } else if (event.key.key == SDLK_X) {
                    if (!editHeld) {
                        editHeld = true;
                        arrowPressedDuringEdit = false;
                    }
                } else {
                    auto oldView = viewManager.getCurrentView();
                    if (viewManager.handleNavigation(event, shiftHeld)) {
                        auto newView = viewManager.getCurrentView();
                        if (oldView == m8::ui::ViewType::INST_POOL && newView == m8::ui::ViewType::INSTRUMENT) {
                            currentInstIndex = pool_cursor_y;
                        }
                        
                        // Update navigation context variables when drilling down left-to-right
                        if (oldView == m8::ui::ViewType::SONG && newView == m8::ui::ViewType::CHAIN) {
                            if (song[songRow].tracks[songCol] != CHAIN_EMPTY) {
                                currentChain = song[songRow].tracks[songCol];
                            }
                        } else if (oldView == m8::ui::ViewType::CHAIN && newView == m8::ui::ViewType::PHRASE) {
                            if (chains[currentChain][chainRow].phrase != PHRASE_EMPTY) {
                                currentPhrase = chains[currentChain][chainRow].phrase;
                            }
                        } else if (oldView == m8::ui::ViewType::PHRASE && newView == m8::ui::ViewType::INSTRUMENT) {
                            if (phrases[currentPhrase][cursorRow].instr != INST_EMPTY) {
                                currentInstIndex = phrases[currentPhrase][cursorRow].instr;
                            }
                        }
                        
                        continue;
                    } else if (viewManager.getCurrentView() == m8::ui::ViewType::FILE_BROWSER) {
                        std::string path = fileBrowser.handleInput(event, editHeld);
                    if (event.key.key == SDLK_LEFT && !shiftHeld) {
                        viewManager.popModal(); // Escape back
                    } else if (!path.empty()) {
                        // Load the sample
                        m8::engine::SampleData buf;
                        if (FileBrowser::loadWavFile(path, buf)) {
                            std::strncpy(buf.path, path.c_str(), 127);
                            buf.path[127] = '\0';
                            EngineCommand cmd;
                            cmd.type = CommandType::LOAD_SAMPLE;
                            cmd.targetId = currentInstIndex;
                            cmd.u.sample = buf;
                            if (!commandSink.send(cmd)) {
                                FileBrowser::freeWavFile(buf);
                            }
                        }
                        viewManager.popModal();
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::PHRASE) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, -1, true); arrowPressedDuringEdit = true; pushStep(currentPhrase, cursorRow); }
                        else { cursorRow = (cursorRow + 1) % 16; }
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, 1, true); arrowPressedDuringEdit = true; pushStep(currentPhrase, cursorRow); }
                        else { cursorRow = (cursorRow - 1 + 16) % 16; }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, 1, false); arrowPressedDuringEdit = true; pushStep(currentPhrase, cursorRow); }
                        else { cursorCol = (cursorCol + 1) % 9; }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, -1, false); arrowPressedDuringEdit = true; pushStep(currentPhrase, cursorRow); }
                        else { cursorCol = (cursorCol - 1 + 9) % 9; }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].phrase = AdjustU8(chains[currentChain][chainRow].phrase, -1, 0, 254, PHRASE_EMPTY);
                            else chains[currentChain][chainRow].tsp = AdjustS8(chains[currentChain][chainRow].tsp, -1, -128, 127, 0);
                            pushChainStep(currentChain, chainRow);
                            arrowPressedDuringEdit = true;
                        } else { chainRow = (chainRow + 1) % 16; }
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].phrase = AdjustU8(chains[currentChain][chainRow].phrase, 1, 0, 254, PHRASE_EMPTY);
                            else chains[currentChain][chainRow].tsp = AdjustS8(chains[currentChain][chainRow].tsp, 1, -128, 127, 0);
                            pushChainStep(currentChain, chainRow);
                            arrowPressedDuringEdit = true;
                        } else { chainRow = (chainRow - 1 + 16) % 16; }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].phrase = AdjustU8(chains[currentChain][chainRow].phrase, 16, 0, 254, PHRASE_EMPTY);
                            else chains[currentChain][chainRow].tsp = AdjustS8(chains[currentChain][chainRow].tsp, 12, -128, 127, 0);
                            pushChainStep(currentChain, chainRow);
                            arrowPressedDuringEdit = true;
                        } else { chainCol = (chainCol + 1) % 2; }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].phrase = AdjustU8(chains[currentChain][chainRow].phrase, -16, 0, 254, PHRASE_EMPTY);
                            else chains[currentChain][chainRow].tsp = AdjustS8(chains[currentChain][chainRow].tsp, -12, -128, 127, 0);
                            pushChainStep(currentChain, chainRow);
                            arrowPressedDuringEdit = true;
                        } else { chainCol = (chainCol - 1 + 2) % 2; }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) { song[songRow].tracks[songCol] = AdjustU8(song[songRow].tracks[songCol], -1, 0, 254, CHAIN_EMPTY); arrowPressedDuringEdit = true; pushSongStep(songRow, songCol); }
                        else if (songRow < 255) songRow++;
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) { song[songRow].tracks[songCol] = AdjustU8(song[songRow].tracks[songCol], 1, 0, 254, CHAIN_EMPTY); arrowPressedDuringEdit = true; pushSongStep(songRow, songCol); }
                        else if (songRow > 0) songRow--;
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) { song[songRow].tracks[songCol] = AdjustU8(song[songRow].tracks[songCol], 16, 0, 254, CHAIN_EMPTY); arrowPressedDuringEdit = true; pushSongStep(songRow, songCol); }
                        else songCol = (songCol + 1) % 8;
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) { song[songRow].tracks[songCol] = AdjustU8(song[songRow].tracks[songCol], -16, 0, 254, CHAIN_EMPTY); arrowPressedDuringEdit = true; pushSongStep(songRow, songCol); }
                        else songCol = (songCol - 1 + 8) % 8;
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INSTRUMENT) {
                    bool isMac = (uiEngineState.instruments[currentInstIndex].type == InstType::INST_MACROSYN);
                    auto navMap = isMac ? m8::ui::instrument::GetMacrosynNavMap() : m8::ui::instrument::GetSamplerNavMap();
                    
                    if (editHeld && (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP || event.key.key == SDLK_LEFT || event.key.key == SDLK_DOWN)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
                        const m8::engine::Instrument& inst = uiEngineState.instruments[currentInstIndex];
                        bool isMac = (inst.type == m8::engine::InstType::INST_MACROSYN);
                        
                        if (active_cursor == "TYPE") pushParam(m8::engine::ParamID::INST_TYPE, std::clamp<int>(static_cast<int>(inst.type)  + step, 0, 1), currentInstIndex);
                        else if (active_cursor == "TRANSP") pushParam(m8::engine::ParamID::INST_TRANSP, std::clamp<int>((isMac ? inst.macrosyn.transp : inst.sampler.transp)  + step, 0, 1), currentInstIndex);
                        else if (active_cursor == "TBL_TIC") pushParam(m8::engine::ParamID::INST_TBL_TIC, std::clamp<int>((isMac ? inst.macrosyn.tbl_tic : inst.sampler.tbl_tic)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "EQ") pushParam(m8::engine::ParamID::INST_EQ, std::clamp<int>((isMac ? inst.macrosyn.eq : inst.sampler.eq)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "AMP") pushParam(m8::engine::ParamID::INST_AMP, std::clamp<int>((isMac ? inst.macrosyn.amp : inst.sampler.amp)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "LIM") pushParam(m8::engine::ParamID::INST_LIM, std::clamp<int>((isMac ? inst.macrosyn.lim : inst.sampler.lim)  + step, 0, 1), currentInstIndex);
                        else if (active_cursor == "PAN") pushParam(m8::engine::ParamID::INST_PAN, std::clamp<int>((isMac ? inst.macrosyn.pan : inst.sampler.pan)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "DRY") pushParam(m8::engine::ParamID::INST_DRY, std::clamp<int>((isMac ? inst.macrosyn.dry : inst.sampler.dry)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "CHO") pushParam(m8::engine::ParamID::INST_CHO, std::clamp<int>((isMac ? inst.macrosyn.cho : inst.sampler.cho)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "DEL") pushParam(m8::engine::ParamID::INST_DEL, std::clamp<int>((isMac ? inst.macrosyn.del : inst.sampler.del)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "REV") pushParam(m8::engine::ParamID::INST_REV, std::clamp<int>((isMac ? inst.macrosyn.rev : inst.sampler.rev)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "DEGRADE") pushParam(m8::engine::ParamID::INST_DEGRADE, std::clamp<int>((isMac ? inst.macrosyn.degrade : inst.sampler.degrade)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "FILTER") pushParam(m8::engine::ParamID::INST_FILTER, std::clamp<int>((isMac ? inst.macrosyn.filter_type : inst.sampler.filter_type)  + step, 0, 3), currentInstIndex);
                        else if (active_cursor == "CUTOFF") pushParam(m8::engine::ParamID::INST_CUTOFF, std::clamp<int>((isMac ? inst.macrosyn.cutoff : inst.sampler.cutoff)  + step, 0, 255), currentInstIndex);
                        else if (active_cursor == "RES") pushParam(m8::engine::ParamID::INST_RES, std::clamp<int>((isMac ? inst.macrosyn.res : inst.sampler.res)  + step, 0, 255), currentInstIndex);
                        else if (!isMac && active_cursor == "SLICE") pushParam(m8::engine::ParamID::SAMP_SLICE, std::clamp<int>(inst.sampler.slice  + step, 0, 255), currentInstIndex);
                        else if (!isMac && active_cursor == "PLAY") pushParam(m8::engine::ParamID::SAMP_PLAY, std::clamp<int>(inst.sampler.play  + step, 0, 1), currentInstIndex);
                        else if (!isMac && active_cursor == "START") pushParam(m8::engine::ParamID::SAMP_START, std::clamp<int>(inst.sampler.start  + step, 0, 255), currentInstIndex);
                        else if (!isMac && active_cursor == "LOOP_ST") pushParam(m8::engine::ParamID::SAMP_LOOP_ST, std::clamp<int>(inst.sampler.loop_st  + step, 0, 255), currentInstIndex);
                        else if (!isMac && active_cursor == "LENGTH") pushParam(m8::engine::ParamID::SAMP_LENGTH, std::clamp<int>(inst.sampler.length  + step, 0, 255), currentInstIndex);
                        else if (!isMac && active_cursor == "DETUNE") pushParam(m8::engine::ParamID::SAMP_DETUNE, std::clamp<int>(inst.sampler.detune  + step, 0, 255), currentInstIndex);
                        else if (isMac && active_cursor == "SHAPE") pushParam(m8::engine::ParamID::MAC_SHAPE, std::clamp<int>(inst.macrosyn.shape  + step, 0, 3), currentInstIndex);
                        else if (isMac && active_cursor == "TIMBRE") pushParam(m8::engine::ParamID::MAC_TIMBRE, std::clamp<int>(inst.macrosyn.timbre  + step, 0, 255), currentInstIndex);
                        else if (isMac && active_cursor == "COLOR") pushParam(m8::engine::ParamID::MAC_COLOR, std::clamp<int>(inst.macrosyn.color  + step, 0, 255), currentInstIndex);
                        else if (isMac && active_cursor == "REDUX") pushParam(m8::engine::ParamID::MAC_REDUX, std::clamp<int>(inst.macrosyn.redux  + step, 0, 255), currentInstIndex);
                    } else {
                        if (event.key.key == SDLK_DOWN) {
                            if (navMap.count(active_cursor) && !navMap[active_cursor].down.empty()) {
                                active_cursor = navMap[active_cursor].down;
                            }
                        } else if (event.key.key == SDLK_UP) {
                            if (navMap.count(active_cursor) && !navMap[active_cursor].up.empty()) {
                                active_cursor = navMap[active_cursor].up;
                            }
                        } else if (event.key.key == SDLK_RIGHT) {
                            if (navMap.count(active_cursor) && !navMap[active_cursor].right.empty()) {
                                active_cursor = navMap[active_cursor].right;
                            }
                        } else if (event.key.key == SDLK_LEFT) {
                            if (navMap.count(active_cursor) && !navMap[active_cursor].left.empty()) {
                                active_cursor = navMap[active_cursor].left;
                            }
                        } else if (event.key.key == SDLK_RETURN) {
                            if (active_cursor == "SAMPLE_LOAD" || active_cursor == "CMD_LOAD") {
                                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
                            }
                        }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::TABLE) {
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld) table_cursor_y = (table_cursor_y + 1) % 16;
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld) table_cursor_y = (table_cursor_y - 1 + 16) % 16;
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld) table_cursor_x = (table_cursor_x + 1) % 5;
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld) table_cursor_x = (table_cursor_x - 1 + 5) % 5;
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_MOD) {
                    auto navMap = m8::ui::mods::GetModNavMap(uiEngineState.instruments[currentInstIndex]);
                    
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld && navMap.count(active_cursor_mod) && !navMap[active_cursor_mod].down.empty()) {
                            active_cursor_mod = navMap[active_cursor_mod].down;
                        } else if (editHeld) { /* Fall through to Edit Block */ }
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld && navMap.count(active_cursor_mod) && !navMap[active_cursor_mod].up.empty()) {
                            active_cursor_mod = navMap[active_cursor_mod].up;
                        } else if (editHeld) { /* Fall through */ }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld && navMap.count(active_cursor_mod) && !navMap[active_cursor_mod].right.empty()) {
                            active_cursor_mod = navMap[active_cursor_mod].right;
                        } else if (editHeld) { /* Fall through */ }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld && navMap.count(active_cursor_mod) && !navMap[active_cursor_mod].left.empty()) {
                            active_cursor_mod = navMap[active_cursor_mod].left;
                        } else if (editHeld) { /* Fall through */ }
                    }
                    
                    // Value Editing Block
                    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
                        int q = active_cursor_mod.back() - '0';
                        const auto& mod = uiEngineState.instruments[currentInstIndex].mods[q];
                        
                        if (active_cursor_mod.find("MOD_TYPE") != std::string::npos) pushParam(m8::engine::ParamID::MOD_TYPE, std::clamp<int>(mod.type  + step, 0, 5), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_DEST") != std::string::npos) pushParam(m8::engine::ParamID::MOD_DEST, std::clamp<int>(mod.dest  + step, 0, 255), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_AMT") != std::string::npos) pushParam(m8::engine::ParamID::MOD_AMT, std::clamp<int>(mod.amt  + step, 0, 255), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_P1") != std::string::npos) pushParam(m8::engine::ParamID::MOD_P1, std::clamp<int>(mod.p1  + step, 0, 255), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_P2") != std::string::npos) pushParam(m8::engine::ParamID::MOD_P2, std::clamp<int>(mod.p2  + step, 0, 255), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_P3") != std::string::npos) pushParam(m8::engine::ParamID::MOD_P3, std::clamp<int>(mod.p3  + step, 0, 255), currentInstIndex, q);
                        else if (active_cursor_mod.find("MOD_P4") != std::string::npos) pushParam(m8::engine::ParamID::MOD_P4, std::clamp<int>(mod.p4  + step, 0, 255), currentInstIndex, q);
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::PROJECT) {
                    auto navMap = m8::ui::project::GetProjectNavMap();
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld && navMap.count(project_cursor_id) && !navMap[project_cursor_id].down.empty()) {
                            project_cursor_id = navMap[project_cursor_id].down;
                        }
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld && navMap.count(project_cursor_id) && !navMap[project_cursor_id].up.empty()) {
                            project_cursor_id = navMap[project_cursor_id].up;
                        }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld && navMap.count(project_cursor_id) && !navMap[project_cursor_id].right.empty()) {
                            project_cursor_id = navMap[project_cursor_id].right;
                        }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld && navMap.count(project_cursor_id) && !navMap[project_cursor_id].left.empty()) {
                            project_cursor_id = navMap[project_cursor_id].left;
                        }
                    }

                    if (editHeld && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                        int step = (event.key.key == SDLK_RIGHT) ? 1 : -1;
                        if (project_cursor_id == "TEMPO_INT") {
                            pushParam(m8::engine::ParamID::BPM_INT, std::clamp<int>(uiEngineState.bpm  + step, 20, 999));
                        } else if (project_cursor_id == "TEMPO_DEC") {
                            pushParam(m8::engine::ParamID::BPM_FRAC, std::clamp<int>(uiEngineState.bpm_frac  + step, 0, 99));
                        }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::GROOVE) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) {
                            grooves[currentGrooveIndex].steps[groove_cursor_y] = AdjustU8(grooves[currentGrooveIndex].steps[groove_cursor_y], -1, 1, 255, 0); pushGrooveStep(currentGrooveIndex, groove_cursor_y);
                            arrowPressedDuringEdit = true;
                        } else {
                            groove_cursor_y = (groove_cursor_y + 1) % 16;
                        }
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) {
                            grooves[currentGrooveIndex].steps[groove_cursor_y] = AdjustU8(grooves[currentGrooveIndex].steps[groove_cursor_y], 1, 1, 255, 0); pushGrooveStep(currentGrooveIndex, groove_cursor_y);
                            arrowPressedDuringEdit = true;
                        } else {
                            groove_cursor_y = (groove_cursor_y - 1 + 16) % 16;
                        }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) {
                            grooves[currentGrooveIndex].steps[groove_cursor_y] = AdjustU8(grooves[currentGrooveIndex].steps[groove_cursor_y], 16, 1, 255, 0); pushGrooveStep(currentGrooveIndex, groove_cursor_y);
                            arrowPressedDuringEdit = true;
                        }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) {
                            grooves[currentGrooveIndex].steps[groove_cursor_y] = AdjustU8(grooves[currentGrooveIndex].steps[groove_cursor_y], -16, 1, 255, 0); pushGrooveStep(currentGrooveIndex, groove_cursor_y);
                            arrowPressedDuringEdit = true;
                        }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::SCALE) {
                    auto navMap = m8::ui::scale::GetScaleNavMap();
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld && navMap.count(scale_cursor_id) && !navMap[scale_cursor_id].down.empty()) {
                            scale_cursor_id = navMap[scale_cursor_id].down;
                        }
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld && navMap.count(scale_cursor_id) && !navMap[scale_cursor_id].up.empty()) {
                            scale_cursor_id = navMap[scale_cursor_id].up;
                        }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld && navMap.count(scale_cursor_id) && !navMap[scale_cursor_id].right.empty()) {
                            scale_cursor_id = navMap[scale_cursor_id].right;
                        }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld && navMap.count(scale_cursor_id) && !navMap[scale_cursor_id].left.empty()) {
                            scale_cursor_id = navMap[scale_cursor_id].left;
                        }
                    }

                    if (editHeld && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_RIGHT) ? 1 : -1;
                        const auto& scale = uiEngineState.scales[currentScaleIndex];
                        
                        if (scale_cursor_id == "KEY") pushParam(m8::engine::ParamID::SCALE_KEY, (scale.key + step + 12) % 12, currentScaleIndex);
                        else if (scale_cursor_id == "TUNE") pushParam(m8::engine::ParamID::SCALE_TUNE, 0, currentScaleIndex, 0, scale.tune + step);
                        else if (scale_cursor_id.find("NOTE_EN_") == 0) {
                            int idx = std::stoi(scale_cursor_id.substr(8));
                            pushParam(m8::engine::ParamID::SCALE_NOTE_EN, !scale.notes[idx].enable, currentScaleIndex, idx);
                        } else if (scale_cursor_id.find("NOTE_OFFSET_") == 0) {
                            int idx = std::stoi(scale_cursor_id.substr(12));
                            pushParam(m8::engine::ParamID::SCALE_NOTE_OFFSET, 0, currentScaleIndex, idx, scale.notes[idx].offset + step);
                        }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_POOL) {
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld) pool_cursor_y = (pool_cursor_y + 1) % 128;
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld) pool_cursor_y = (pool_cursor_y - 1 + 128) % 128;
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld) pool_cursor_x = (pool_cursor_x + 1) % 6;
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld) pool_cursor_x = (pool_cursor_x - 1 + 6) % 6;
                    }

                    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_UP) ? 1 : -1;
                        const auto& inst = uiEngineState.instruments[pool_cursor_y];
                        bool isMac = (inst.type == m8::engine::InstType::INST_MACROSYN);
                        
                        if (pool_cursor_x == 0) {
                            int t = static_cast<int>(inst.type);
                            pushParam(m8::engine::ParamID::INST_TYPE, (t + step + 3) % 3, pool_cursor_y);
                        } else if (inst.type != m8::engine::InstType::INST_NONE) {
                            if (pool_cursor_x == 1) { int v = isMac ? inst.macrosyn.dry : inst.sampler.dry; pushParam(m8::engine::ParamID::INST_DRY, std::clamp<int>(v  + step, 0, 255), pool_cursor_y); }
                            else if (pool_cursor_x == 2) { int v = isMac ? inst.macrosyn.cho : inst.sampler.cho; pushParam(m8::engine::ParamID::INST_CHO, std::clamp<int>(v  + step, 0, 255), pool_cursor_y); }
                            else if (pool_cursor_x == 3) { int v = isMac ? inst.macrosyn.del : inst.sampler.del; pushParam(m8::engine::ParamID::INST_DEL, std::clamp<int>(v  + step, 0, 255), pool_cursor_y); }
                            else if (pool_cursor_x == 4) { int v = isMac ? inst.macrosyn.rev : inst.sampler.rev; pushParam(m8::engine::ParamID::INST_REV, std::clamp<int>(v  + step, 0, 255), pool_cursor_y); }
                            else if (pool_cursor_x == 5) { int v = isMac ? inst.macrosyn.eq : inst.sampler.eq; pushParam(m8::engine::ParamID::INST_EQ, std::clamp<int>(v  + step, 0, 255), pool_cursor_y); }
                        }
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::MIXER) {
                    auto navMap = m8::ui::mixer::GetMixerNavMap();
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld && navMap.count(active_cursor_mixer) && !navMap[active_cursor_mixer].down.empty()) {
                            active_cursor_mixer = navMap[active_cursor_mixer].down;
                        }
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld && navMap.count(active_cursor_mixer) && !navMap[active_cursor_mixer].up.empty()) {
                            active_cursor_mixer = navMap[active_cursor_mixer].up;
                        }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld && navMap.count(active_cursor_mixer) && !navMap[active_cursor_mixer].right.empty()) {
                            active_cursor_mixer = navMap[active_cursor_mixer].right;
                        }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld && navMap.count(active_cursor_mixer) && !navMap[active_cursor_mixer].left.empty()) {
                            active_cursor_mixer = navMap[active_cursor_mixer].left;
                        }
                    }
                    
                    // Edit Value logic
                    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
                        
                        const auto& mx = uiEngineState.mixer;
                        if (active_cursor_mixer == "OUT_VOL") pushParam(m8::engine::ParamID::MIX_OUT_VOL, std::clamp<int>((int)mx.out_vol  + step, 0, 255));
                        else if (active_cursor_mixer.find("TRK_VOL_") == 0) {
                            int t = active_cursor_mixer.back() - '0';
                            pushParam(m8::engine::ParamID::MIX_TRK_VOL, std::clamp<int>((int)mx.track_vol[t]  + step, 0, 255), 0, t);
                        }
                        else if (active_cursor_mixer == "MST_CHO") pushParam(m8::engine::ParamID::MIX_CHO_VOL, std::clamp<int>((int)mx.cho_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "MST_DEL") pushParam(m8::engine::ParamID::MIX_DEL_VOL, std::clamp<int>((int)mx.del_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "MST_REV") pushParam(m8::engine::ParamID::MIX_REV_VOL, std::clamp<int>((int)mx.rev_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "IN_VOL") pushParam(m8::engine::ParamID::MIX_IN_VOL, std::clamp<int>((int)mx.in_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "IN_CHO") pushParam(m8::engine::ParamID::MIX_IN_CHO, std::clamp<int>((int)mx.in_cho  + step, 0, 255));
                        else if (active_cursor_mixer == "IN_DEL") pushParam(m8::engine::ParamID::MIX_IN_DEL, std::clamp<int>((int)mx.in_del  + step, 0, 255));
                        else if (active_cursor_mixer == "IN_REV") pushParam(m8::engine::ParamID::MIX_IN_REV, std::clamp<int>((int)mx.in_rev  + step, 0, 255));
                        else if (active_cursor_mixer == "USB_VOL") pushParam(m8::engine::ParamID::MIX_USB_VOL, std::clamp<int>((int)mx.usb_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "USB_CHO") pushParam(m8::engine::ParamID::MIX_USB_CHO, std::clamp<int>((int)mx.usb_cho  + step, 0, 255));
                        else if (active_cursor_mixer == "USB_DEL") pushParam(m8::engine::ParamID::MIX_USB_DEL, std::clamp<int>((int)mx.usb_del  + step, 0, 255));
                        else if (active_cursor_mixer == "USB_REV") pushParam(m8::engine::ParamID::MIX_USB_REV, std::clamp<int>((int)mx.usb_rev  + step, 0, 255));
                        else if (active_cursor_mixer == "MIX_VOL") pushParam(m8::engine::ParamID::MIX_MIX_VOL, std::clamp<int>((int)mx.mix_vol  + step, 0, 255));
                        else if (active_cursor_mixer == "LIM_VAL") pushParam(m8::engine::ParamID::MIX_LIM_VAL, std::clamp<int>((int)mx.lim_val  + step, 0, 255));
                        else if (active_cursor_mixer == "DJF_FREQ") pushParam(m8::engine::ParamID::MIX_DJF_FREQ, std::clamp<int>((int)mx.djf_freq  + step, 0, 255));
                        else if (active_cursor_mixer == "DJF_RES") pushParam(m8::engine::ParamID::MIX_DJF_RES, std::clamp<int>((int)mx.djf_res  + step, 0, 255));
                        else if (active_cursor_mixer == "DJF_TYP") pushParam(m8::engine::ParamID::MIX_DJF_TYP, std::clamp<int>((int)mx.djf_typ  + step, 0, 255));
                    }
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::EFFECTS) {
                    auto navMap = m8::ui::effects::GetEffectsNavMap();
                    if (event.key.key == SDLK_DOWN) {
                        if (!editHeld && navMap.count(active_cursor_effects) && !navMap[active_cursor_effects].down.empty()) {
                            active_cursor_effects = navMap[active_cursor_effects].down;
                        }
                    } else if (event.key.key == SDLK_UP) {
                        if (!editHeld && navMap.count(active_cursor_effects) && !navMap[active_cursor_effects].up.empty()) {
                            active_cursor_effects = navMap[active_cursor_effects].up;
                        }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (!editHeld && navMap.count(active_cursor_effects) && !navMap[active_cursor_effects].right.empty()) {
                            active_cursor_effects = navMap[active_cursor_effects].right;
                        }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (!editHeld && navMap.count(active_cursor_effects) && !navMap[active_cursor_effects].left.empty()) {
                            active_cursor_effects = navMap[active_cursor_effects].left;
                        }
                    }
                    
                    // Edit Value logic
                    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                        arrowPressedDuringEdit = true;
                        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
                        const auto& fx = uiEngineState.effects;
                        
                        if (active_cursor_effects == "CHO_MOD_DEP") pushParam(m8::engine::ParamID::FX_CHO_MOD_DEPTH, std::clamp<int>(fx.cho_mod_depth  + step, 0, 255));
                        else if (active_cursor_effects == "CHO_MOD_FRQ") pushParam(m8::engine::ParamID::FX_CHO_MOD_FREQ, std::clamp<int>(fx.cho_mod_freq  + step, 0, 255));
                        else if (active_cursor_effects == "CHO_WID") pushParam(m8::engine::ParamID::FX_CHO_WIDTH, std::clamp<int>(fx.cho_width  + step, 0, 255));
                        else if (active_cursor_effects == "CHO_REV") pushParam(m8::engine::ParamID::FX_CHO_REVERB, std::clamp<int>(fx.cho_reverb  + step, 0, 255));
                        else if (active_cursor_effects == "DEL_TIME_L") pushParam(m8::engine::ParamID::FX_DEL_TIME_L, std::clamp<int>(fx.del_time_l  + step, 0, 255));
                        else if (active_cursor_effects == "DEL_TIME_R") pushParam(m8::engine::ParamID::FX_DEL_TIME_R, std::clamp<int>(fx.del_time_r  + step, 0, 255));
                        else if (active_cursor_effects == "DEL_FBK") pushParam(m8::engine::ParamID::FX_DEL_FEEDBACK, std::clamp<int>(fx.del_feedback  + step, 0, 255));
                        else if (active_cursor_effects == "DEL_WID") pushParam(m8::engine::ParamID::FX_DEL_WIDTH, std::clamp<int>(fx.del_width  + step, 0, 255));
                        else if (active_cursor_effects == "DEL_REV") pushParam(m8::engine::ParamID::FX_DEL_REVERB, std::clamp<int>(fx.del_reverb  + step, 0, 255));
                        else if (active_cursor_effects == "REV_SIZE") pushParam(m8::engine::ParamID::FX_REV_SIZE, std::clamp<int>(fx.rev_size  + step, 0, 255));
                        else if (active_cursor_effects == "REV_DEC") pushParam(m8::engine::ParamID::FX_REV_DECAY, std::clamp<int>(fx.rev_decay  + step, 0, 255));
                        else if (active_cursor_effects == "REV_MOD_DEP") pushParam(m8::engine::ParamID::FX_REV_MOD_DEPTH, std::clamp<int>(fx.rev_mod_depth  + step, 0, 255));
                        else if (active_cursor_effects == "REV_MOD_FRQ") pushParam(m8::engine::ParamID::FX_REV_MOD_FREQ, std::clamp<int>(fx.rev_mod_freq  + step, 0, 255));
                        else if (active_cursor_effects == "REV_WID") pushParam(m8::engine::ParamID::FX_REV_WIDTH, std::clamp<int>(fx.rev_width  + step, 0, 255));
                    }
                }
                
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_SPACE) {
                    EngineCommand cmd;
                    if (isPlaying) {
                        cmd.type = CommandType::PLAY_STOP;
                    } else {
                        cmd.type = CommandType::PLAY_START;
                        if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
                            cmd.targetId = songRow; cmd.col = songCol; cmd.row = 0; cmd.value = 3;
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
                            cmd.targetId = currentChain; cmd.col = chainCol; cmd.row = chainRow; cmd.value = 2;
                        } else {
                            cmd.targetId = currentPhrase; cmd.col = songCol; cmd.row = cursorRow; cmd.value = 1;
                        }
                    }
                    commandSink.send(cmd);
                }
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                if (event.key.key == SDLK_LSHIFT) {
                    shiftHeld = false;
                } else if (event.key.key == SDLK_X) {
                    editHeld = false;
                    if (!arrowPressedDuringEdit) {
                        if (viewManager.getCurrentView() == m8::ui::ViewType::PHRASE) {
                            InsertDefault(phrases[currentPhrase][cursorRow], cursorCol); pushStep(currentPhrase, cursorRow);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
                            if (chainCol == 0) {
                                if (chains[currentChain][chainRow].phrase == PHRASE_EMPTY) chains[currentChain][chainRow].phrase = 0;
                                else chains[currentChain][chainRow].phrase = PHRASE_EMPTY;
                                pushChainStep(currentChain, chainRow);
                            }
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
                            if (song[songRow].tracks[songCol] == CHAIN_EMPTY) song[songRow].tracks[songCol] = 0;
                            else song[songRow].tracks[songCol] = CHAIN_EMPTY;
                            pushSongStep(songRow, songCol);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::INSTRUMENT) {
                            if (active_cursor == "SAMPLE_LOAD" || active_cursor == "CMD_LOAD") {
                                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
                            }
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::FILE_BROWSER) {
                            SDL_Event simEvent;
                            simEvent.type = SDL_EVENT_KEY_DOWN;
                            simEvent.key.key = SDLK_RIGHT;
                            std::string path = fileBrowser.handleInput(simEvent, false);
                            if (!path.empty()) {
                                m8::engine::SampleData buf;
                                if (FileBrowser::loadWavFile(path, buf)) {
                                    std::strncpy(buf.path, path.c_str(), 127);
                                    buf.path[127] = '\0';
                                    EngineCommand cmd;
                                    cmd.type = CommandType::LOAD_SAMPLE;
                                    cmd.targetId = currentInstIndex;
                                    cmd.u.sample = buf;
                                    if (!commandSink.send(cmd)) {
                                        FileBrowser::freeWavFile(buf);
                                    }
                                }
                                viewManager.popModal();
                            }
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::GROOVE) {
                            if (grooves[currentGrooveIndex].steps[groove_cursor_y] == 0) {
                                grooves[currentGrooveIndex].steps[groove_cursor_y] = 6;
                            } else {
                                grooves[currentGrooveIndex].steps[groove_cursor_y] = 0;
                            }
                            pushGrooveStep(currentGrooveIndex, groove_cursor_y);
                        }
                    }
                }
            }
        }



        m8::engine::SampleData gcData;
        while (engine.getGcRing().pop(gcData)) {
            FileBrowser::freeWavFile(gcData);
        }

        renderer.clear(colorBg);

        if (viewManager.getCurrentView() == m8::ui::ViewType::PHRASE) {
            m8::ui::phrase::RenderPhraseScreen(renderer, uiSequencer, uiEngineState, playheads, currentPhrase, cursorCol, cursorRow, songCol, isPlaying);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
            m8::ui::chain::RenderChainScreen(renderer, uiSequencer, uiEngineState, playheads, currentChain, chainCol, chainRow, isPlaying);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
            m8::ui::song::RenderSongScreen(renderer, uiSequencer, uiEngineState, playheads, songCol, songRow, isPlaying);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::INSTRUMENT) {
            m8::ui::instrument::RenderInstrumentScreen(renderer, uiEngineState, currentInstIndex, active_cursor);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::TABLE) {
            m8::ui::table::RenderTableScreen(renderer, uiSequencer, uiEngineState, currentInstIndex, table_cursor_x, table_cursor_y);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_MOD) {
            m8::ui::mods::RenderModScreen(renderer, uiEngineState, currentInstIndex, active_cursor_mod);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::PROJECT) {
            m8::ui::project::RenderProjectScreen(renderer, uiEngineState, project_cursor_id);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::GROOVE) {
            m8::ui::groove::RenderGrooveScreen(renderer, uiEngineState, uiSequencer.grooves[currentGrooveIndex], currentGrooveIndex, groove_cursor_y);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::SCALE) {
            m8::ui::scale::RenderScaleScreen(renderer, uiEngineState, currentScaleIndex, scale_cursor_id);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_POOL) {
            m8::ui::inst_pool::RenderInstPoolScreen(renderer, uiEngineState, pool_cursor_x, pool_cursor_y);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::MIXER) {
            m8::ui::mixer::RenderMixerScreen(renderer, uiEngineState, active_cursor_mixer);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::EFFECTS) {
            m8::ui::effects::RenderEffectsScreen(renderer, uiEngineState, active_cursor_effects);
        } else if (viewManager.getCurrentView() == m8::ui::ViewType::FILE_BROWSER) {
            fileBrowser.update(renderer, colorWhite, colorCyan, colorRed);
        } else {
            renderer.drawString("NOT IMPLEMENTED", 10, 10, colorWhite);
        }

        // Legacy Shared Right side UI has been removed because it is now driven by JSON.

        viewManager.renderChrome(renderer, uiEngineState.bpm);
        renderer.present();
        SDL_Delay(16); 
    }

    if (stream) {
        SDL_DestroyAudioStream(stream);
    }

    return 0;
}

