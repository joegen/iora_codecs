# tests/CMakeLists.txt — Tests for iora_codecs

# ==========================================================================
# Catch2 v3 via FetchContent
# ==========================================================================
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# ==========================================================================
# Core abstractions unit tests (Catch2)
# ==========================================================================
add_executable(core_tests
  core/test_sample_format.cpp
  core/test_pixel_format.cpp
  core/test_codec_info.cpp
  core/test_media_buffer.cpp
  core/test_media_clock.cpp
  core/test_codec_registry.cpp
)
target_link_libraries(core_tests PRIVATE iora_codecs_core Catch2::Catch2WithMain)
target_include_directories(core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

include(Catch)
catch_discover_tests(core_tests)

# ==========================================================================
# Registry + G.711 plugin integration test (loads mod_registry then mod_g711)
# ==========================================================================
if(IORA_CODECS_ENABLE_G711)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(registry_plugin_tests
    codec/test_registry_plugin.cpp
  )
  target_link_libraries(registry_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(registry_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(registry_plugin_tests mod_registry mod_g711)
  catch_discover_tests(registry_plugin_tests)
endif()

# ==========================================================================
# G.711 codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_G711)
  add_executable(g711_tests
    codec/test_g711_codec.cpp
  )
  target_link_libraries(g711_tests PRIVATE iora_codecs_core Catch2::Catch2WithMain)
  target_include_directories(g711_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
  )
  catch_discover_tests(g711_tests)

  # G.711 plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(g711_plugin_tests
    codec/test_g711_plugin.cpp
  )
  target_link_libraries(g711_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(g711_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(g711_plugin_tests mod_g711)
  catch_discover_tests(g711_plugin_tests)
endif()

# ==========================================================================
# Opus codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_OPUS)
  add_executable(opus_tests
    codec/test_opus_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/opus/opus_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/opus/opus_codec_factory.cpp
  )
  target_link_libraries(opus_tests PRIVATE iora_codecs_core opus Catch2::Catch2WithMain)
  target_include_directories(opus_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
  )
  catch_discover_tests(opus_tests)

  # Opus plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(opus_plugin_tests
    codec/test_opus_plugin.cpp
  )
  target_link_libraries(opus_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(opus_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(opus_plugin_tests mod_opus)
  catch_discover_tests(opus_plugin_tests)
endif()

# ==========================================================================
# G.722 codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_G722)
  add_executable(g722_tests
    codec/test_g722_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/g722/g722_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/g722/g722_codec_factory.cpp
  )
  target_link_libraries(g722_tests PRIVATE iora_codecs_core g722 Catch2::Catch2WithMain)
  target_include_directories(g722_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
  )
  catch_discover_tests(g722_tests)

  # G.722 plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(g722_plugin_tests
    codec/test_g722_plugin.cpp
  )
  target_link_libraries(g722_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(g722_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(g722_plugin_tests mod_g722)
  catch_discover_tests(g722_plugin_tests)
endif()

# ==========================================================================
# iLBC codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_ILBC)
  add_executable(ilbc_tests
    codec/test_ilbc_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/ilbc/ilbc_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/ilbc/ilbc_codec_factory.cpp
  )
  target_link_libraries(ilbc_tests PRIVATE iora_codecs_core ilbc Catch2::Catch2WithMain)
  target_include_directories(ilbc_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_SOURCE_DIR}/libs/libilbc"
    "${CMAKE_BINARY_DIR}/libs/libilbc"
  )
  catch_discover_tests(ilbc_tests)

  # iLBC plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(ilbc_plugin_tests
    codec/test_ilbc_plugin.cpp
  )
  target_link_libraries(ilbc_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(ilbc_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(ilbc_plugin_tests mod_ilbc)
  catch_discover_tests(ilbc_plugin_tests)
endif()

# ==========================================================================
# AMR codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_AMR)
  add_executable(amr_tests
    codec/test_amr_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/amr/amr_nb_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/amr/amr_wb_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/amr/amr_nb_codec_factory.cpp
    ${CMAKE_SOURCE_DIR}/modules/amr/amr_wb_codec_factory.cpp
  )
  target_link_libraries(amr_tests PRIVATE
    iora_codecs_core
    opencore-amrnb
    opencore-amrwb
    vo-amrwbenc
    Catch2::Catch2WithMain
  )
  target_include_directories(amr_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_BINARY_DIR}/external/include"
  )
  catch_discover_tests(amr_tests)

  # AMR plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(amr_plugin_tests
    codec/test_amr_plugin.cpp
  )
  target_link_libraries(amr_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(amr_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(amr_plugin_tests mod_amr)
  catch_discover_tests(amr_plugin_tests)
endif()

# ==========================================================================
# H.264 codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_H264)
  add_executable(h264_tests
    codec/test_h264_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/h264/h264_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/h264/h264_codec_factory.cpp
  )
  target_link_libraries(h264_tests PRIVATE
    iora_codecs_core
    openh264
    pthread
    Catch2::Catch2WithMain
  )
  target_include_directories(h264_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_BINARY_DIR}/external/include"
  )
  catch_discover_tests(h264_tests)

  # H.264 plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(h264_plugin_tests
    codec/test_h264_plugin.cpp
  )
  target_link_libraries(h264_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(h264_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(h264_plugin_tests mod_h264)
  catch_discover_tests(h264_plugin_tests)
endif()

# ==========================================================================
# VP8/VP9 codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_VPX)
  add_executable(vpx_tests
    codec/test_vpx_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/vpx/vpx_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/vpx/vpx_codec_factory.cpp
  )
  target_link_libraries(vpx_tests PRIVATE
    iora_codecs_core
    vpx
    pthread
    Catch2::Catch2WithMain
  )
  target_include_directories(vpx_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_BINARY_DIR}/external/include"
  )
  catch_discover_tests(vpx_tests)

  # VPX plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(vpx_plugin_tests
    codec/test_vpx_plugin.cpp
  )
  target_link_libraries(vpx_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(vpx_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(vpx_plugin_tests mod_vpx)
  catch_discover_tests(vpx_plugin_tests)
endif()

# ==========================================================================
# AV1 codec unit tests (Catch2)
# ==========================================================================
if(IORA_CODECS_ENABLE_AV1)
  find_package(Threads REQUIRED)
  add_executable(av1_tests
    codec/test_av1_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/av1/av1_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/av1/av1_codec_factory.cpp
  )
  target_link_libraries(av1_tests PRIVATE
    iora_codecs_core
    aom
    dav1d
    Threads::Threads
    Catch2::Catch2WithMain
  )
  target_include_directories(av1_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_BINARY_DIR}/external/include"
  )
  catch_discover_tests(av1_tests)

  # AV1 plugin integration test (loads .so via IoraService)
  find_package(OpenSSL REQUIRED)
  add_executable(av1_plugin_tests
    codec/test_av1_plugin.cpp
  )
  target_link_libraries(av1_plugin_tests PRIVATE
    iora_codecs_core
    iora_lib
    Catch2::Catch2WithMain
    OpenSSL::SSL OpenSSL::Crypto
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )
  target_include_directories(av1_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
  )
  add_dependencies(av1_plugin_tests mod_av1)
  catch_discover_tests(av1_plugin_tests)
endif()

# ==========================================================================
# DSP unit tests (Catch2)
# ==========================================================================
add_executable(dsp_tests
  dsp/test_resampler.cpp
  dsp/test_audio_mixer.cpp
  dsp/test_gain.cpp
  dsp/test_vad.cpp
  dsp/test_tone_generator.cpp
  dsp/test_goertzel_detector.cpp
  dsp/test_wav_writer.cpp
  dsp/test_wav_reader.cpp
  dsp/test_dsp_pipeline_integration.cpp
)
target_link_libraries(dsp_tests PRIVATE iora_codecs_core Catch2::Catch2WithMain)
target_include_directories(dsp_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

catch_discover_tests(dsp_tests)

# ==========================================================================
# Pipeline unit tests (Catch2) — requires G.711 + Opus
# ==========================================================================
if(IORA_CODECS_ENABLE_G711 AND IORA_CODECS_ENABLE_OPUS)
  add_executable(pipeline_tests
    pipeline/test_transcoding_handler.cpp
    ${CMAKE_SOURCE_DIR}/modules/opus/opus_codec.cpp
    ${CMAKE_SOURCE_DIR}/modules/opus/opus_codec_factory.cpp
  )
  target_link_libraries(pipeline_tests PRIVATE iora_codecs_core opus Catch2::Catch2WithMain)
  target_include_directories(pipeline_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/modules"
  )
  catch_discover_tests(pipeline_tests)
endif()

# ==========================================================================
# Audio mixer pipeline tests (Catch2) — no codec dependency
# ==========================================================================
add_executable(mixer_pipeline_tests
  pipeline/test_audio_mixer_handler.cpp
)
target_link_libraries(mixer_pipeline_tests PRIVATE iora_codecs_core Catch2::Catch2WithMain)
target_include_directories(mixer_pipeline_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(mixer_pipeline_tests)

# ==========================================================================
# Media pipeline unit tests (Catch2) — no codec dependency
# ==========================================================================
add_executable(media_pipeline_tests
  pipeline/test_media_pipeline.cpp
)
target_link_libraries(media_pipeline_tests PRIVATE iora_codecs_core Catch2::Catch2WithMain)
target_include_directories(media_pipeline_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
catch_discover_tests(media_pipeline_tests)

# ==========================================================================
# Build smoke test (submodule linkage verification)
# ==========================================================================

add_executable(build_smoke_test build_smoke_test.cpp)

target_link_libraries(build_smoke_test PRIVATE iora_lib speexdsp)

# Pass IORA_CODECS_ENABLE_* defines to the test so #ifdef guards work
if(IORA_CODECS_ENABLE_OPUS)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_OPUS)
  target_link_libraries(build_smoke_test PRIVATE opus)
endif()

if(IORA_CODECS_ENABLE_G722)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_G722)
  target_link_libraries(build_smoke_test PRIVATE g722)
  target_include_directories(build_smoke_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../libs/libg722")
endif()

if(IORA_CODECS_ENABLE_ILBC)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_ILBC)
  target_link_libraries(build_smoke_test PRIVATE ilbc)
  target_include_directories(build_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/libilbc"
    "${CMAKE_BINARY_DIR}/libs/libilbc"
  )
endif()

if(IORA_CODECS_ENABLE_AMR)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_AMR)
  target_link_libraries(build_smoke_test PRIVATE opencore-amrnb opencore-amrwb vo-amrwbenc)
  target_include_directories(build_smoke_test PRIVATE "${CMAKE_BINARY_DIR}/external/include")
endif()

if(IORA_CODECS_ENABLE_H264)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_H264)
  target_link_libraries(build_smoke_test PRIVATE openh264 pthread)
  target_include_directories(build_smoke_test PRIVATE "${CMAKE_BINARY_DIR}/external/include")
endif()

if(IORA_CODECS_ENABLE_VPX)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_VPX)
  target_link_libraries(build_smoke_test PRIVATE vpx pthread)
  target_include_directories(build_smoke_test PRIVATE "${CMAKE_BINARY_DIR}/external/include")
endif()

if(IORA_CODECS_ENABLE_AV1)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_AV1)
  target_link_libraries(build_smoke_test PRIVATE aom dav1d pthread)
  target_include_directories(build_smoke_test PRIVATE "${CMAKE_BINARY_DIR}/external/include")
endif()

if(IORA_CODECS_ENABLE_G711)
  target_compile_definitions(build_smoke_test PRIVATE IORA_CODECS_ENABLE_G711)
endif()

add_test(NAME build_smoke_test COMMAND build_smoke_test)
