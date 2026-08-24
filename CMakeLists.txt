cmake_minimum_required(VERSION 3.21)
project(m8_clone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(M8_SANITIZE "off" CACHE STRING "asan|tsan|off")
if(M8_SANITIZE STREQUAL "asan")
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(M8_SANITIZE STREQUAL "tsan")
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

# Gates fetching SDL3 and building the m8_clone app. OFF builds only the
# engine, the offline tools (m8_render/m8_analyze/m8_spectrum/m8_makeprobe/
# m8_capture), and m8_tests -- none of which need SDL.
option(BUILD_CLONE "Build the SDL3 app (m8_clone)" ON)

include(FetchContent)

# Fetch SDL3
if(BUILD_CLONE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG preview-3.1.3 # Or latest main, but a specific tag is safer
)
FetchContent_MakeAvailable(SDL3)
endif()

# Fetch DaisySP
FetchContent_Declare(
    DaisySP
    GIT_REPOSITORY https://github.com/electro-smith/DaisySP.git
    GIT_TAG master
)
FetchContent_MakeAvailable(DaisySP)

if(MSVC)
    target_compile_definitions(DaisySP PUBLIC _USE_MATH_DEFINES)
    target_compile_options(DaisySP PRIVATE /FI"${CMAKE_CURRENT_SOURCE_DIR}/daisysp_msvc_compat.h")
endif()
target_compile_definitions(DaisySP PUBLIC USE_DAISYSP_LGPL)
target_include_directories(DaisySP PUBLIC "${daisysp_SOURCE_DIR}/DaisySP-LGPL/Source")

# -------------------------------------------------------------------
# m8-files-cxx (song file I/O library)
# -------------------------------------------------------------------
add_subdirectory(third_party/m8-files-cxx)

# -------------------------------------------------------------------
# Engine library (no SDL) — used by m8_tests, m8_render, m8_analyze
# -------------------------------------------------------------------
# kissfft: C sources need the C language enabled
enable_language(C)
add_library(kissfft STATIC
    third_party/kissfft/kiss_fft.c
    third_party/kissfft/kiss_fftr.c
)
target_include_directories(kissfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft)
set_target_properties(kissfft PROPERTIES LINKER_LANGUAGE C)

add_library(m8_engine STATIC
    src/engine/Sequencer.cpp
    src/engine/SongCleanup.cpp
    src/engine/Engine.cpp
    src/engine/SynthVoice.cpp
    src/engine/SamplePool.cpp
    src/engine/SamplerEngine.cpp
    src/io/SongIO.cpp
    src/io/ScaleIO.cpp
    src/io/InstrumentIO.cpp
    src/io/RenderAudio.cpp
    src/io/BundleExport.cpp
    src/analysis/AudioMetrics.cpp
    # Was ${daisysp_SOURCE_DIR}/DaisySP-LGPL/Source/Effects/reverbsc.cpp.
    # Now a modified vendored copy: the stock class exposes only feedback and
    # damping, while the M8 also has ROOM SIZE / MOD DEPTH / MOD FREQ, which are
    # compile-time constants upstream. LGPL 2.1, change list in ReverbScM8.h.
    src/engine/ReverbScM8.cpp
    src/engine/braids/analog_oscillator.cc
    src/engine/braids/digital_oscillator.cc
    src/engine/braids/macro_oscillator.cc
    src/engine/braids/resources.cc
    src/engine/braids/quantizer.cc
    src/engine/stmlib/utils/random.cc
    src/engine/data/WavetableBank.cpp
)
target_include_directories(m8_engine PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)
target_link_libraries(m8_engine PUBLIC DaisySP m8_files_cpp kissfft)

# Add executable
if(BUILD_CLONE)
add_executable(m8_clone 
    src/main.cpp 
    src/ui/Renderer.cpp
    src/ui/ViewManager.cpp
    src/ui/FileBrowser.cpp
    src/ui/ConfirmationDialog.cpp
    src/ui/CharPicker.cpp
    src/ui/ScriptRunner.cpp
    src/io/ThemeIO.cpp
    src/ui/screens/song/SongScreen.cpp
    src/ui/screens/chain/ChainScreen.cpp
    src/ui/screens/phrase/PhraseScreen.cpp
    src/ui/screens/instrument/InstrumentScreen.cpp
    src/ui/screens/table/TableScreen.cpp
    src/ui/screens/project/ProjectScreen.cpp
    src/ui/screens/groove/GrooveScreen.cpp
    src/ui/screens/scale/ScaleScreen.cpp
    src/ui/screens/render/RenderScreen.cpp
    src/ui/screens/mods/ModScreen.cpp
    src/ui/screens/inst_pool/InstPoolScreen.cpp
    src/ui/screens/mixer/MixerScreen.cpp
    src/ui/screens/effects/EffectsScreen.cpp
    src/ui/screens/eq/EqScreen.cpp
    src/ui/screens/sample_editor/SampleEditorScreen.cpp
    src/ui/screens/system_settings/SystemSettingsScreen.cpp
    src/ui/screens/theme/ThemeScreen.cpp
)

# Link SDL3 and engine
target_link_libraries(m8_clone PRIVATE SDL3::SDL3 m8_engine)
target_include_directories(m8_clone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(m8_clone PRIVATE /FS)
endif()
endif()

# -------------------------------------------------------------------
# Offline renderer (no SDL)
# -------------------------------------------------------------------
add_executable(m8_render src/tools/main_render.cpp)
target_link_libraries(m8_render PRIVATE m8_engine)
target_include_directories(m8_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -------------------------------------------------------------------
# Audio analysis tool (no SDL)
# -------------------------------------------------------------------
add_executable(m8_analyze src/tools/main_analyze.cpp)
target_link_libraries(m8_analyze PRIVATE m8_engine)
target_include_directories(m8_analyze PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -------------------------------------------------------------------
# Spectral A/B comparison tool (no SDL) — M8_AUDIO_ANALYSIS_SPEC.md Part D
# -------------------------------------------------------------------
add_executable(m8_spectrum src/tools/main_spectrum.cpp)
target_link_libraries(m8_spectrum PRIVATE m8_engine)
target_include_directories(m8_spectrum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -------------------------------------------------------------------
# Probe generator (links m8_files_cpp only, no engine)
# -------------------------------------------------------------------
add_executable(m8_makeprobe src/tools/main_makeprobe.cpp)
target_link_libraries(m8_makeprobe PRIVATE m8_files_cpp)
target_include_directories(m8_makeprobe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/m8-files-cxx/src)

# -------------------------------------------------------------------
# Opening-song generator — writes songs/opening.m8s + its drum samples
# (no SDL). Regenerates the committed demo song from engine state.
# -------------------------------------------------------------------
add_executable(m8_makesong src/tools/main_makesong.cpp)
target_link_libraries(m8_makesong PRIVATE m8_engine)
target_include_directories(m8_makesong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Startup-song composer — authors songs/sunrise.m8s (data, not hard-coded).
add_executable(m8_composesong src/tools/main_composesong.cpp)
target_link_libraries(m8_composesong PRIVATE m8_engine)
target_include_directories(m8_composesong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -------------------------------------------------------------------
# Hardware capture tool (miniaudio header-only, no engine, no SDL)
# -------------------------------------------------------------------
# The USB-audio half of m8_capture, extracted so m8_watchcapture can share it
# rather than grow a second WAV writer and device picker. Audio only -- it is
# deliberately NOT part of m8_device, which is committed to "no audio".
add_library(m8_audiocap STATIC src/tools/audio/CaptureCore.cpp)
target_include_directories(m8_audiocap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools)
if(WIN32)
    target_link_libraries(m8_audiocap PUBLIC winmm ole32)
endif()

add_executable(m8_capture src/tools/main_capture.cpp)
target_link_libraries(m8_capture PRIVATE m8_audiocap)

# -------------------------------------------------------------------
# M8 device control library (serial + SLIP + ScreenGrid + primitives).
# No engine, no SDL, no audio — shared by m8_nav and the test suite.
# -------------------------------------------------------------------
add_library(m8_device STATIC
    src/tools/m8/M8Device.cpp
    src/tools/m8/LiveReader.cpp
    src/tools/m8/FlightRecorder.cpp
    src/tools/m8/NavPaths.cpp
    src/tools/m8/Primitives.cpp
    src/tools/m8/Gestures.cpp
    src/tools/m8/DeviceTheme.cpp
    src/tools/m8/DeviceScriptRunner.cpp
    src/tools/m8/Semantic.cpp
    src/tools/m8/Daemon.cpp
    src/tools/m8/UiCapture.cpp
)
target_include_directories(m8_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/tools)
# LiveReader runs the drain on its own thread. Still no engine, no SDL, no
# audio -- the invariant this library is held to is about those, not threads.
find_package(Threads REQUIRED)
target_link_libraries(m8_device PUBLIC Threads::Threads)

# -------------------------------------------------------------------
# Serial display decoder / browser navigator (M8_HARDWARE_TEST_SPEC.md
# §8.2b). Serial only -- no engine, no SDL, no audio.
# -------------------------------------------------------------------
add_executable(m8_nav src/tools/main_nav.cpp)
target_link_libraries(m8_nav PRIVATE m8_device)

# ---------------------------------------------------------------------------
# Cursor-chain crawler. Walks every stop a screen actually has and records the
# navigation graph, so the field maps can be diffed against the device instead
# of hand-typed. See M8_DRIVER_BUGS.md #20/#31.
# ---------------------------------------------------------------------------
add_executable(m8_crawl src/tools/main_crawl.cpp)
target_link_libraries(m8_crawl PRIVATE m8_device)

# ---------------------------------------------------------------------------
# Edit-walk cursor watcher. Replays the gesture editValue sends, one press at a
# time, sampling the cursor between presses -- the reproduction step
# M8_DRIVER_BUGS.md #34 says it needs. Serial only.
# ---------------------------------------------------------------------------
add_executable(m8_editwatch src/tools/main_editwatch.cpp)
target_link_libraries(m8_editwatch PRIVATE m8_device)

# ---------------------------------------------------------------------------
# Watched audio capture: records USB audio while decoding the screen, and
# aborts on transport inversion or field drift instead of producing a number
# that has to be thrown away later. Links both halves -- m8_device for the
# display, m8_audiocap for the audio.
# ---------------------------------------------------------------------------
add_executable(m8_watchcapture src/tools/main_watchcapture.cpp)
target_link_libraries(m8_watchcapture PRIVATE m8_device m8_audiocap)

# ---------------------------------------------------------------------------
# Live-read hardware diagnostic. Proves on the device what
# tests/test_live_reader.cpp proves against a fake: a settle-gated read cannot
# complete while the transport runs, and the live reader can. Serial only.
# ---------------------------------------------------------------------------
add_executable(m8_livecheck src/tools/main_livecheck.cpp)
target_link_libraries(m8_livecheck PRIVATE m8_device)

# ---------------------------------------------------------------------------
# Device-vs-golden diff checker. Runs a script on the device, dumps the
# final screen as JSON, and compares against a golden reference.
# ---------------------------------------------------------------------------
add_executable(m8_diffcheck src/tools/main_diffcheck.cpp)
target_link_libraries(m8_diffcheck PRIVATE m8_device)

# -----------------------------------------------------------------------------
# Tests Target (m8_tests)
# -----------------------------------------------------------------------------
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(m8_tests
    tests/test_main.cpp
    tests/test_tempo.cpp
    tests/test_sequencer_walk.cpp
    tests/test_fx.cpp
    tests/test_fx_rev_freeze_modrate.cpp
    tests/test_groove.cpp
    tests/test_commands.cpp
    tests/test_sample_pool.cpp
    tests/test_sample_preview.cpp
    tests/test_repitch.cpp
    tests/test_crawl_check.cpp
    tests/test_flight_recorder.cpp
    tests/test_sampler.cpp
    tests/test_modulation.cpp
    tests/test_rt_safety.cpp
    tests/test_demo_verify.cpp
    tests/test_persistence.cpp
    tests/test_audio.cpp
    tests/test_ui_scripts.cpp
    tests/test_device_fake.cpp
    tests/test_live_reader.cpp
    tests/test_field_guard.cpp
    tests/test_macrosynth.cpp
    tests/test_instrument_io.cpp
    tests/test_fmsynth.cpp
    tests/test_fm_pit.cpp
    tests/test_hypersynth.cpp
    tests/test_wavsynth.cpp
    tests/test_tables.cpp
    tests/test_device_decode.cpp
    tests/test_ui_fuzz.cpp
    tests/test_file_browser.cpp
    tests/test_confirmation_dialog.cpp
    tests/test_char_picker.cpp
    tests/test_project_screen.cpp
    tests/test_scale_screen.cpp
    tests/test_render_screen.cpp
    tests/test_bundle_export.cpp
    tests/test_clear_phrases.cpp
    tests/test_output_stage.cpp
    tests/test_inst_pool_screen.cpp
    tests/test_mixer_bus.cpp
    tests/test_eq.cpp
    tests/test_system_settings.cpp
    src/io/ThemeIO.cpp
    src/ui/FileBrowser.cpp
    src/ui/ConfirmationDialog.cpp
    src/ui/CharPicker.cpp
    src/ui/Renderer.cpp
    src/ui/ViewManager.cpp
    src/ui/screens/project/ProjectScreen.cpp
    src/ui/screens/groove/GrooveScreen.cpp
    src/ui/screens/scale/ScaleScreen.cpp
    src/ui/screens/table/TableScreen.cpp
    src/ui/screens/render/RenderScreen.cpp
    src/ui/screens/inst_pool/InstPoolScreen.cpp
    src/ui/screens/eq/EqScreen.cpp
    src/ui/screens/sample_editor/SampleEditorScreen.cpp
    src/ui/screens/system_settings/SystemSettingsScreen.cpp
    src/ui/screens/theme/ThemeScreen.cpp
    tests/support/OfflineHost.cpp
    tests/support/AllocGuard.cpp
)

target_link_libraries(m8_tests PRIVATE Catch2::Catch2WithMain m8_engine m8_device SDL3::SDL3)

# The [ui] suite shells out to three executables by hardcoded path
# (tests/test_ui_scripts.cpp:33-35) and CMake knew nothing about it, so
# `--target m8_tests` could run a freshly built suite against binaries nobody
# had rebuilt -- and then report a confident verdict about the wrong thing.
# That is not hypothetical: on 2026-08-18 a stale m8_clone failed three golden
# assertions that were not broken, and once it was rebuilt, live_vs_offline
# compared the new clone against a day-old m8_render and reported the engine
# and the offline renderer disagreeing by 1/4096 when they do not.
# Shelling out is fine; doing it without a build-order edge is not.
add_dependencies(m8_tests m8_render m8_analyze)
if(BUILD_CLONE)
    add_dependencies(m8_tests m8_clone)
endif()
target_include_directories(m8_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(m8_tests PRIVATE THIRD_PARTY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(MSVC)
    target_compile_options(m8_tests PRIVATE /FS /bigobj)
endif()

if(BUILD_CLONE)
add_custom_command(TARGET m8_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:m8_tests> $<TARGET_FILE_DIR:m8_tests>
    COMMAND_EXPAND_LISTS
)
endif()

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(m8_tests)

# Copy SDL3.dll to output directory
if(BUILD_CLONE)
add_custom_command(TARGET m8_clone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:m8_clone> $<TARGET_FILE_DIR:m8_clone>
    COMMAND_EXPAND_LISTS
)
endif()



