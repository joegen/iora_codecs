# modules/g722/CMakeLists.txt — G.722 codec module (mod_g722.so)

add_library(mod_g722 SHARED
  g722_codec.cpp
  g722_codec_factory.cpp
  mod_g722.cpp
)

target_link_libraries(mod_g722 PRIVATE
  iora_codecs_core
  iora_lib
  g722
)

target_include_directories(mod_g722 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
)

set_target_properties(mod_g722 PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
