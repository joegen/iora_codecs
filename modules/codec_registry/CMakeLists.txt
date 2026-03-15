# modules/codec_registry/CMakeLists.txt — CodecRegistry plugin shared library.

add_library(mod_codec_registry SHARED
  mod_codec_registry.cpp
)

target_link_libraries(mod_codec_registry PRIVATE
  iora_codecs_core
  iora_lib
)

target_include_directories(mod_codec_registry PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
)

set_target_properties(mod_codec_registry PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
