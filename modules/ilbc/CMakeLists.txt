# modules/ilbc/CMakeLists.txt — iLBC codec module (mod_ilbc.so)

add_library(mod_ilbc SHARED
  ilbc_codec.cpp
  ilbc_codec_factory.cpp
  mod_ilbc.cpp
)

target_link_libraries(mod_ilbc PRIVATE
  iora_codecs_core
  iora_lib
  ilbc
)

target_include_directories(mod_ilbc PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_SOURCE_DIR}/libs/libilbc"
  "${CMAKE_BINARY_DIR}/libs/libilbc"
)

set_target_properties(mod_ilbc PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
