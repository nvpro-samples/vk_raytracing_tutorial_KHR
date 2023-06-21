cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)
project(vk_raytracing_tutorial)

#--------------------------------------------------------------------------------------------------
# look for nvpro_core 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet
if(NOT BASE_DIRECTORY)

  find_path(BASE_DIRECTORY
    NAMES nvpro_core/cmake/setup.cmake
    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../.. 
    REQUIRED
    DOC "Directory containing nvpro_core"
    )
endif()

## Various functions and macros REQUIRED
if(EXISTS ${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/nvpro_core/cmake/utilities.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing nvpro_core")
endif()

set(TUTO_KHR_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    add_definitions(/wd26812)  # 'enum class' over 'enum'
    add_definitions(/wd26451)  # Arithmetic overflow, casting 4 byte value to 8 byte value
endif()

#--------------------------------------------------------------------------------------------------
# Package shared by all projects
_add_package_VulkanSDK()
_add_package_ImGUI()
_add_nvpro_core_lib()

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
add_subdirectory(ray_tracing_specialization)
add_subdirectory(ray_tracing_advanced_compilation)
add_subdirectory(ray_tracing_motionblur)


#--------------------------------------------------------------------------------------------------
# Install - copying the media directory
install(DIRECTORY "media" 
        CONFIGURATIONS Release 
        DESTINATION "bin_${ARCH}")
install(DIRECTORY "media" 
        CONFIGURATIONS Debug 
        DESTINATION "bin_${ARCH}_debug")