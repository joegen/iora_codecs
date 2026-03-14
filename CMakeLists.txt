cmake_minimum_required(VERSION 3.14)

project(iora_codecs
  VERSION 0.1.0
  LANGUAGES C CXX
  DESCRIPTION "General-purpose audio/video codec library built on the Iora framework"
)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code for all targets (needed for shared libs)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default parallel level for ExternalProject builds
if(NOT CMAKE_BUILD_PARALLEL_LEVEL)
  include(ProcessorCount)
  ProcessorCount(NPROC)
  if(NPROC EQUAL 0)
    set(NPROC 1)
  endif()
  set(CMAKE_BUILD_PARALLEL_LEVEL ${NPROC})
endif()

# ==========================================================================
# Codec enable/disable options
# ==========================================================================
include(cmake/CodecModules.cmake)

# ==========================================================================
# Framework: Iora (header-only interface library)
# ==========================================================================
# Iora's own CMakeLists.txt is not designed for add_subdirectory (uses
# CMAKE_SOURCE_DIR, always builds the iora executable, fetches Catch2).
# Since iora_lib is an INTERFACE header-only library, we create our own
# target pointing directly at the headers. This avoids pulling in iora's
# executable build, OpenSSL dependency (for the executable), and FetchContent
# downloads (Catch2) that are not needed for iora_codecs.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/include/iora/iora.hpp")
  add_library(iora_lib INTERFACE)
  target_include_directories(iora_lib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/include"
  )
  target_compile_features(iora_lib INTERFACE cxx_std_17)
  message(STATUS "  -> iora: header-only interface library (libs/iora/include)")
else()
  message(WARNING "libs/iora not found. Run: git submodule update --init --recursive")
endif()

# ==========================================================================
# Non-CMake submodule targets (ExternalProject + direct compilation)
# ==========================================================================
include(cmake/SubmoduleTargets.cmake)

# ==========================================================================
# iora_codecs_core — Core abstractions (types, interfaces, registry)
# ==========================================================================
add_library(iora_codecs_core STATIC
  src/codec/codec_registry.cpp
  src/dsp/resampler.cpp
  src/dsp/audio_mixer.cpp
  src/pipeline/transcoding_handler.cpp
  src/pipeline/audio_mixer_handler.cpp
  src/pipeline/stage_metrics.cpp
  src/pipeline/media_pipeline.cpp
)
target_include_directories(iora_codecs_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(iora_codecs_core PUBLIC iora_lib)
target_link_libraries(iora_codecs_core PRIVATE speexdsp)
set_target_properties(iora_codecs_core PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

# ==========================================================================
# Codec modules (shared libraries)
# ==========================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/CMakeLists.txt")
  add_subdirectory(modules)
endif()

# ==========================================================================
# Tests
# ==========================================================================
if(IORA_CODECS_BUILD_TESTS)
  enable_testing()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  endif()
endif()
