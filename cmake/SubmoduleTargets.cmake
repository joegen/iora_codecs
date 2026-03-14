# SubmoduleTargets.cmake — ExternalProject_Add wrappers and thin CMake targets
# for non-CMake submodules (autotools, Meson, Makefile-based)
#
# Each wrapper produces an IMPORTED target with correct include/library paths.

include(ExternalProject)

set(IORA_CODECS_EXTERNAL_PREFIX "${CMAKE_BINARY_DIR}/external" CACHE PATH "Install prefix for ExternalProject builds")
file(MAKE_DIRECTORY "${IORA_CODECS_EXTERNAL_PREFIX}/include")
file(MAKE_DIRECTORY "${IORA_CODECS_EXTERNAL_PREFIX}/lib")

# ============================================================================
# libspeexdsp — Option B: compile just the resampler .c file directly
# This avoids autotools entirely and is much faster.
# ============================================================================
if(TRUE)  # speexdsp resampler is always built — required for sample rate conversion
  set(SPEEXDSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/speexdsp")
  if(EXISTS "${SPEEXDSP_DIR}/libspeexdsp/resample.c")
    # Generate speexdsp_config_types.h in the build tree
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/speexdsp_config_types.h"
      "${CMAKE_BINARY_DIR}/speexdsp_generated/speexdsp_config_types.h"
      COPYONLY
    )

    add_library(speexdsp STATIC
      "${SPEEXDSP_DIR}/libspeexdsp/resample.c"
    )
    target_include_directories(speexdsp
      PUBLIC
        "${SPEEXDSP_DIR}/include"
        "${CMAKE_BINARY_DIR}/speexdsp_generated"
      PRIVATE
        "${SPEEXDSP_DIR}/include/speex"
        "${SPEEXDSP_DIR}/libspeexdsp"
        "${CMAKE_BINARY_DIR}/speexdsp_generated"
    )
    target_compile_definitions(speexdsp
      PRIVATE
        FLOATING_POINT
        EXPORT=
        RANDOM_PREFIX=speex
        OUTSIDE_SPEEX
        HAVE_STDINT_H
    )
    set_target_properties(speexdsp PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_STANDARD 99
    )
    message(STATUS "  -> libspeexdsp: compiled resampler directly (no autotools)")
  else()
    message(WARNING "libs/speexdsp not found. Run: git submodule update --init --recursive")
  endif()
endif()

