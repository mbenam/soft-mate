#include <SDL3/SDL.h>
#include "ui/Renderer.h"
#include "engine/Engine.h"
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <map>

using namespace m8::engine;

enum ViewType {
    VIEW_SONG, VIEW_CHAIN, VIEW_PHRASE, VIEW_INSTRUMENT, VIEW_TABLE,
    VIEW_PROJECT, VIEW_GROOVE, VIEW_MIXER, VIEW_INST_MOD
};








std::string AdjustHex(const std::string& hexStr, int delta, int minVal, int maxVal, const std::string& emptyStr) {
    int val = 0;
    bool wasEmpty = false;
    if (hexStr == emptyStr || hexStr.find('-') != std::string::npos) {
        wasEmpty = true;
    } else {
        try {
            val = std::stoi(hexStr, nullptr, 16);
        } catch (...) { wasEmpty = true; }
    }
    
    if (wasEmpty) {
        val = (delta > 0) ? minVal : maxVal;
    } else {
        val += delta;
    }
    
    if (val < minVal) val = minVal;
    if (val > maxVal) val = maxVal;
    
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << val;
    return ss.str();
}




void ModifyValue(Step& step, int col, int delta, bool largeStep) {
    if (col == 0) {
        int midi = Sequencer::NoteToMidi(step.note);
        if (midi == -1) {
            midi = 60;
            if (step.vol == "--") step.vol = "64";
            if (step.instr == "--") step.instr = "00";
        } else {
            midi += (largeStep ? delta * 12 : delta);
        }
        step.note = Sequencer::MidiToNote(midi);
    } else if (col == 1) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.vol == "--") step.vol = "64";
        else step.vol = AdjustHex(step.vol, d, 0, 127, "--");
    } else if (col == 2) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.instr == "--") step.instr = "00";
        else step.instr = AdjustHex(step.instr, d, 0, 127, "--");
    } else if (col == 4 || col == 6 || col == 8) {
        int d = largeStep ? delta * 0x10 : delta;
        std::string& target = (col == 4) ? step.fx1 : (col == 6) ? step.fx2 : step.fx3;
        std::string cmd = target.substr(0, 3);
        std::string val = target.substr(3, 2);
        if (cmd != "---") {
            val = AdjustHex(val, d, 0, 255, "00");
            target = cmd + val;
        }
    } else if (col == 3 || col == 5 || col == 7) {
        std::vector<std::string> cmds = {"---", "VOL", "PIT", "DEL", "REV", "HOP", "KIL"};
        std::string& target = (col == 3) ? step.fx1 : (col == 5) ? step.fx2 : step.fx3;
        std::string cmd = target.substr(0, 3);
        std::string val = target.substr(3, 2);
        
        int idx = 0;
        for (size_t i = 0; i < cmds.size(); ++i) {
            if (cmds[i] == cmd) { idx = i; break; }
        }
        idx += delta;
        if (idx < 0) idx = cmds.size() - 1;
        if (idx >= (int)cmds.size()) idx = 0;
        
        target = cmds[idx] + val;
    }
}

