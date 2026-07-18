#include <SDL3/SDL.h>
#include "ui/Renderer.h"
#include "ui/ViewManager.h"
#include "engine/Engine.h"
#include "ui/FileBrowser.h"
#include "ui/ScriptRunner.h"
#include "io/SongIO.h"
#include <ui/screens/song/SongScreen.h>
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
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <map>
#include <algorithm>
#include <filesystem>

using namespace m8::engine;

#include "ui/HexFmt.h"

// WAV writer — identical to m8_render's (16-bit PCM, interleaved stereo float input)
static void writeWav(const std::string& path, const std::vector<float>& interleaved,
                     int channels, int sampleRate) {
    const uint32_t nFrames  = static_cast<uint32_t>(interleaved.size() / channels);
    const uint32_t dataSize = nFrames * channels * 2;          // 16-bit
    const uint32_t riffSize = 36 + dataSize;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::cerr << "writeWav: cannot open " << path << "\n"; return; }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  u32(riffSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);
    u16(1);                                        // PCM
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * channels * 2));   // byte rate
    u16(static_cast<uint16_t>(channels * 2));                // block align
    u16(16);                                       // bits
    std::fwrite("data", 1, 4, f);  u32(dataSize);

    for (float s : interleaved) {
        s = std::max(-1.0f, std::min(1.0f, s));
        int16_t v = static_cast<int16_t>(s * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

// AdjustU8/AdjustS8/ModifyValue/InsertDefault moved to ui/UiEditHelpers.h
// (m8::ui namespace) when per-screen input handling was extracted out of
// main() (CODE_CLEANUP_SPEC.md #1) -- every call site now lives in the
// screens/<name>/ files that actually use them.

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

// Loads a song from `path` into the engine + UI mirrors: PLAY_STOP, pack a
// [Sequencer][EngineState] buffer, push LOAD_SONG, update uiSequencer/
// uiEngineState/currentSongPath/currentLoadResult, and set missingSamplesMsg
// (either "LOAD FAILED: ..." or the missing-samples list, or cleared).
// Shared by both file-browser load sites (key-down select, key-up release)
// and ScriptRunner's loadSong callback -- previously duplicated ~35 lines
// each, three times over (plus a fourth copy that was unreachable dead code
// and has been removed rather than folded in here).
static bool loadSongIntoEngine(const std::string& path, const std::string& sampleRoot,
                                CommandRing<EngineCommand, 1024>& commandRing,
                                m8::engine::Sequencer& uiSequencer,
                                m8::engine::EngineState& uiEngineState,
                                std::string& currentSongPath,
                                m8::io::LoadResult& currentLoadResult,
                                std::string& missingSamplesMsg) {
    auto result = m8::io::loadSong(path, sampleRoot);
    if (!result.ok) {
        missingSamplesMsg = "LOAD FAILED: " + result.error;
        return false;
    }

    EngineCommand stopCmd{};
    stopCmd.type = CommandType::PLAY_STOP;
    commandRing.push(stopCmd);

    auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
    *reinterpret_cast<Sequencer*>(buf) = result.sequencer;
    *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = result.state;

    EngineCommand loadCmd{};
    loadCmd.type = CommandType::LOAD_SONG;
    loadCmd.u.song.data = buf;
    commandRing.push(loadCmd);

    uiSequencer = result.sequencer;
    uiEngineState = result.state;
    currentSongPath = path;
    currentLoadResult = std::move(result);
    // NOTE: previously the interactive-load call sites (file-browser select
    // and file-browser key-up-release) cleared currentLoadResult.original
    // here; the ScriptRunner load path never did. saveSong() re-parses from
    // origin.original (SongIO.cpp), so clearing it here would break any
    // save that follows a load -- confirmed by save_reload.m8script failing
    // once this helper unified on the clearing behavior (CODE_CLEANUP_SPEC.md
    // #2). Kept non-clearing, matching the one path that was actually
    // tested end-to-end (load then save).
    currentLoadResult.ok = true;

    if (!currentLoadResult.missing.empty()) {
        missingSamplesMsg = "MISSING SAMPLES:";
        for (const auto& s : currentLoadResult.missing)
            missingSamplesMsg += "\n  " + s;
    } else {
        missingSamplesMsg.clear();
    }
    return true;
}

// loadSongIntoEngine() loads the song structure but not its sample data. Decode
// each sampler instrument's WAV (resolved under sampleRoot) and push a
// LOAD_SAMPLE so the audio thread installs it — mirrors the offline renderer's
// setup. Missing samples are simply skipped (the instrument stays silent; the
// missing-samples overlay is driven separately by loadSongIntoEngine).
static void loadSongSamples(const EngineState& state, const std::string& sampleRoot,
                            CommandRing<EngineCommand, 1024>& commandRing) {
    for (int i = 0; i < 128; ++i) {
        if (state.instruments[i].type != InstType::INST_SAMPLER) continue;
        const char* mpath = state.instruments[i].sampler.samplePath;
        if (mpath[0] == '\0') continue;
        std::string rel = mpath;
        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);   // M8 paths are absolute
        std::string resolved = sampleRoot.empty() ? rel : sampleRoot + "/" + rel;

        SampleData buf{};
        if (!FileBrowser::loadWavFile(resolved, buf) && !FileBrowser::loadWavFile(rel, buf))
            continue;  // missing on disk — leave the instrument silent
        std::strncpy(buf.path, mpath, sizeof(buf.path) - 1);
        buf.path[sizeof(buf.path) - 1] = '\0';

        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SAMPLE;
        cmd.targetId = i;
        cmd.u.sample = buf;
        if (!commandRing.push(cmd)) FileBrowser::freeWavFile(buf);
    }
}

// Locate the startup song. Its committed location is <repo>/songs/, but the app
// is commonly launched from build/Release/ (or elsewhere), so a bare relative
// "songs/..." fails and the app would silently fall back to the demo. Search the
// CWD first, then directories relative to the executable (repo root is
// build/Release/../../). On success, returns the song path and its containing
// "songs" dir as the sample root (the song's "/samples/..." resolve under it).
static bool findStartupSong(const std::string& fileName,
                            std::string& outSong, std::string& outSampleRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> roots = { fs::path("songs") };
    if (const char* base = SDL_GetBasePath()) {   // SDL3: owned by SDL, do not free
        fs::path b(base);
        roots.push_back(b / "songs");
        roots.push_back(b / ".." / "songs");
        roots.push_back(b / ".." / ".." / "songs");
        roots.push_back(b / ".." / ".." / ".." / "songs");
    }
    for (const auto& r : roots) {
        if (fs::exists(r / fileName, ec)) {
            fs::path canon = fs::weakly_canonical(r, ec);
            if (ec) canon = r;
            outSampleRoot = canon.string();
            outSong = (canon / fileName).string();
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
// ─── Script mode / headless / out-dir flag parsing ──────────────────────────
    std::string scriptPath;
    std::string outDir;
    bool headless = false;
    bool updateGoldens = false;  // Tier 5: assert_matches_golden writes instead of compares
    {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--script" && i + 1 < argc) {
                scriptPath = argv[++i];
            } else if (arg == "--headless") {
                headless = true;
            } else             if (arg == "--out-dir" && i + 1 < argc) {
                outDir = argv[++i];
                std::filesystem::create_directories(outDir);
            } else if (arg == "--update-goldens") {
                updateGoldens = true;
            }
        }
    }
    bool scriptMode = !scriptPath.empty();
    bool audioActive = false;  // true when stream is non-null (audio device owns engine.render())

    Renderer renderer;
    if (!renderer.init(320, 240, 3, headless)) {
        return 1;
    }

    bool running = true;
    m8::ui::ViewManager viewManager;
    
    bool shiftHeld = false;
    bool editHeld = false;
    bool arrowPressedDuringEdit = false;
    
    int cursorRow = 0; 
    int cursorCol = 0; 
    
    m8::ui::instrument::CursorId active_cursor = m8::ui::instrument::CursorId::TYPE;
    m8::ui::project::CursorId project_cursor_id = m8::ui::project::CursorId::TEMPO_INT;
    m8::ui::mods::CursorId active_cursor_mod = m8::ui::mods::CursorId::MOD_TYPE_0;
    m8::ui::mixer::CursorId active_cursor_mixer = m8::ui::mixer::CursorId::TRK_VOL_0;
    m8::ui::effects::CursorId active_cursor_effects = m8::ui::effects::CursorId::CHO_EQ;
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

    m8::ui::scale::CursorId scale_cursor_id = m8::ui::scale::CursorId::KEY;
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

    m8::ui::CommandSink commandSink{commandRing};

    struct PendingEdit { CommandType type; int targetId; int row; };
    std::vector<PendingEdit> pendingEdits;

    auto engine_ptr = std::make_unique<Engine>(commandRing);
    Engine& engine = *engine_ptr;
    
    g_audioCtx.engine = &engine;

    SDL_AudioStream *stream = nullptr;
    if (!headless) {
        SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, static_cast<int>(m8::engine::kSampleRate) };
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, [](void* userdata, SDL_AudioStream* astream, int additional_amount, int total_amount) {
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
    }
    
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

    // Song persistence state
    m8::io::LoadResult currentLoadResult;
    std::string currentSongPath;
    std::string sampleRoot = "Samples";
    bool browserForSongLoad = false;  // true = browser filters .m8s for song load
    bool textInputActive = false;
    std::string textInputBuffer;
    std::string textInputPrompt;
    std::string missingSamplesMsg;     // non-empty = show missing samples overlay
    int missingSamplesScroll = 0;

    SDL_Color colorBg = {0, 0, 0, 255};
    SDL_Color colorRed = {255, 60, 60, 255};
    SDL_Color colorCyan = {0, 255, 255, 255};
    SDL_Color colorWhite = {255, 255, 255, 255};
    SDL_Color colorGrey = {100, 100, 100, 255};
    SDL_Color colorGreen = {0, 255, 100, 255};
    SDL_Color colorBar = {120, 170, 170, 255};

    // pushParam moved to m8::ui::PushParam (ui/UiCommands.h) when per-screen
    // input handling was extracted out of main() (CODE_CLEANUP_SPEC.md #1) --
    // every call site now lives in the screens/<name>/ files that use it.

    // Startup song is LOADED from disk (data, not hard-coded into the app). The
    // in-code demo is only a fallback if the song file is missing.
    {
        std::string startupSong, startupRoot;
        bool loaded = false;
        if (findStartupSong("sunrise.m8s", startupSong, startupRoot)) {
            sampleRoot = startupRoot;
            m8::ui::project::setSampleRoot(sampleRoot);
            loaded = loadSongIntoEngine(startupSong, sampleRoot, commandRing, uiSequencer,
                                        uiEngineState, currentSongPath, currentLoadResult,
                                        missingSamplesMsg);
            if (loaded) loadSongSamples(uiEngineState, sampleRoot, commandRing);
        }
        if (!loaded) {
            engine.loadDemoSong();
            uiSequencer = engine.getSequencerForInit();
            uiEngineState = engine.getStateForInit();
        }
    }

    // ─── Script runner setup ───────────────────────────────────────────────
    std::unique_ptr<ScriptRunner> scriptRunner;
    if (scriptMode) {
        scriptRunner = std::make_unique<ScriptRunner>();
        if (!scriptRunner->loadScript(scriptPath)) {
            return scriptRunner->getExitCode();
        }
        scriptRunner->setOutDir(outDir);
        scriptRunner->setUpdateGoldens(updateGoldens);
    }

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

        // Store playheads in renderer for dump_json
        {
            StoredPlayhead sph[8];
            for (int i = 0; i < 8; ++i) {
                sph[i].track     = i;
                sph[i].songRow   = playheads[i].songRow;
                sph[i].chainRow  = playheads[i].chainRow;
                sph[i].phraseRow = playheads[i].phraseRow;
                sph[i].playMode  = playheads[i].playMode;
            }
            renderer.setPlayheads(sph, 8);
        }

        bool isPlaying = playheads[0].playMode != static_cast<uint8_t>(m8::engine::PlayMode::NONE);
        m8::engine::EngineEvent ev;
        while (engine.getEventRing().pop(ev)) {}

        // Script mode: push synthetic events before SDL_PollEvent
        if (scriptRunner) scriptRunner->onFrameStart();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (textInputActive && event.type == SDL_EVENT_TEXT_INPUT) {
                textInputBuffer += event.text.text;
            } else if (!missingSamplesMsg.empty() && event.type == SDL_EVENT_KEY_DOWN) {
                missingSamplesMsg.clear();
            } else if (textInputActive && event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    SDL_StopTextInput(SDL_GetKeyboardFocus());
                    if (textInputPrompt == "SAVE AS:") {
                        if (!textInputBuffer.empty()) {
                            currentSongPath = textInputBuffer;
                            if (currentSongPath.find('.') == std::string::npos)
                                currentSongPath += ".m8s";
                            std::string err;
                            bool ok = m8::io::saveSong(currentSongPath, currentLoadResult,
                                                       uiSequencer, uiEngineState, err);
                            if (!ok) missingSamplesMsg = "SAVE FAILED: " + err;
                            else missingSamplesMsg = "SAVED: " + currentSongPath;
                        }
                    } else if (textInputPrompt == "SAMPLE ROOT:") {
                        sampleRoot = textInputBuffer;
                        m8::ui::project::setSampleRoot(sampleRoot);
                    }
                    textInputActive = false;
                } else if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_BACKSPACE) {
                    if (event.key.key == SDLK_BACKSPACE && !textInputBuffer.empty()) {
                        textInputBuffer.pop_back();
                    } else if (event.key.key == SDLK_ESCAPE) {
                        SDL_StopTextInput(SDL_GetKeyboardFocus());
                        textInputActive = false;
                    }
                } else if (event.key.key == SDLK_LEFT) {
                    viewManager.popModal();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_LSHIFT) {
                    shiftHeld = true;
                } else if (event.key.key == SDLK_X) {
                    if (!editHeld) {
                        editHeld = true;
                        arrowPressedDuringEdit = false;
                    }
                } else {
                    // FILE_BROWSER modal: handle escape/select before navigation
                    if (viewManager.getCurrentView() == m8::ui::ViewType::FILE_BROWSER) {
                        if (event.key.key == SDLK_LEFT || event.key.key == SDLK_ESCAPE) {
                            viewManager.popModal();
                            continue;
                        }
                        std::string path = fileBrowser.handleInput(event, editHeld);
                        if (!path.empty()) {
                            if (browserForSongLoad) {
                                loadSongIntoEngine(path, sampleRoot, commandRing, uiSequencer,
                                                   uiEngineState, currentSongPath,
                                                   currentLoadResult, missingSamplesMsg);
                            } else {
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
                            }
                            viewManager.popModal();
                        }
                        continue;
                    }

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
                    // NOTE: a second FILE_BROWSER branch used to exist here as an
                    // `else if` after `handleNavigation(...)`. It was unreachable:
                    // the block above unconditionally `continue`s whenever the
                    // current view is FILE_BROWSER, and `ViewManager::handleNavigation`
                    // returns false without side effects while a modal is active
                    // (ViewManager.cpp:43) -- so getCurrentView() can never become
                    // FILE_BROWSER between here and where that branch used to be
                    // checked. Confirmed dead, removed (CODE_CLEANUP_SPEC.md #2).
                    } else if (viewManager.getCurrentView() == m8::ui::ViewType::PHRASE) {
                        m8::ui::phrase::HandlePhraseInput(event, editHeld, arrowPressedDuringEdit,
                                                          uiSequencer, currentPhrase, cursorCol, cursorRow, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
                    m8::ui::chain::HandleChainInput(event, editHeld, arrowPressedDuringEdit,
                                                    uiSequencer, currentChain, chainCol, chainRow, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
                    m8::ui::song::HandleSongInput(event, editHeld, arrowPressedDuringEdit,
                                                  uiSequencer, songCol, songRow, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INSTRUMENT) {
                    m8::ui::instrument::HandleInstrumentInput(event, editHeld, arrowPressedDuringEdit,
                                                              uiEngineState, currentInstIndex, active_cursor,
                                                              commandSink, viewManager);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::TABLE) {
                    m8::ui::table::HandleTableInput(event, editHeld, table_cursor_x, table_cursor_y);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_MOD) {
                    m8::ui::mods::HandleModInput(event, editHeld, arrowPressedDuringEdit,
                                                 uiEngineState, currentInstIndex, active_cursor_mod, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::PROJECT) {
                    m8::ui::project::ProjectActionState projActions{
                        browserForSongLoad, fileBrowser, viewManager, textInputActive,
                        textInputBuffer, textInputPrompt, currentSongPath, currentLoadResult,
                        uiSequencer, missingSamplesMsg
                    };
                    m8::ui::project::HandleProjectInput(event, editHeld, arrowPressedDuringEdit,
                                                        uiEngineState, project_cursor_id, commandSink, projActions);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::GROOVE) {
                    m8::ui::groove::HandleGrooveInput(event, editHeld, arrowPressedDuringEdit,
                                                      grooves[currentGrooveIndex], currentGrooveIndex,
                                                      groove_cursor_y, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::SCALE) {
                    m8::ui::scale::HandleScaleInput(event, editHeld, arrowPressedDuringEdit,
                                                    uiEngineState, currentScaleIndex, scale_cursor_id, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::INST_POOL) {
                    m8::ui::inst_pool::HandleInstPoolInput(event, editHeld, arrowPressedDuringEdit,
                                                           uiEngineState, pool_cursor_x, pool_cursor_y, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::MIXER) {
                    m8::ui::mixer::HandleMixerInput(event, editHeld, arrowPressedDuringEdit,
                                                    uiEngineState, active_cursor_mixer, commandSink);
                } else if (viewManager.getCurrentView() == m8::ui::ViewType::EFFECTS) {
                    m8::ui::effects::HandleEffectsInput(event, editHeld, arrowPressedDuringEdit,
                                                        uiEngineState, active_cursor_effects, commandSink);
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
                } else if (event.key.key == SDLK_F1) {
                    // TEMPORARY debug hook — to be replaced by --script mode (Task 1/2)
                    auto viewTypeToStr = [](m8::ui::ViewType v) -> const char* {
                        switch (v) {
                            case m8::ui::ViewType::SONG: return "SONG";
                            case m8::ui::ViewType::CHAIN: return "CHAIN";
                            case m8::ui::ViewType::PHRASE: return "PHRASE";
                            case m8::ui::ViewType::INSTRUMENT: return "INST";
                            case m8::ui::ViewType::TABLE: return "TABLE";
                            case m8::ui::ViewType::PROJECT: return "PROJECT";
                            case m8::ui::ViewType::GROOVE: return "GROOVE";
                            case m8::ui::ViewType::MIXER: return "MIXER";
                            case m8::ui::ViewType::INST_MOD: return "MODS";
                            case m8::ui::ViewType::EFFECTS: return "FX";
                            case m8::ui::ViewType::SCALE: return "SCALE";
                            case m8::ui::ViewType::INST_POOL: return "POOL";
                            case m8::ui::ViewType::FILE_BROWSER: return "BROWSER";
                            default: return "NONE";
                        }
                    };
                    const char* screenName = viewTypeToStr(viewManager.getCurrentView());
                    renderer.dumpScreenText("dump_screen.txt");
                    renderer.dumpJson("dump_json.json", screenName, uiEngineState.bpm);
                }
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                if (event.key.key == SDLK_LSHIFT) {
                    shiftHeld = false;
                } else if (event.key.key == SDLK_X) {
                    editHeld = false;
                    if (!arrowPressedDuringEdit) {
                        if (viewManager.getCurrentView() == m8::ui::ViewType::PHRASE) {
                            m8::ui::phrase::HandlePhraseEditRelease(uiSequencer, currentPhrase, cursorCol, cursorRow, commandSink);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::CHAIN) {
                            m8::ui::chain::HandleChainEditRelease(uiSequencer, currentChain, chainCol, chainRow, commandSink);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::SONG) {
                            m8::ui::song::HandleSongEditRelease(uiSequencer, songCol, songRow, commandSink);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::INSTRUMENT) {
                            m8::ui::instrument::HandleInstrumentEditRelease(active_cursor, browserForSongLoad, fileBrowser, viewManager);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::PROJECT) {
                            m8::ui::project::ProjectActionState projActions{
                                browserForSongLoad, fileBrowser, viewManager, textInputActive,
                                textInputBuffer, textInputPrompt, currentSongPath, currentLoadResult,
                                uiSequencer, missingSamplesMsg
                            };
                            m8::ui::project::HandleProjectEditRelease(project_cursor_id, uiEngineState, projActions);
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::FILE_BROWSER) {
                            SDL_Event simEvent;
                            simEvent.type = SDL_EVENT_KEY_DOWN;
                            simEvent.key.key = SDLK_RIGHT;
                            std::string path = fileBrowser.handleInput(simEvent, false);
                            if (!path.empty()) {
                                if (browserForSongLoad) {
                                    loadSongIntoEngine(path, sampleRoot, commandRing, uiSequencer,
                                                       uiEngineState, currentSongPath,
                                                       currentLoadResult, missingSamplesMsg);
                                } else {
                                    // Load WAV sample
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
                                }
                                viewManager.popModal();
                            }
                        } else if (viewManager.getCurrentView() == m8::ui::ViewType::GROOVE) {
                            m8::ui::groove::HandleGrooveEditRelease(grooves[currentGrooveIndex], currentGrooveIndex, groove_cursor_y, commandSink);
                        }
                    }
                }
            }
        }

        // In script mode, if no audio device is playing, call engine.render()
        // manually so commands (LOAD_SONG, PLAY_START etc.) are processed. Do NOT
        // call when a stream exists — the audio callback's render() and a manual
        // render() would both pop from the SPSC CommandRing concurrently, which is
        // a data race. The SPSC ring guarantees safety for ONE consumer; two
        // concurrent consumers (main thread + audio thread) is undefined behavior.
        if (scriptMode && !stream) {
            float scratch[kMaxFramesPerChunk * 2];
            engine.render(scratch, kMaxFramesPerChunk);
        }

        // Re-read playhead state after script events (e.g. PLAY/STOP) are processed
        for (int i=0; i<8; i++) playheads[i] = engine.getPlayhead(i);
        isPlaying = playheads[0].playMode != static_cast<uint8_t>(m8::engine::PlayMode::NONE);

        m8::engine::SampleData gcData;
        while (engine.getGcRing().pop(gcData)) {
            FileBrowser::freeWavFile(gcData);
        }

        // Drain song GC ring
        void* songGcPtr = nullptr;
        while (engine.getSongGcRing().pop(songGcPtr)) {
            delete[] static_cast<uint8_t*>(songGcPtr);
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

        // Missing samples overlay
        if (!missingSamplesMsg.empty() && !textInputActive) {
            renderer.fillRectPixel(0, 0, 320, 240, {0, 0, 0, 220});
            int y = 2;
            std::istringstream iss(missingSamplesMsg);
            std::string line;
            while (std::getline(iss, line) && y < 28) {
                if (line.length() > 38) line = line.substr(0, 38);
                SDL_Color c = (y == 2) ? colorCyan : colorWhite;
                renderer.drawString(line, 1, y, c);
                y++;
            }
            renderer.drawString("PRESS ANY KEY", 10, 29, colorGrey);
        }

        // Text input overlay
        if (textInputActive) {
            renderer.fillRectPixel(0, 0, 320, 240, {0, 0, 0, 220});
            renderer.drawString(textInputPrompt, 2, 10, colorCyan);
            std::string display = textInputBuffer + "_";
            if (display.length() > 36) display = display.substr(display.length() - 36);
            renderer.drawString(display, 2, 12, colorWhite);
            renderer.drawString("ENTER=OK  ESC=CANCEL", 4, 16, colorGrey);
        }

        renderer.present();

        // Script mode: run post-render commands (dump / assert / screenshot)
        if (scriptRunner) {
            audioActive = (stream != nullptr);  // recompute each frame

            // ScriptAppContext's callbacks are C function pointers (no captures), so
            // per-frame state is threaded through via a helper struct + void* userData.
            struct ScriptCtxHelper {
                Renderer* renderer;
                bool playing;
                bool hasErrorFlag;
                const std::string* errorMsg;
                const std::string* songName;
                Engine* engine;
                m8::engine::Sequencer* seq;
                m8::engine::EngineState* state;
                std::string* songPath;
                m8::io::LoadResult* loadResult;
                std::string* err_msg;
                std::string* sampRoot;
                CommandRing<EngineCommand, 1024>* cmdRing;
                bool* audioActive;
            };
            static ScriptCtxHelper helper; // static so C function pointers can access it
            helper.renderer   = &renderer;
            helper.playing    = isPlaying;
            helper.hasErrorFlag = !missingSamplesMsg.empty();
            helper.errorMsg   = &missingSamplesMsg;
            helper.songName   = &currentSongPath;
            helper.engine     = &engine;
            helper.seq        = &uiSequencer;
            helper.state      = &uiEngineState;
            helper.songPath   = &currentSongPath;
            helper.loadResult = &currentLoadResult;
            helper.err_msg    = &missingSamplesMsg;
            helper.sampRoot   = &sampleRoot;
            helper.cmdRing    = &commandRing;
            helper.audioActive = &audioActive;

            ScriptAppContext sctx;
            sctx.userData = &helper;
            sctx.renderer = &renderer;
            sctx.isPlaying = [](void* u) -> bool {
                return static_cast<ScriptCtxHelper*>(u)->playing;
            };
            sctx.hasError = [](void* u) -> bool {
                return !static_cast<ScriptCtxHelper*>(u)->err_msg->empty();
            };
            sctx.getErrorMessage = [](void* u) -> const std::string& {
                return *static_cast<ScriptCtxHelper*>(u)->errorMsg;
            };
            sctx.getSongName = [](void* u) -> const std::string& {
                return *static_cast<ScriptCtxHelper*>(u)->songName;
            };
            // Track-0 playhead observability (Tier 2): reads the engine's
            // wait-free packed atomic directly, same source the UI itself
            // reads for playhead highlighting (see the `playheads[i] =
            // engine.getPlayhead(i)` calls above) -- not the shadow grid,
            // which can't see the playhead line.
            sctx.getPlayheadState = [](void* u) -> uint32_t {
                return static_cast<ScriptCtxHelper*>(u)->engine->getPlayheadState(0);
            };
            sctx.getPlayheadRow = [](void* u) -> int {
                return static_cast<ScriptCtxHelper*>(u)->engine->getPlayhead(0).phraseRow;
            };
            sctx.loadSong = [](const std::string& path, void* u) -> bool {
                auto* h = static_cast<ScriptCtxHelper*>(u);
                return loadSongIntoEngine(path, *h->sampRoot, *h->cmdRing, *h->seq, *h->state,
                                          *h->songPath, *h->loadResult, *h->err_msg);
            };
            sctx.saveSong = [](const std::string& path, void* u) -> bool {
                auto* h = static_cast<ScriptCtxHelper*>(u);
                std::string err;
                bool ok = m8::io::saveSong(path, *h->loadResult, *h->seq, *h->state, err);
                if (!ok) *h->err_msg = "SAVE FAILED: " + err;
                else     h->err_msg->clear();
                *h->songPath = path;
                return ok;
            };
            sctx.setSampleRoot = [](const std::string& root, void* u) {
                auto* h = static_cast<ScriptCtxHelper*>(u);
                *h->sampRoot = root;
                m8::ui::project::setSampleRoot(root);
            };
            sctx.audioActive = helper.audioActive;
            sctx.renderOffline = [](int seconds, const std::string& path, void* u) -> bool {
                auto* h = static_cast<ScriptCtxHelper*>(u);
                if (seconds <= 0) return false;

                // Push PLAY_START in SONG mode
                EngineCommand playCmd{};
                playCmd.type     = CommandType::PLAY_START;
                playCmd.value    = 3;  // SONG mode
                playCmd.targetId = 0;
                playCmd.col      = 0;
                playCmd.row      = 0;
                h->cmdRing->push(playCmd);

                const int total = static_cast<int>(seconds * kSampleRate);
                constexpr int kChunk = 512;
                std::vector<float> audio;
                audio.reserve(static_cast<size_t>(total) * 2);
                std::vector<float> buf(kChunk * 2);
                int done = 0;
                while (done < total) {
                    const int n = std::min(kChunk, total - done);
                    h->engine->render(buf.data(), n);
                    audio.insert(audio.end(), buf.begin(), buf.begin() + n * 2);
                    done += n;
                }

                // Stop playback after render
                EngineCommand stopCmd{};
                stopCmd.type = CommandType::PLAY_STOP;
                h->cmdRing->push(stopCmd);

                writeWav(path, audio, 2, static_cast<int>(kSampleRate));
                return true;
            };

            if (!scriptRunner->onFrameEnd(sctx)) {
                running = false; // let the loop exit cleanly for proper teardown
            }
        }

        // In script mode skip real-time delay; in normal mode keep 16 ms frame pacing
        if (!scriptMode) SDL_Delay(16);
    }

    if (stream) {
        SDL_DestroyAudioStream(stream);
    }

    return scriptRunner ? scriptRunner->getExitCode() : 0;
}

