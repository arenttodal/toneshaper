cmake_minimum_required(VERSION 3.22)

project(ToneShaper VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TONESHAPER_BUILD_PLUGIN "Build the JUCE plugin (AU / VST3 / Standalone)" ON)
option(TONESHAPER_BUILD_TESTS "Build the DSP test suite" ON)

# ---------------------------------------------------------------------------
# DSP core: plain C++17 + PFFFT, no framework dependency.
# ---------------------------------------------------------------------------
add_library(toneshaper_dsp STATIC
    third_party/pffft/pffft.c
    src/dsp/RealFFT.cpp
    src/dsp/LogGrid.cpp
    src/dsp/MinPhase.cpp
    src/dsp/SpectralCore.cpp
    src/dsp/ToneShaperEngine.cpp)

target_include_directories(toneshaper_dsp PUBLIC src/dsp third_party/pffft)
set_target_properties(toneshaper_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT MSVC)
    target_compile_options(toneshaper_dsp PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_libraries(toneshaper_dsp PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(TONESHAPER_BUILD_TESTS)
    enable_testing()
    add_executable(toneshaper_tests tests/DspTests.cpp)
    target_link_libraries(toneshaper_tests PRIVATE toneshaper_dsp)
    add_test(NAME dsp COMMAND toneshaper_tests)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(TONESHAPER_BUILD_PLUGIN)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)

    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    add_subdirectory(src/plugin)
endif()
