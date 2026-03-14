# modules/vpx/CMakeLists.txt — VP8/VP9 codec module (mod_vpx.so)

add_library(mod_vpx SHARED
  vpx_codec.cpp
  vpx_codec_factory.cpp
  mod_vpx.cpp
)

target_link_libraries(mod_vpx PRIVATE
  iora_codecs_core
  iora_lib
  vpx
)

target_include_directories(mod_vpx PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_BINARY_DIR}/external/include"
)

set_target_properties(mod_vpx PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
