# modules/opus/CMakeLists.txt — Opus codec module (mod_opus.so)

add_library(mod_opus SHARED
  opus_codec.cpp
  opus_codec_factory.cpp
  mod_opus.cpp
)

target_link_libraries(mod_opus PRIVATE
  iora_codecs_core
  iora_lib
  opus
)

target_include_directories(mod_opus PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
)

set_target_properties(mod_opus PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
