# modules/h264/CMakeLists.txt — H.264 codec module (mod_h264.so)

add_library(mod_h264 SHARED
  h264_codec.cpp
  h264_codec_factory.cpp
  mod_h264.cpp
)

target_link_libraries(mod_h264 PRIVATE
  iora_codecs_core
  iora_lib
  openh264
)

target_include_directories(mod_h264 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_BINARY_DIR}/external/include"
)

set_target_properties(mod_h264 PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
