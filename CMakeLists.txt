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
# Engine library (no SDL) — used by m8_tests, m8_render
# -------------------------------------------------------------------
add_library(m8_engine STATIC
    src/engine/Sequencer.cpp
    src/engine/Engine.cpp
    src/engine/SynthVoice.cpp
    src/engine/SamplePool.cpp
    src/engine/SamplerEngine.cpp
    ${daisysp_SOURCE_DIR}/DaisySP-LGPL/Source/Effects/reverbsc.cpp
)
target_include_directories(m8_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(m8_engine PUBLIC DaisySP)

# Add executable
if(BUILD_CLONE)
add_executable(m8_clone 
    src/main.cpp 
    src/ui/Renderer.cpp
    src/ui/ViewManager.cpp
    src/ui/FileBrowser.cpp
    src/ui/screens/song/SongScreen.cpp
    src/ui/screens/chain/ChainScreen.cpp
    src/ui/screens/phrase/PhraseScreen.cpp
    src/ui/screens/instrument/InstrumentScreen.cpp
    src/ui/screens/table/TableScreen.cpp
    src/ui/screens/project/ProjectScreen.cpp
    src/ui/screens/groove/GrooveScreen.cpp
    src/ui/screens/scale/ScaleScreen.cpp
    src/ui/screens/mods/ModScreen.cpp
    src/ui/screens/inst_pool/InstPoolScreen.cpp
    src/ui/screens/mixer/MixerScreen.cpp
    src/ui/screens/effects/EffectsScreen.cpp
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

# -----------------------------------------------------------------------------
# Tests Target (m8_tests)
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(m8_tests
    tests/test_main.cpp
    tests/test_tempo.cpp
    tests/test_sequencer_walk.cpp
    tests/test_fx.cpp
    tests/test_groove.cpp
    tests/test_commands.cpp
    tests/test_sample_pool.cpp
    tests/test_sampler.cpp
    tests/test_modulation.cpp
    tests/test_rt_safety.cpp
    tests/test_demo_verify.cpp
    tests/support/OfflineHost.cpp
    tests/support/AllocGuard.cpp
)

target_link_libraries(m8_tests PRIVATE Catch2::Catch2WithMain m8_engine)
target_include_directories(m8_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(m8_tests PRIVATE /FS /bigobj)
endif()

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



