cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

if(NOT TARGET score_lib_base)
  include("${SCORE_SOURCE_DIR}/cmake/ScoreExternalAddon.cmake")
endif()

find_package(Vulkan REQUIRED)

if(NOT TARGET score_plugin_gfx)
  message("Requires gfx plug-in")
  return()
endif()

project(score_addon_qvkrt LANGUAGES CXX)

# General initialization
score_common_setup()

# Creation of the library
add_library(score_addon_qvkrt
  QVKRT/Executor.hpp
  QVKRT/Metadata.hpp
  QVKRT/Process.hpp
  QVKRT/Layer.hpp
  QVKRT/Executor.cpp
  QVKRT/Process.cpp
  QVKRT/Node.hpp
  QVKRT/Node.cpp

  QVKRT/rt/rt.hpp
  QVKRT/rt/rt.cpp

  "${3RDPARTY_FOLDER}/miniply/miniply.cpp"

        QVKRT/load_ply.hpp
        QVKRT/load_ply.cpp


        QVKRT/Ply.hpp
        QVKRT/Ply.cpp

        QVKRT/TinyObj.hpp
        QVKRT/TinyObj.cpp


        QVKRT/PlyLoader.hpp
        QVKRT/PlyLoader.cpp

  score_addon_qvkrt.hpp
  score_addon_qvkrt.cpp

)

qt_add_resources(qvkrt_resources QVKRT/rt/shaders/shaders.qrc)
target_sources(score_addon_qvkrt PRIVATE ${qvkrt_resources})


# Link
target_link_libraries(score_addon_qvkrt PUBLIC
  score_plugin_gfx
  score_plugin_engine
  Vulkan::Vulkan
  score_plugin_avnd
)

target_include_directories(
        score_plugin_threedim SYSTEM
        PRIVATE "${3RDPARTY_FOLDER}/libssynth/src" "${3RDPARTY_FOLDER}/vcglib"
        "${3RDPARTY_FOLDER}/eigen" "${3RDPARTY_FOLDER}/miniply")

# Target-specific options
setup_score_plugin(score_addon_qvkrt)

