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
add_library(score_addon_vkfrt
        fulldome_voxel/Executor.hpp
        fulldome_voxel/Metadata.hpp
        fulldome_voxel/Process.hpp
        fulldome_voxel/Layer.hpp
        fulldome_voxel/Executor.cpp
        fulldome_voxel/Process.cpp
        fulldome_voxel/Node.hpp
        fulldome_voxel/Node.cpp

        fulldome_voxel/vk_raytracing/vk_voxel_raytracing.hpp
        fulldome_voxel/vk_raytracing/vk_voxel_raytracing.cpp

  "${3RDPARTY_FOLDER}/miniply/miniply.cpp"


        score_addon_vkfrt.hpp
        score_addon_vkfrt.cpp

)

qt_add_resources(vkfrt_resources fulldome_voxel/vk_raytracing/shaders/shaders.qrc)
target_sources(score_addon_vkfrt PRIVATE ${vkfrt_resources})


# Link
target_link_libraries(score_addon_vkfrt PUBLIC
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
setup_score_plugin(score_addon_vkfrt)

