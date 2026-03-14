# modules/g711/CMakeLists.txt — G.711 codec plugin shared library.

add_library(mod_g711 SHARED
  mod_g711.cpp
)

target_link_libraries(mod_g711 PRIVATE
  iora_codecs_core
  iora_lib
)

target_include_directories(mod_g711 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
)

set_target_properties(mod_g711 PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
