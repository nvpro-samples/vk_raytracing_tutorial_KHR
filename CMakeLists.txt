cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)
project(vk_raytracing_tutorial)

#--------------------------------------------------------------------------------------------------
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find CMakeLists_include.txt yet
if(NOT BASE_DIRECTORY)
   SET(BASE_DIRECTORY "" CACHE FILEPATH "folder containing shared_sources")
  SET(ADD_SUBDIR_BELOW 1)
  find_path(BASE_DIRECTORY2
    NAMES shared_sources
    PATHS  ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}
    REQUIRED
    DOC "Couldn't find shared_source directory'"
    )
  SET(BASE_DIRECTORY ${BASE_DIRECTORY2} )
endif()

## Various functions and macros REQUIRED
include(${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
set(TUTO_KHR_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# presets and utility functions used in all CMakeLists
include(utilities.cmake)

#--------------------------------------------------------------------------------------------------
# Package shared by all projects
_add_package_VulkanSDK()
_add_package_OpenGL()
_add_package_ImGUI()
_add_package_ZLIB()
_add_shared_sources_lib()

message(STATUS "COPY ${CMAKE_CURRENT_SOURCE_DIR}/media  to  ${EXECUTABLE_OUTPUT_PATH}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/media DESTINATION ${EXECUTABLE_OUTPUT_PATH})


#--------------------------------------------------------------------------------------------------
# Sub examples
add_subdirectory(ray_tracing__advance) 
add_subdirectory(ray_tracing__before) 
add_subdirectory(ray_tracing__simple)

add_subdirectory(ray_tracing_animation)
add_subdirectory(ray_tracing_anyhit)
add_subdirectory(ray_tracing_callable)
add_subdirectory(ray_tracing_gltf)
add_subdirectory(ray_tracing_instances)
add_subdirectory(ray_tracing_intersection)
add_subdirectory(ray_tracing_jitter_cam)
add_subdirectory(ray_tracing_manyhits)
add_subdirectory(ray_tracing_rayquery)
add_subdirectory(ray_tracing_reflections)

add_subdirectory(ray_tracing_indirect_scissor)
