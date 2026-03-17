# CodecModules.cmake — Per-codec build enable/disable options
#
# Each IORA_CODECS_ENABLE_* option controls whether the corresponding
# submodule is built and the codec module is available at runtime.

# Audio codecs — mandatory (always ON)
option(IORA_CODECS_ENABLE_OPUS  "Build Opus codec module"   ON)
option(IORA_CODECS_ENABLE_G711  "Build G.711 codec module (built-in, always recommended)" ON)

# Audio codecs — ON by default
option(IORA_CODECS_ENABLE_G722  "Build G.722 codec module"  ON)
option(IORA_CODECS_ENABLE_ILBC  "Build iLBC codec module"   ON)
option(IORA_CODECS_ENABLE_AMR   "Build AMR-NB/WB codec module (patent encumbered — Via Licensing patent license required for commercial deployment)" OFF)
option(IORA_CODECS_ENABLE_G729  "Build G.729 codec module (GPL-3.0 via bcg729 — GPL scope confined to mod_g729.so only)" OFF)

# Video codecs — ON by default
option(IORA_CODECS_ENABLE_H264  "Build H.264 codec module (OpenH264)" ON)
option(IORA_CODECS_ENABLE_VPX   "Build VP8/VP9 codec module"          ON)

# Video codecs — OFF by default
option(IORA_CODECS_ENABLE_AV1   "Build AV1 codec module (libaom + dav1d)" OFF)

# Build options
option(IORA_CODECS_BUILD_TESTS    "Build test executables"  ON)
option(IORA_CODECS_BUILD_EXAMPLES "Build example programs"  OFF)

# --- Print configuration summary ---
message(STATUS "=== iora_codecs codec configuration ===")
message(STATUS "  Opus:    ${IORA_CODECS_ENABLE_OPUS}")
message(STATUS "  G.711:   ${IORA_CODECS_ENABLE_G711}")
message(STATUS "  G.722:   ${IORA_CODECS_ENABLE_G722}")
message(STATUS "  iLBC:    ${IORA_CODECS_ENABLE_ILBC}")
message(STATUS "  AMR:     ${IORA_CODECS_ENABLE_AMR}")
message(STATUS "  G.729:   ${IORA_CODECS_ENABLE_G729} (GPL-3.0 — opt-in only)")
message(STATUS "  H.264:   ${IORA_CODECS_ENABLE_H264}")
message(STATUS "  VP8/VP9: ${IORA_CODECS_ENABLE_VPX}")
message(STATUS "  AV1:     ${IORA_CODECS_ENABLE_AV1}")
message(STATUS "========================================")

# --- Integrate CMake-native submodules via add_subdirectory ---

# libopus (CMake-native)
if(IORA_CODECS_ENABLE_OPUS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/opus/CMakeLists.txt")
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/opus)
    message(STATUS "  -> libopus: added via add_subdirectory")
  else()
    message(WARNING "IORA_CODECS_ENABLE_OPUS=ON but libs/opus not found. Run: git submodule update --init --recursive")
  endif()
endif()

# libilbc (CMake-native)
if(IORA_CODECS_ENABLE_ILBC)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/libilbc/CMakeLists.txt")
    set(_ilbc_saved_shared_libs ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/libilbc)
    # libilbc's abseil-cpp uses [[nodiscard]] in C headers — requires C23 / gnu2x
    set_property(TARGET ilbc PROPERTY C_STANDARD 23)
    set(BUILD_SHARED_LIBS ${_ilbc_saved_shared_libs} CACHE BOOL "" FORCE)
    message(STATUS "  -> libilbc: added via add_subdirectory")
  else()
    message(WARNING "IORA_CODECS_ENABLE_ILBC=ON but libs/libilbc not found. Run: git submodule update --init --recursive")
  endif()
endif()

# bcg729 (CMake-native, GPL-3.0 — isolated to mod_g729.so)
if(IORA_CODECS_ENABLE_G729)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/bcg729/CMakeLists.txt")
    set(ENABLE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_STRICT OFF CACHE BOOL "" FORCE)
    set(_bcg729_saved_static ${ENABLE_STATIC})
    set(_bcg729_saved_shared ${ENABLE_SHARED})
    set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
    set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/bcg729)
    set(ENABLE_STATIC ${_bcg729_saved_static} CACHE BOOL "" FORCE)
    set(ENABLE_SHARED ${_bcg729_saved_shared} CACHE BOOL "" FORCE)
    message(STATUS "  -> bcg729: added via add_subdirectory (GPL-3.0, static lib)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_G729=ON but libs/bcg729 not found. Run: git submodule update --init --recursive")
  endif()
endif()

# libaom — ExternalProject_Add (add_subdirectory pollutes global CMAKE_C_FLAGS
# with -Werror, breaking other submodules)
# Handled in SubmoduleTargets.cmake

# --- Non-CMake submodules are handled in SubmoduleTargets.cmake ---
# libspeexdsp, libg722, opencore-amr, vo-amrwbenc, OpenH264, libvpx, dav1d
