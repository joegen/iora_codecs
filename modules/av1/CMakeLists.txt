# modules/av1/CMakeLists.txt — AV1 codec module (mod_av1.so)

find_package(Threads REQUIRED)

add_library(mod_av1 SHARED
  av1_codec.cpp
  av1_codec_factory.cpp
  mod_av1.cpp
)

target_link_libraries(mod_av1 PRIVATE
  iora_codecs_core
  iora_lib
  aom
  dav1d
  Threads::Threads
)

target_include_directories(mod_av1 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_BINARY_DIR}/external/include"
)

set_target_properties(mod_av1 PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