void InsertDefault(Step& step, int col) {
    if (col == 0 && step.note == "---") {
        step.note = "C-4";
        if (step.vol == "--") step.vol = "64";
        if (step.instr == "--") step.instr = "00";
    } else if (col == 1 && step.vol == "--") {
        step.vol = "64";
    } else if (col == 2 && step.instr == "--") {
        step.instr = "00";
    } else if (col == 3 && step.fx1.substr(0, 3) == "---") {
        step.fx1 = "VOL00";
    } else if (col == 5 && step.fx2.substr(0, 3) == "---") {
        step.fx2 = "VOL00";
    } else if (col == 7 && step.fx3.substr(0, 3) == "---") {
        step.fx3 = "VOL00";
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

int main(int argc, char* argv[]) {
    Renderer renderer;
    if (!renderer.init(320, 240, 3)) {
        return 1;
    }

    bool running = true;
    
    ViewType currentView = VIEW_PHRASE;
    ViewType previousHorizontalView = VIEW_PHRASE;
    bool shiftHeld = false;
    bool editHeld = false;
    bool arrowPressedDuringEdit = false;
    
    int cursorRow = 0; 
    int cursorCol = 0; 
    
    int songRow = 0;
    int songCol = 0;
    
    int chainRow = 0;
    int chainCol = 0;

    int currentPhrase = 0;
    int currentChain = 0;

    CommandRing<EngineCommand, 256> commandRing;
    Engine engine(commandRing);
    auto& phrases = engine.getSequencer().phrases;
    auto& chains = engine.getSequencer().chains;
    auto& song = engine.getSequencer().song;
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, 
        [](void* userdata, SDL_AudioStream* astream, int additional_amount, int total_amount) {
            Engine* eng = (Engine*)userdata;
            int frames = additional_amount / sizeof(float);
            float* buf = (float*)SDL_malloc(frames * sizeof(float));
            if (buf) {
                eng->render(buf, frames);
                SDL_PutAudioStreamData(astream, buf, frames * sizeof(float));
                SDL_free(buf);
            }
        }, &engine);
    if (stream) {
        SDL_ResumeAudioStreamDevice(stream);
    }

    bool isPlaying = false;

    phrases[0][0] = {"C-4", "64", "00", "---00", "---00", "---00"};
    phrases[0][2] = {"C-4", "40", "00", "---00", "---00", "---00"};
    phrases[0][4] = {"C-4", "64", "00", "---00", "---00", "---00"};
    phrases[0][6] = {"C-4", "20", "00", "---00", "---00", "---00"};
    phrases[0][8] = {"C-4", "64", "00", "---00", "---00", "---00"};
    phrases[0][10] = {"C-4", "10", "00", "---00", "---00", "---00"};
    phrases[0][12] = {"C-4", "64", "00", "---00", "---00", "---00"};
    phrases[0][14] = {"C-4", "00", "00", "---00", "---00", "---00"};

    phrases[1][0] = {"E-4", "64", "00", "---00", "---00", "---00"};
    phrases[1][3] = {"G-4", "40", "00", "---00", "---00", "---00"};
    phrases[1][6] = {"E-4", "40", "00", "---00", "---00", "---00"};
    phrases[1][8] = {"G-4", "64", "00", "---00", "---00", "---00"};
    phrases[1][11] = {"E-4", "40", "00", "---00", "---00", "---00"};
    phrases[1][14] = {"C-4", "20", "00", "---00", "---00", "---00"};

    chains[0][0] = {"00", "0C"};
    chains[0][1] = {"00", "0C"};
    chains[0][2] = {"01", "0C"};
    chains[0][3] = {"01", "0C"};
    
    chains[1][0] = {"01", "00"};
    chains[1][1] = {"00", "00"};
    chains[1][2] = {"01", "00"};
    chains[1][3] = {"00", "00"};
    
    song[1] = {{"01", "00", "--", "--", "--", "--", "--", "--"}};

    SDL_Color colorBg = {0, 0, 0, 255};
    SDL_Color colorRed = {255, 60, 60, 255};
    SDL_Color colorCyan = {0, 255, 255, 255};
    SDL_Color colorWhite = {255, 255, 255, 255};
    SDL_Color colorGrey = {100, 100, 100, 255};
    SDL_Color colorGreen = {0, 255, 100, 255};

    while (running) {
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
                } else if (shiftHeld) {
                    if (event.key.key == SDLK_LEFT) {
                        if (currentView == VIEW_CHAIN) { currentView = VIEW_SONG; previousHorizontalView = currentView; }
                        else if (currentView == VIEW_PHRASE) { currentView = VIEW_CHAIN; previousHorizontalView = currentView; }
                        else if (currentView == VIEW_INSTRUMENT) { currentView = VIEW_PHRASE; previousHorizontalView = currentView; }
                        else if (currentView == VIEW_TABLE) { currentView = VIEW_INSTRUMENT; previousHorizontalView = currentView; }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (currentView == VIEW_SONG) { 
                            currentView = VIEW_CHAIN; 
                            previousHorizontalView = currentView; 
                            if (song[songRow].tracks[songCol] != "--") {
                                currentChain = std::stoi(song[songRow].tracks[songCol], nullptr, 16);
                            }
                        }
                        else if (currentView == VIEW_CHAIN) { 
                            currentView = VIEW_PHRASE; 
                            previousHorizontalView = currentView; 
                            if (chains[currentChain][chainRow].ph != "--") {
                                currentPhrase = std::stoi(chains[currentChain][chainRow].ph, nullptr, 16);
                            }
                        }
                        else if (currentView == VIEW_PHRASE) { currentView = VIEW_INSTRUMENT; previousHorizontalView = currentView; }
                        else if (currentView == VIEW_INSTRUMENT) { currentView = VIEW_TABLE; previousHorizontalView = currentView; }
                    } else if (event.key.key == SDLK_UP) {
                        if (currentView == VIEW_SONG || currentView == VIEW_CHAIN) {
                            previousHorizontalView = currentView;
                            currentView = VIEW_PROJECT;
                        } else if (currentView == VIEW_PHRASE) {
                            previousHorizontalView = currentView;
                            currentView = VIEW_GROOVE;
                        } else if (currentView == VIEW_INSTRUMENT || currentView == VIEW_TABLE) {
                            previousHorizontalView = currentView;
                            currentView = VIEW_INST_MOD;
                        } else if (currentView == VIEW_MIXER) {
                            currentView = previousHorizontalView; 
                        }
                    } else if (event.key.key == SDLK_DOWN) {
                        if (currentView == VIEW_PROJECT || currentView == VIEW_GROOVE || currentView == VIEW_INST_MOD) {
                            currentView = previousHorizontalView;
                        } else if (currentView == VIEW_SONG || currentView == VIEW_CHAIN || currentView == VIEW_PHRASE || currentView == VIEW_INSTRUMENT || currentView == VIEW_TABLE) {
                            previousHorizontalView = currentView;
                            currentView = VIEW_MIXER;
                        }
                    }
                } else if (currentView == VIEW_PHRASE) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, -1, true); arrowPressedDuringEdit = true; }
                        else { cursorRow = (cursorRow + 1) % 16; }
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, 1, true); arrowPressedDuringEdit = true; }
                        else { cursorRow = (cursorRow - 1 + 16) % 16; }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, 1, false); arrowPressedDuringEdit = true; }
                        else { cursorCol = (cursorCol + 1) % 9; }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) { ModifyValue(phrases[currentPhrase][cursorRow], cursorCol, -1, false); arrowPressedDuringEdit = true; }
                        else { cursorCol = (cursorCol - 1 + 9) % 9; }
                    }
                } else if (currentView == VIEW_CHAIN) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].ph = AdjustHex(chains[currentChain][chainRow].ph, -1, 0, 255, "--");
                            else chains[currentChain][chainRow].tsp = AdjustHex(chains[currentChain][chainRow].tsp, -1, 0, 255, "00");
                        } else { chainRow = (chainRow + 1) % 16; }
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].ph = AdjustHex(chains[currentChain][chainRow].ph, 1, 0, 255, "--");
                            else chains[currentChain][chainRow].tsp = AdjustHex(chains[currentChain][chainRow].tsp, 1, 0, 255, "00");
                        } else { chainRow = (chainRow - 1 + 16) % 16; }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].ph = AdjustHex(chains[currentChain][chainRow].ph, 16, 0, 255, "--");
                            else chains[currentChain][chainRow].tsp = AdjustHex(chains[currentChain][chainRow].tsp, 16, 0, 255, "00");
                        } else { chainCol = (chainCol + 1) % 2; }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) {
                            if (chainCol == 0) chains[currentChain][chainRow].ph = AdjustHex(chains[currentChain][chainRow].ph, -16, 0, 255, "--");
                            else chains[currentChain][chainRow].tsp = AdjustHex(chains[currentChain][chainRow].tsp, -16, 0, 255, "00");
                        } else { chainCol = (chainCol - 1 + 2) % 2; }
                    }
                } else if (currentView == VIEW_SONG) {
                    if (event.key.key == SDLK_DOWN) {
                        if (editHeld) song[songRow].tracks[songCol] = AdjustHex(song[songRow].tracks[songCol], -1, 0, 255, "--");
                        else songRow = (songRow + 1) % 256;
                    } else if (event.key.key == SDLK_UP) {
                        if (editHeld) song[songRow].tracks[songCol] = AdjustHex(song[songRow].tracks[songCol], 1, 0, 255, "--");
                        else songRow = (songRow - 1 + 256) % 256;
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (editHeld) song[songRow].tracks[songCol] = AdjustHex(song[songRow].tracks[songCol], 16, 0, 255, "--");
                        else songCol = (songCol + 1) % 8;
                    } else if (event.key.key == SDLK_LEFT) {
                        if (editHeld) song[songRow].tracks[songCol] = AdjustHex(song[songRow].tracks[songCol], -16, 0, 255, "--");
                        else songCol = (songCol - 1 + 8) % 8;
                    }
                } 
                
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_SPACE) {
                    isPlaying = !isPlaying;
                    if (!isPlaying) {
                        playMode = PLAY_NONE;
                        for(int t=0; t<8; ++t) audioState.volume[t] = 0.0f;
                    } else {
                        if (currentView == VIEW_SONG) {
                            playMode = PLAY_SONG;
                            for(int t=0; t<8; ++t) {
                                playSongRow[t] = songRow;
                                playChainRow[t] = 0;
                                playPhraseRow[t] = 0;
                            }
                        } else if (currentView == VIEW_CHAIN) {
                            playMode = PLAY_CHAIN;
                            playChainRow[songCol] = chainRow;
                            playPhraseRow[songCol] = 0;
                        } else {
                            playMode = PLAY_PHRASE;
                            playPhraseRow[songCol] = cursorRow;
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                if (event.key.key == SDLK_LSHIFT) {
                    shiftHeld = false;
                } else if (event.key.key == SDLK_X) {
                    editHeld = false;
                    if (!arrowPressedDuringEdit && currentView == VIEW_PHRASE) {
                        InsertDefault(phrases[currentPhrase][cursorRow], cursorCol);
                    }
                }
            }
        }

        if (isPlaying && playMode != PLAY_NONE) {
            Uint64 currentTime = SDL_GetTicks();
            if (currentTime - lastStepTime >= stepDurationMs) {
                lastStepTime = currentTime;
                
                for (int t = 0; t < 8; ++t) {
                    if (playMode == PLAY_PHRASE || playMode == PLAY_CHAIN) {
                        if (t != songCol) {
                            audioState.volume[t] = 0.0f;
                            continue;
                        }
                    }
                    
                    int phIdx = currentPhrase;
                    int transpose = 0;
                    std::string chainIdStr = "00";
                    
                    if (playMode == PLAY_SONG) {
                        chainIdStr = song[playSongRow[t]].tracks[t];
                        if (chainIdStr == "--") {
                            playSongRow[t] = 0; // Loop back
                            chainIdStr = song[playSongRow[t]].tracks[t];
                            if (chainIdStr == "--") {
                                audioState.volume[t] = 0.0f;
                                continue;
                            }
                        }
                    } else if (playMode == PLAY_CHAIN) {
                        std::stringstream ss; ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentChain;
                        chainIdStr = ss.str();
                    }
                    
                    if (playMode == PLAY_SONG || playMode == PLAY_CHAIN) {
                        int chainId = std::stoi(chainIdStr, nullptr, 16);
                        std::string phStr = chains[chainId][playChainRow[t]].ph;
                        
                        if (phStr == "--") {
                            if (playMode == PLAY_SONG) {
                                playSongRow[t] = (playSongRow[t] + 1) % 256;
                                playChainRow[t] = 0;
                                playPhraseRow[t] = 0;
                                
                                chainIdStr = song[playSongRow[t]].tracks[t];
                                if (chainIdStr == "--") {
                                    playSongRow[t] = 0; // Loop back
                                    chainIdStr = song[playSongRow[t]].tracks[t];
                                }
                                if (chainIdStr != "--") {
                                    chainId = std::stoi(chainIdStr, nullptr, 16);
                                    phStr = chains[chainId][playChainRow[t]].ph;
                                }
                            } else { // PLAY_CHAIN loops
                                playChainRow[t] = 0;
                                playPhraseRow[t] = 0;
                                phStr = chains[chainId][playChainRow[t]].ph;
                                if (phStr == "--") {
                                    isPlaying = false;
                                    playMode = PLAY_NONE;
                                    break;
                                }
                            }
                        }
                        
                        if (phStr != "--" && chainIdStr != "--") {
                            phIdx = std::stoi(phStr, nullptr, 16);
                            std::string tspStr = chains[chainId][playChainRow[t]].tsp;
                            if (tspStr != "00") {
                                int ts = std::stoi(tspStr, nullptr, 16);
                                if (ts >= 128) ts -= 256;
                                transpose = ts;
                            }
                        }
                    }
                    
                    const Step& step = phrases[phIdx][playPhraseRow[t]];
                    float freq = NoteToFrequency(step.note);
                    if (freq > 0.0f) {
                        if (transpose != 0) {
                            int midi = Sequencer::NoteToMidi(step.note);
                            if (midi != -1) {
                                midi += transpose;
                                freq = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
                            }
                        }
                        audioState.frequency[t] = freq;
                    }
                    
                    int vol = ParseVolume(step.vol);
                    if (vol >= 0) {
                        audioState.volume[t] = (float)vol / 100.0f;
                    }
                    
                    playPhraseRow[t]++;
                    if (playPhraseRow[t] >= 16) {
                        playPhraseRow[t] = 0;
                        if (playMode == PLAY_CHAIN || playMode == PLAY_SONG) {
                            playChainRow[t]++;
                        }
                    }
                }
            }
        }

        renderer.clear(colorBg);

        if (currentView == VIEW_PHRASE) {
            std::stringstream titleSS;
            titleSS << "PHRASE " << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentPhrase << "*";
            renderer.drawString(titleSS.str(), 2, 1, colorRed);
            int r = 4;
            renderer.drawString("N", 4, r, cursorCol == 0 ? colorCyan : colorGrey);
            renderer.drawString("V", 7, r, cursorCol == 1 ? colorCyan : colorGrey);
            renderer.drawString("I", 10, r, cursorCol == 2 ? colorCyan : colorGrey);
            renderer.drawString("FX1", 13, r, (cursorCol == 3 || cursorCol == 4) ? colorCyan : colorGrey);
            renderer.drawString("FX2", 19, r, (cursorCol == 5 || cursorCol == 6) ? colorCyan : colorGrey);
            renderer.drawString("FX3", 25, r, (cursorCol == 7 || cursorCol == 8) ? colorCyan : colorGrey);

            for (int i = 0; i < 16; ++i) {
                int y = 6 + i;
                std::stringstream ss;
                ss << std::hex << std::uppercase << i;
                renderer.drawString(ss.str(), 1, y, (i == cursorRow) ? colorCyan : colorGrey);

                if (i == cursorRow) {
                    int px = 0 * 8 + 2;
                    int py = y * 8 + 1;
                    for (int ty = 0; ty < 7; ++ty) {
                        int w = (ty < 4) ? (ty + 1) : (7 - ty);
                        renderer.drawLinePixel(px + 4 - w, py + ty, px + 3, py + ty, colorGrey);
                    }
                    
                    int cx = 0, cw = 0;
                    if (cursorCol == 0) { cx = 3; cw = 3; }
                    else if (cursorCol == 1) { cx = 7; cw = 2; }
                    else if (cursorCol == 2) { cx = 10; cw = 2; }
                    else if (cursorCol == 3) { cx = 13; cw = 3; }
                    else if (cursorCol == 4) { cx = 16; cw = 2; }
                    else if (cursorCol == 5) { cx = 19; cw = 3; }
                    else if (cursorCol == 6) { cx = 22; cw = 2; }
                    else if (cursorCol == 7) { cx = 25; cw = 3; }
                    else if (cursorCol == 8) { cx = 28; cw = 2; }
                    
                    DrawBracket(renderer, cx, y, cw, colorCyan);
                }

                if (isPlaying && playMode == PLAY_PHRASE && i == (playPhraseRow[songCol] - 1 + 16) % 16) {
                    int px = 2 * 8 + 2;
                    int py = y * 8 + 1;
                    for (int ty = 0; ty < 7; ++ty) {
                        int w = (ty < 4) ? (ty + 1) : (7 - ty);
                        renderer.drawLinePixel(px, py + ty, px + w - 1, py + ty, colorGreen);
                    }
                }

                renderer.drawString(phrases[currentPhrase][i].note, 3, y, phrases[currentPhrase][i].note == "---" ? colorGrey : colorWhite);
                renderer.drawString(phrases[currentPhrase][i].vol, 7, y, colorWhite);
                renderer.drawString(phrases[currentPhrase][i].instr, 10, y, phrases[currentPhrase][i].instr == "--" ? colorGrey : colorWhite);
                renderer.drawString(phrases[currentPhrase][i].fx1, 13, y, phrases[currentPhrase][i].fx1 == "---00" ? colorGrey : colorWhite);
                renderer.drawString(phrases[currentPhrase][i].fx2, 19, y, phrases[currentPhrase][i].fx2 == "---00" ? colorGrey : colorWhite);
                renderer.drawString(phrases[currentPhrase][i].fx3, 25, y, phrases[currentPhrase][i].fx3 == "---00" ? colorGrey : colorWhite);
            }
        } else if (currentView == VIEW_CHAIN) {
            std::stringstream titleSS;
            titleSS << "CHAIN " << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentChain << "*";
            renderer.drawString(titleSS.str(), 2, 1, colorRed);
            int r = 4;
            renderer.drawString("PH", 3, r, chainCol == 0 ? colorCyan : colorGrey);
            renderer.drawString("TSP", 6, r, chainCol == 1 ? colorCyan : colorGrey);

            for (int i = 0; i < 16; ++i) {
                int y = 6 + i;
                std::stringstream ss; ss << std::hex << std::uppercase << i;
                renderer.drawString(ss.str(), 1, y, (i == chainRow) ? colorCyan : colorGrey);

                if (i == chainRow) {
                    int px = 0 * 8 + 2;
                    int py = y * 8 + 1;
                    for (int ty = 0; ty < 7; ++ty) {
                        int w = (ty < 4) ? (ty + 1) : (7 - ty);
                        renderer.drawLinePixel(px + 4 - w, py + ty, px + 3, py + ty, colorGrey);
                    }
                    
                    int cx = (chainCol == 0) ? 3 : 6;
                    DrawBracket(renderer, cx, y, 2, colorCyan);
                }
                
                if (isPlaying && playMode == PLAY_CHAIN && i == playChainRow[songCol]) {
                    int px = 2 * 8 + 2;
                    int py = y * 8 + 1;
                    for (int ty = 0; ty < 7; ++ty) {
                        int w = (ty < 4) ? (ty + 1) : (7 - ty);
                        renderer.drawLinePixel(px, py + ty, px + w - 1, py + ty, colorGreen);
                    }
                }
                
                renderer.drawString(chains[currentChain][i].ph, 3, y, chains[currentChain][i].ph == "--" ? colorGrey : colorWhite);
                renderer.drawString(chains[currentChain][i].tsp, 6, y, chains[currentChain][i].tsp == "00" ? colorGrey : colorWhite);
            }
        } else if (currentView == VIEW_SONG) {
            renderer.drawString("SONG", 2, 1, colorRed);
            for(int c = 0; c < 8; ++c) {
                std::stringstream ss; ss << (c + 1);
                renderer.drawString(ss.str(), 4 + c * 3, 4, (songCol == c) ? colorCyan : colorGrey);
            }

            for (int i = 0; i < 16; ++i) {
                int displayRow = i; 
                int y = 6 + i;
                std::stringstream ss; ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << displayRow;
                renderer.drawString(ss.str(), 1, y, (displayRow == songRow) ? colorCyan : colorGrey);

                if (displayRow == songRow) {
                    int px = 0 * 8 + 2;
                    int py = y * 8 + 1;
                    for (int ty = 0; ty < 7; ++ty) {
                        int w = (ty < 4) ? (ty + 1) : (7 - ty);
                        renderer.drawLinePixel(px + 4 - w, py + ty, px + 3, py + ty, colorGrey);
                    }
                    int cx = 4 + songCol * 3;
                    DrawBracket(renderer, cx, y, 2, colorCyan);
                }
                
                for(int c=0; c<8; ++c) {
                    std::string val = song[displayRow].tracks[c];
                    
                    if (isPlaying && playMode == PLAY_SONG && displayRow == playSongRow[c] && val != "--") {
                        int px = (3 + c*3) * 8 + 2;
                        int py = y * 8 + 1;
                        for (int ty = 0; ty < 7; ++ty) {
                            int w = (ty < 4) ? (ty + 1) : (7 - ty);
                            renderer.drawLinePixel(px, py + ty, px + w - 1, py + ty, colorGreen);
                        }
                    }
                
                    renderer.drawString(val, 4 + c*3, y, val == "--" ? colorGrey : colorWhite);
                }
            }
        } else {
            renderer.drawString("NOT IMPLEMENTED", 10, 10, colorWhite);
        }

        // Shared Right side UI
        renderer.drawString("T>130", 32, 4, colorGrey);
        
        for (int i=1; i<=8; ++i) {
            std::stringstream ss;
            ss << i;
            renderer.drawString(ss.str(), 32, 5+i, i==1 ? colorCyan : colorGrey);
            renderer.drawString("---", 35, 5+i, colorGrey);
        }

        int prX = 31 * 8;
        int prY = 15 * 8 + 4; 
        int prW = 7 * 8; 
        int prH = 14;
        renderer.drawLinePixel(prX, prY, prX + prW, prY, colorGrey);
        renderer.drawLinePixel(prX, prY+prH, prX + prW, prY+prH, colorGrey);
        renderer.drawLinePixel(prX, prY, prX, prY+prH, colorGrey);
        renderer.drawLinePixel(prX+prW, prY, prX+prW, prY+prH, colorGrey);
        for(int k = 1; k < 7; ++k) renderer.drawLinePixel(prX + k * 8, prY, prX + k * 8, prY + prH, colorGrey);
        int blackKeys[] = {1, 2, 4, 5, 6};
        for(int k : blackKeys) renderer.fillRectPixel(prX + k * 8 - 3, prY, 6, 9, colorGrey);

        // Dynamic Mini-Map
        int mapY = 26;
        renderer.drawString("SCPIT", 32, mapY, colorGrey);
        
        ViewType trackView = currentView;
        if (currentView == VIEW_MIXER || currentView == VIEW_PROJECT || currentView == VIEW_GROOVE || currentView == VIEW_INST_MOD) {
            trackView = previousHorizontalView;
        }

        if (currentView == VIEW_SONG) renderer.drawString("S", 32, mapY, colorCyan);
        if (currentView == VIEW_CHAIN) renderer.drawString("C", 33, mapY, colorCyan);
        if (currentView == VIEW_PHRASE) renderer.drawString("P", 34, mapY, colorCyan);
        if (currentView == VIEW_INSTRUMENT) renderer.drawString("I", 35, mapY, colorCyan);
        if (currentView == VIEW_TABLE) renderer.drawString("T", 36, mapY, colorCyan);

        int mapX = 32;
        std::string topChar = "";
        bool topActive = (currentView == VIEW_PROJECT || currentView == VIEW_GROOVE || currentView == VIEW_INST_MOD);
        
        if (trackView == VIEW_SONG) { mapX = 32; topChar = "P"; }
        else if (trackView == VIEW_CHAIN) { mapX = 33; topChar = "P"; }
        else if (trackView == VIEW_PHRASE) { mapX = 34; topChar = "G"; }
        else if (trackView == VIEW_INSTRUMENT) { mapX = 35; topChar = "M"; }
        else if (trackView == VIEW_TABLE) { mapX = 36; topChar = "M"; }

        if (!topChar.empty()) {
            renderer.drawString(topChar, mapX, mapY - 1, topActive ? colorCyan : colorGrey);
        }
        
        renderer.drawString("V", mapX, mapY + 1, currentView == VIEW_MIXER ? colorCyan : colorGrey);

        renderer.present();
        SDL_Delay(16); 
    }

    if (stream) {
        SDL_DestroyAudioStream(stream);
    }

    return 0;
}

