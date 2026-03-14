# modules/amr/CMakeLists.txt — AMR codec module (mod_amr.so)

add_library(mod_amr SHARED
  amr_nb_codec.cpp
  amr_wb_codec.cpp
  amr_nb_codec_factory.cpp
  amr_wb_codec_factory.cpp
  mod_amr.cpp
)

target_link_libraries(mod_amr PRIVATE
  iora_codecs_core
  iora_lib
  opencore-amrnb
  opencore-amrwb
  vo-amrwbenc
)

target_include_directories(mod_amr PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/libs/iora/include"
  "${CMAKE_BINARY_DIR}/external/include"
)

set_target_properties(mod_amr PROPERTIES
  PREFIX ""
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
