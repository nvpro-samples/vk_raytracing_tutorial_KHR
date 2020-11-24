#*****************************************************************************
# Copyright 2020 NVIDIA Corporation. All rights reserved.
#*****************************************************************************

cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)

#--------------------------------------------------------------------------------------------------
# Project setting
get_filename_component(PROJNAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
SET(PROJNAME vk_${PROJNAME}_KHR)
Project(${PROJNAME})
message(STATUS "-------------------------------")
message(STATUS "Processing Project ${PROJNAME}:")


#--------------------------------------------------------------------------------------------------
# C++ target and defines
set(CMAKE_CXX_STANDARD 17)
add_executable(${PROJNAME})
_add_project_definitions(${PROJNAME})


#--------------------------------------------------------------------------------------------------
# Source files for this project
#
file(GLOB SOURCE_FILES *.cpp *.hpp *.inl *.h *.c)
file(GLOB EXTRA_COMMON ${TUTO_KHR_DIR}/common/*.*)
list(APPEND COMMON_SOURCE_FILES ${EXTRA_COMMON})
include_directories(${TUTO_KHR_DIR}/common)


#--------------------------------------------------------------------------------------------------
# GLSL to SPIR-V custom build
#
SET(VULKAN_TARGET_ENV vulkan1.2)
UNSET(GLSL_SOURCES)
UNSET(SPV_OUTPUT)
file(GLOB_RECURSE GLSL_HEADER_FILES "shaders/*.h" "shaders/*.glsl")
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "shaders/*.comp"
    "shaders/*.frag"
    "shaders/*.vert"
    "shaders/*.rchit"
    "shaders/*.rahit"
    "shaders/*.rint"
    "shaders/*.rmiss"
    "shaders/*.rgen"
    "shaders/*.rcall"
    )
foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    _compile_GLSL(${GLSL} "shaders/${FILE_NAME}.spv" GLSL_SOURCES SPV_OUTPUT)
endforeach(GLSL)
list(APPEND GLSL_SOURCES ${GLSL_HEADER_FILES})


#--------------------------------------------------------------------------------------------------
# Sources
target_sources(${PROJNAME} PUBLIC ${SOURCE_FILES} ${HEADER_FILES})
target_sources(${PROJNAME} PUBLIC ${COMMON_SOURCE_FILES})
target_sources(${PROJNAME} PUBLIC ${PACKAGE_SOURCE_FILES})
target_sources(${PROJNAME} PUBLIC ${GLSL_SOURCES})


#--------------------------------------------------------------------------------------------------
# Sub-folders in Visual Studio
#
source_group("Common"       FILES ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES})
source_group("Sources"      FILES ${SOURCE_FILES})
source_group("Headers"      FILES ${HEADER_FILES})
source_group("Shader_Files" FILES ${GLSL_SOURCES})


#--------------------------------------------------------------------------------------------------
# Linkage
#
target_link_libraries(${PROJNAME} ${PLATFORM_LIBRARIES} shared_sources)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${PROJNAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${PROJNAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

#--------------------------------------------------------------------------------------------------
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#
_copy_binaries_to_target( ${PROJNAME} )


#install(FILES ${SPV_OUTPUT} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}/shaders")
#install(FILES ${SPV_OUTPUT} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}/shaders")
#install(FILES ${CUBIN_SOURCES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}")
#install(FILES ${CUBIN_SOURCES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}")
#install(DIRECTORY "../media" CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}")
#install(DIRECTORY "../media" CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}")

#----------------------------------------------------------------------------------------------------
# Copying elements
# Media
# target_copy_to_output_dir(TARGET ${PROJECT_NAME} FILES "${TUTO_KHR_DIR}/media")
# Spir-V Shaders
target_copy_to_output_dir(
  TARGET ${PROJECT_NAME} 
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  DEST_SUBFOLDER "${PROJECT_NAME}/"
  FILES ${SPV_OUTPUT}
  )

