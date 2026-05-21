cmake_minimum_required(VERSION 3.22...3.31)
project(vk_raytracing_tutorial VERSION 2.0 LANGUAGES C CXX)

# Here we can set the Slang version to use (uncomment to override the default)
# set(Slang_VERSION "2025.20" CACHE STRING "Version of Slang to use")

# Add the cmake folder to the module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Configure NvproCore2 options
set(NVPRO2_ENABLE_nvimageformats off)
set(NVPRO2_ENABLE_nvsight off)
set(NVPRO2_ENABLE_nvvkglsl off)
set(NVPRO2_ENABLE_nvvkgltf off)
# Adding NvproCore2
find_package(NvproCore2 REQUIRED)

# Include the RtTutorial common functions
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtTutorial.cmake")

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(TUTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/raytrace_tutorial")



# Add common library first as other samples might depend on it
add_subdirectory(common)

add_subdirectory(raytrace_tutorial/01_foundation)
add_subdirectory(raytrace_tutorial/02_basic)
add_subdirectory(raytrace_tutorial/02_basic_nvvk)
add_subdirectory(raytrace_tutorial/03_any_hit)
add_subdirectory(raytrace_tutorial/04_jitter_camera)
add_subdirectory(raytrace_tutorial/05_shadow_miss)
add_subdirectory(raytrace_tutorial/06_reflection)
add_subdirectory(raytrace_tutorial/07_multi_closest_hit)
add_subdirectory(raytrace_tutorial/08_intersection)
add_subdirectory(raytrace_tutorial/09_motion_blur)
add_subdirectory(raytrace_tutorial/10_position_fetch)
add_subdirectory(raytrace_tutorial/11_shader_execution_reorder)
add_subdirectory(raytrace_tutorial/12_infinite_plane)
add_subdirectory(raytrace_tutorial/13_callable_shader)
add_subdirectory(raytrace_tutorial/14_animation)
add_subdirectory(raytrace_tutorial/15_micro_maps_opacity)
add_subdirectory(raytrace_tutorial/16_ray_query)
add_subdirectory(raytrace_tutorial/17_ray_query_screenspace)
add_subdirectory(raytrace_tutorial/18_swept_spheres)
add_subdirectory(raytrace_tutorial/19_ray_differentials)
add_subdirectory(raytrace_tutorial/20_wireframe)
add_subdirectory(raytrace_tutorial/XX_template)

# Make Visual Studio use this project as the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT 01_foundation)

# Install the resources and nvshaders folders
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources" DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/common/shaders" DESTINATION ${CMAKE_INSTALL_PREFIX}/common)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/common/io_gltf.h" DESTINATION ${CMAKE_INSTALL_PREFIX}/common)
install(DIRECTORY "${NVSHADERS_DIR}/nvshaders" DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${Slang_GLSLANG} DESTINATION ${CMAKE_INSTALL_PREFIX})