# ============================================================================
# libg722 — compile .c files directly (no build system in upstream)
# ============================================================================
if(IORA_CODECS_ENABLE_G722)
  set(G722_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/libg722")
  if(EXISTS "${G722_DIR}")
    file(GLOB G722_SOURCES "${G722_DIR}/src/*.c")
    if(G722_SOURCES)
      add_library(g722 STATIC ${G722_SOURCES})
      target_include_directories(g722 PUBLIC "${G722_DIR}/src")
      set_target_properties(g722 PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99
      )
      message(STATUS "  -> libg722: compiled sources directly")
    else()
      # Fallback: root-level layout — list sources explicitly to exclude test.c
      set(G722_SOURCES_ROOT
        "${G722_DIR}/g722_encode.c"
        "${G722_DIR}/g722_decode.c"
      )
      set(_g722_found TRUE)
      foreach(_src IN LISTS G722_SOURCES_ROOT)
        if(NOT EXISTS "${_src}")
          set(_g722_found FALSE)
        endif()
      endforeach()
      if(_g722_found)
        add_library(g722 STATIC ${G722_SOURCES_ROOT})
        target_include_directories(g722 PUBLIC "${G722_DIR}")
        set_target_properties(g722 PROPERTIES
          POSITION_INDEPENDENT_CODE ON
          C_STANDARD 99
        )
        message(STATUS "  -> libg722: compiled sources directly (root layout)")
      else()
        message(WARNING "IORA_CODECS_ENABLE_G722=ON but no .c sources found in libs/libg722/")
      endif()
    endif()
  else()
    message(WARNING "IORA_CODECS_ENABLE_G722=ON but libs/libg722 not found. Run: git submodule update --init --recursive")
  endif()
endif()

# ============================================================================
# opencore-amr — autotools via ExternalProject_Add
# ============================================================================
if(IORA_CODECS_ENABLE_AMR)
  set(OPENCORE_AMR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/opencore-amr")
  if(EXISTS "${OPENCORE_AMR_DIR}")
    ExternalProject_Add(opencore-amr-build
      SOURCE_DIR "${OPENCORE_AMR_DIR}"
      CONFIGURE_COMMAND
        cd "${OPENCORE_AMR_DIR}" &&
        autoreconf -if &&
        "${OPENCORE_AMR_DIR}/configure"
          --prefix=${IORA_CODECS_EXTERNAL_PREFIX}
          --enable-static
          --disable-shared
          --with-pic
      BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND make install
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopencore-amrnb.a"
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopencore-amrwb.a"
    )

    # Create imported targets
    add_library(opencore-amrnb STATIC IMPORTED GLOBAL)
    set_target_properties(opencore-amrnb PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopencore-amrnb.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(opencore-amrnb opencore-amr-build)

    add_library(opencore-amrwb STATIC IMPORTED GLOBAL)
    set_target_properties(opencore-amrwb PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopencore-amrwb.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(opencore-amrwb opencore-amr-build)

    message(STATUS "  -> opencore-amr: ExternalProject (autotools)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_AMR=ON but libs/opencore-amr not found.")
  endif()

  # vo-amrwbenc — autotools via ExternalProject_Add
  set(VO_AMRWBENC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/vo-amrwbenc")
  if(EXISTS "${VO_AMRWBENC_DIR}")
    ExternalProject_Add(vo-amrwbenc-build
      SOURCE_DIR "${VO_AMRWBENC_DIR}"
      CONFIGURE_COMMAND
        cd "${VO_AMRWBENC_DIR}" &&
        autoreconf -if &&
        "${VO_AMRWBENC_DIR}/configure"
          --prefix=${IORA_CODECS_EXTERNAL_PREFIX}
          --enable-static
          --disable-shared
          --with-pic
      BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND make install
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libvo-amrwbenc.a"
    )

    add_library(vo-amrwbenc STATIC IMPORTED GLOBAL)
    set_target_properties(vo-amrwbenc PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libvo-amrwbenc.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(vo-amrwbenc vo-amrwbenc-build)

    message(STATUS "  -> vo-amrwbenc: ExternalProject (autotools)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_AMR=ON but libs/vo-amrwbenc not found.")
  endif()
endif()

# ============================================================================
# OpenH264 — Make via ExternalProject_Add
# ============================================================================
if(IORA_CODECS_ENABLE_H264)
  set(OPENH264_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/openh264")
  if(EXISTS "${OPENH264_DIR}")
    ExternalProject_Add(openh264-build
      SOURCE_DIR "${OPENH264_DIR}"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND
        make -C "${OPENH264_DIR}"
          PREFIX=${IORA_CODECS_EXTERNAL_PREFIX}
          BUILDTYPE=Release
          ENABLE_SHARED=NO
          -j${CMAKE_BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND
        make -C "${OPENH264_DIR}"
          PREFIX=${IORA_CODECS_EXTERNAL_PREFIX}
          install-static
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopenh264.a"
    )

    add_library(openh264 STATIC IMPORTED GLOBAL)
    set_target_properties(openh264 PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libopenh264.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(openh264 openh264-build)

    message(STATUS "  -> OpenH264: ExternalProject (Make)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_H264=ON but libs/openh264 not found.")
  endif()
endif()

# ============================================================================
# libvpx — configure/make via ExternalProject_Add
# Note: NASM recommended for x86/x86_64 asm optimizations
# ============================================================================
if(IORA_CODECS_ENABLE_VPX)
  set(LIBVPX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/libvpx")
  if(EXISTS "${LIBVPX_DIR}")
    ExternalProject_Add(libvpx-build
      SOURCE_DIR "${LIBVPX_DIR}"
      CONFIGURE_COMMAND
        "${LIBVPX_DIR}/configure"
          --prefix=${IORA_CODECS_EXTERNAL_PREFIX}
          --enable-static
          --disable-shared
          --enable-pic
          --disable-examples
          --disable-tools
          --disable-docs
          --disable-unit-tests
      BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND make install
      BUILD_IN_SOURCE ON
      BUILD_BYPRODUCTS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libvpx.a"
    )

    add_library(vpx STATIC IMPORTED GLOBAL)
    set_target_properties(vpx PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libvpx.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(vpx libvpx-build)

    message(STATUS "  -> libvpx: ExternalProject (configure/make)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_VPX=ON but libs/libvpx not found.")
  endif()
endif()

# ============================================================================
# libaom — ExternalProject_Add (add_subdirectory pollutes CMAKE_C_FLAGS with
# -Werror flags that break other submodules)
# ============================================================================
if(IORA_CODECS_ENABLE_AV1)
  set(LIBAOM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/libaom")
  if(EXISTS "${LIBAOM_DIR}/CMakeLists.txt")
    set(LIBAOM_BUILD_DIR "${CMAKE_BINARY_DIR}/libaom-build")
    ExternalProject_Add(libaom-build
      SOURCE_DIR "${LIBAOM_DIR}"
      BINARY_DIR "${LIBAOM_BUILD_DIR}"
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -S "${LIBAOM_DIR}" -B "${LIBAOM_BUILD_DIR}"
          -DCMAKE_INSTALL_PREFIX=${IORA_CODECS_EXTERNAL_PREFIX}
          -DCMAKE_BUILD_TYPE=Release
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON
          -DAOM_BUILD_EXAMPLES=OFF
          -DAOM_BUILD_APPS=OFF
          -DAOM_BUILD_TESTING=OFF
          -DENABLE_DOCS=OFF
          -DENABLE_TESTS=OFF
          -DBUILD_SHARED_LIBS=OFF
      BUILD_COMMAND
        ${CMAKE_COMMAND} --build "${LIBAOM_BUILD_DIR}" -j${CMAKE_BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND
        ${CMAKE_COMMAND} --install "${LIBAOM_BUILD_DIR}"
      BUILD_IN_SOURCE OFF
      BUILD_BYPRODUCTS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libaom.a"
    )

    add_library(aom STATIC IMPORTED GLOBAL)
    set_target_properties(aom PROPERTIES
      IMPORTED_LOCATION "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libaom.a"
      INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
    )
    add_dependencies(aom libaom-build)

    message(STATUS "  -> libaom: ExternalProject (CMake, isolated build)")
  else()
    message(WARNING "IORA_CODECS_ENABLE_AV1=ON but libs/libaom not found.")
  endif()
endif()

# ============================================================================
# dav1d — Meson via ExternalProject_Add (requires meson + ninja)
# ============================================================================
if(IORA_CODECS_ENABLE_AV1)
  set(DAV1D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/dav1d")
  if(EXISTS "${DAV1D_DIR}")
    find_program(MESON_EXECUTABLE meson)
    find_program(NINJA_EXECUTABLE ninja)
    if(MESON_EXECUTABLE AND NINJA_EXECUTABLE)
      set(DAV1D_BUILD_DIR "${CMAKE_BINARY_DIR}/dav1d-build")
      ExternalProject_Add(dav1d-build
        SOURCE_DIR "${DAV1D_DIR}"
        CONFIGURE_COMMAND
          ${MESON_EXECUTABLE} setup "${DAV1D_BUILD_DIR}" "${DAV1D_DIR}"
            --prefix=${IORA_CODECS_EXTERNAL_PREFIX}
            --default-library=static
            -Denable_tests=false
            -Denable_tools=false
        BUILD_COMMAND
          ${MESON_EXECUTABLE} compile -C "${DAV1D_BUILD_DIR}"
        INSTALL_COMMAND
          ${MESON_EXECUTABLE} install -C "${DAV1D_BUILD_DIR}"
        BUILD_IN_SOURCE OFF
        BUILD_BYPRODUCTS
          "${IORA_CODECS_EXTERNAL_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}dav1d${CMAKE_STATIC_LIBRARY_SUFFIX}"
      )

      # dav1d/meson may install to lib/ or lib/x86_64-linux-gnu/ depending on platform
      set(DAV1D_LIB_SEARCH_PATHS
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib"
        "${IORA_CODECS_EXTERNAL_PREFIX}/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      )
      set(DAV1D_LIB_PATH "${IORA_CODECS_EXTERNAL_PREFIX}/lib/libdav1d.a")
      foreach(_dir IN LISTS DAV1D_LIB_SEARCH_PATHS)
        if(EXISTS "${_dir}/libdav1d.a")
          set(DAV1D_LIB_PATH "${_dir}/libdav1d.a")
          break()
        endif()
      endforeach()

      add_library(dav1d STATIC IMPORTED GLOBAL)
      set_target_properties(dav1d PROPERTIES
        IMPORTED_LOCATION "${DAV1D_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${IORA_CODECS_EXTERNAL_PREFIX}/include"
      )
      add_dependencies(dav1d dav1d-build)

      message(STATUS "  -> dav1d: ExternalProject (Meson)")
    else()
      message(WARNING "IORA_CODECS_ENABLE_AV1=ON but meson/ninja not found. dav1d cannot be built.")
    endif()
  else()
    message(WARNING "IORA_CODECS_ENABLE_AV1=ON but libs/dav1d not found.")
  endif()
endif()
