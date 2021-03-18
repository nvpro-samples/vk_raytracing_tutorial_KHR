cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)
project(vk_raytracing_tutorial)

#--------------------------------------------------------------------------------------------------
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet
if(NOT BASE_DIRECTORY)

  find_path(BASE_DIRECTORY
    NAMES shared_sources/cmake/setup.cmake
    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../.. 
    REQUIRED
    DOC "Directory containing shared_sources"
    )
endif()

## Various functions and macros REQUIRED
if(EXISTS ${BASE_DIRECTORY}/shared_sources/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/shared_sources/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/shared_sources/cmake/utilities.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing shared_sources")
endif()

set(TUTO_KHR_DIR ${CMAKE_CURRENT_SOURCE_DIR})


#--------------------------------------------------------------------------------------------------
# Package shared by all projects
_add_package_VulkanSDK()
_add_package_ImGUI()
_add_shared_sources_lib()

message(STATUS "COPY ${CMAKE_CURRENT_SOURCE_DIR}/media  to  ${OUTPUT_PATH}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/media DESTINATION ${OUTPUT_PATH})


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
add_subdirectory(ray_tracing_ao)
add_subdirectory(ray_tracing_indirect_scissor)



#--------------------------------------------------------------------------------------------------
# Install - copying the media directory
install(DIRECTORY "media" 
        CONFIGURATIONS Release 
        DESTINATION "bin_${ARCH}")
install(DIRECTORY "media" 
        CONFIGURATIONS Debug 
        DESTINATION "bin_${ARCH}_debug")