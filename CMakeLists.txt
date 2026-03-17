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
# Framework: Iora (find_package with submodule fallback)
# ==========================================================================
# Strategy:
#   1. Try find_package(Iora) — works when iora is installed system-wide
#      or via CMAKE_PREFIX_PATH from a superproject build.
#   2. Fall back to building from libs/iora submodule for standalone dev.
#
# Either path provides the same two targets:
#   iora_lib  — INTERFACE library (headers + libiora-core.so linkage + IORA_CORE_SHARED)
#   iora_core — SHARED library (singleton state for RTLD_LOCAL plugin loading)
# ==========================================================================
find_package(Iora QUIET)

if(Iora_FOUND)
  # find_package(Iora) exports Iora::iora_lib and Iora::iora_core.
  # Alias to unnamespaced names so all downstream CMakeLists.txt work unchanged.
  if(NOT TARGET iora_lib)
    add_library(iora_lib ALIAS Iora::iora_lib)
  endif()
  if(NOT TARGET iora_core)
    add_library(iora_core ALIAS Iora::iora_core)
  endif()
  message(STATUS "  -> iora: found via find_package(Iora) [${Iora_DIR}]")

elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/include/iora/iora.hpp")
  # Submodule fallback: build iora_core and iora_lib from the submodule.
  # Cannot use add_subdirectory(libs/iora/src/core) because its CMakeLists.txt
  # references ${CMAKE_SOURCE_DIR}/include which resolves to our root.
  find_package(Threads REQUIRED)
  find_package(OpenSSL REQUIRED)

  add_library(iora_core SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/src/core/iora_core.cpp"
  )
  target_include_directories(iora_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/include"
  )
  target_compile_features(iora_core PUBLIC cxx_std_17)
  target_link_libraries(iora_core PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
  )
  target_compile_definitions(iora_core PRIVATE IORA_CORE_BUILDING)
  set_target_properties(iora_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
  )

  add_library(iora_lib INTERFACE)
  target_include_directories(iora_lib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/iora/include"
  )
  target_compile_features(iora_lib INTERFACE cxx_std_17)
  target_link_libraries(iora_lib INTERFACE iora_core)
  target_compile_definitions(iora_lib INTERFACE IORA_CORE_SHARED)
  message(STATUS "  -> iora: built from submodule (libs/iora)")

else()
  message(FATAL_ERROR
    "Iora framework not found. Either:\n"
    "  1. Install iora and set CMAKE_PREFIX_PATH, or\n"
    "  2. Run: git submodule update --init --recursive")
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
