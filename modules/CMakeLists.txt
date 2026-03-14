# modules/CMakeLists.txt — Router for codec module subdirectories.

if(IORA_CODECS_ENABLE_G711)
  add_subdirectory(g711)
endif()

if(IORA_CODECS_ENABLE_OPUS)
  add_subdirectory(opus)
endif()

if(IORA_CODECS_ENABLE_G722)
  add_subdirectory(g722)
endif()

if(IORA_CODECS_ENABLE_ILBC)
  add_subdirectory(ilbc)
endif()

if(IORA_CODECS_ENABLE_AMR)
  add_subdirectory(amr)
endif()

if(IORA_CODECS_ENABLE_H264)
  add_subdirectory(h264)
endif()

if(IORA_CODECS_ENABLE_VPX)
  add_subdirectory(vpx)
endif()

if(IORA_CODECS_ENABLE_AV1)
  add_subdirectory(av1)
endif()
