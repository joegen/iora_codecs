# modules/g729/CMakeLists.txt — G.729 codec module (mod_g729.so)
#
# GPL-3.0 notice: This target links bcg729 (GPL-3.0). The resulting
# mod_g729.so binary is GPL-3.0. All other iora_codecs targets remain
# under their permissive licenses.

add_library(mod_g729 SHARED
  g729_codec.cpp
  g729_codec_factory.cpp
  mod_g729.cpp
)

target_link_libraries(mod_g729 PRIVATE
  iora_codecs_core
  iora_lib
  bcg729-static
)

target_include_directories(mod_g729 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_SOURCE_DIR}/libs/bcg729/include"
)

set_target_properties(mod_g729 PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
