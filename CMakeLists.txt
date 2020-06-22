cmake_minimum_required(VERSION 2.8)
project(vk_raytracing_tutorial)

#####################################################################################
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find CMakeLists_include.txt yet
#
if(NOT BASE_DIRECTORY) # if not defined, it means this cmake file was called as the first entry point and not included
  # check if the external repository is outside or inside the project (as a sub-module)
  # testing the file CMakeLists_include.txt because when sub-modules are not cloned, the folders are still there...
  # we also assume here that if shared_sources is there, shared_external is, too...
  SET(BASE_DIRECTORY "" CACHE FILEPATH "folder containing shared_sources")
  SET(ADD_SUBDIR_BELOW 1)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  else()
  endif()
endif(NOT BASE_DIRECTORY)

if(EXISTS ${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
  INCLUDE(${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
else()
  Message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing shared_sources")
endif()
_add_package_VulkanSDK()
_add_package_OpenGL()
_add_package_ImGUI()
_add_package_ZLIB()
_add_shared_sources_lib()

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

