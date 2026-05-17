# RtTutorial.cmake - Common CMake functions for Vulkan Raytracing Tutorial samples
#
# This file provides a reusable function to set up tutorial samples with consistent
# configuration, reducing duplication across individual CMakeLists.txt files.
#
# Usage:
#   setup_rt_tutorial_sample(
#     [USE_RT_COMMON]                    # Include RT common sources (default: OFF)
#     [USE_FOUNDATION_SHADER]            # Include foundation.slang (default: OFF)
#     [SHADER_HEADERS]                   # Include .h.slang files as shader dependencies (default: OFF)
#     [SHADER_INCLUDE_DIRS <dirs>]       # Additional -I directories for shader compilation
#     [CAPABILITIES <cap1> <cap2>...]    # Extra SPIR-V capabilities (clean names, e.g. spvRayQueryKHR)
#     [LINK_LIBRARIES <lib1> <lib2>...]  # Extra libraries to link
#     [COPY_FILES <files>]               # Additional files to copy alongside executable
#     [COPY_DIRS <dirs>]                 # Additional directories to copy
#   )
#
# Sets in parent scope:
#   RT_TUTORIAL_TARGET - The CMake target name (directory name) for post-call customization

function(setup_rt_tutorial_sample)
    # Parse function arguments
    set(options USE_RT_COMMON USE_FOUNDATION_SHADER SHADER_HEADERS)
    set(oneValueArgs)
    set(multiValueArgs SHADER_INCLUDE_DIRS CAPABILITIES LINK_LIBRARIES COPY_FILES COPY_DIRS)
    cmake_parse_arguments(RT_TUTORIAL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Get the name of the current directory
    get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    project(${PROJECT_NAME})
    message(STATUS "Processing: ${PROJECT_NAME}")

    # Adding all sources
    file(GLOB EXE_SOURCES "*.cpp" "*.hpp" "*.md")
    source_group("Source Files" FILES ${EXE_SOURCES})

    # Handle RT common sources if requested
    set(ALL_SOURCES ${EXE_SOURCES})
    if(RT_TUTORIAL_USE_RT_COMMON)
        set(RT_COMMON_DIR "${TUTO_DIR}/common")
        file(GLOB RT_COMMON_SOURCES "${RT_COMMON_DIR}/*.cpp" "${RT_COMMON_DIR}/*.hpp")
        source_group("RtTutorial Common" FILES ${RT_COMMON_SOURCES})
        list(APPEND ALL_SOURCES ${RT_COMMON_SOURCES})
    endif()

    # Add the executable
    add_executable(${PROJECT_NAME} ${ALL_SOURCES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "RtTutorial")

    # Base libraries (consistent across all samples)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        nvpro2::nvapp           # The application framework
        nvpro2::nvgui           # The GUI framework
        nvpro2::nvslang         # Slang functions
        nvpro2::nvutils         # Utility functions
        nvpro2::nvvk            # Vulkan functions
        nvpro2::nvshaders_host  # Shader host functions
        nvpro2::nvaftermath     # Aftermath functions
        vk_raytracing_tutorial_common # Common functions
        stb                     # Image loading
    )

    if(RT_TUTORIAL_LINK_LIBRARIES)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${RT_TUTORIAL_LINK_LIBRARIES})
    endif()

    add_project_definitions(${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${ROOT_DIR})

    #------------------------------------------------------------------------------------------------------------------------------
    # Compile shaders
    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/_autogen")
    file(GLOB SHADER_GLSL_FILES "shaders/*.glsl")
    file(GLOB SHADER_SLANG_FILES "shaders/*.slang")

    if(RT_TUTORIAL_SHADER_HEADERS)
        file(GLOB SHADER_H_FILES "shaders/*.h" "shaders/*.h.slang")
        list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")
    else()
        file(GLOB SHADER_H_FILES "shaders/*.h")
    endif()

    # Standard shaders included by all samples
    list(APPEND SHADER_SLANG_FILES
        ${NVSHADERS_DIR}/nvshaders/sky_simple.slang
        ${NVSHADERS_DIR}/nvshaders/tonemapper.slang
    )

    if(RT_TUTORIAL_USE_FOUNDATION_SHADER)
        list(APPEND SHADER_SLANG_FILES ${COMMON_DIR}/shaders/foundation.slang)
    endif()

    # Build shader include flags
    set(SHADER_INCLUDE_FLAGS "-I${NVSHADERS_DIR}" "-I${ROOT_DIR}")
    if(RT_TUTORIAL_SHADER_INCLUDE_DIRS)
        foreach(include_dir ${RT_TUTORIAL_SHADER_INCLUDE_DIRS})
            list(APPEND SHADER_INCLUDE_FLAGS "-I${include_dir}")
        endforeach()
    endif()

    # Base capabilities required by standard shaders (e.g., tonemapper)
    set(ALL_CAPABILITIES spvGroupNonUniformBallot spvGroupNonUniformArithmetic spvGroupNonUniform)
    list(APPEND ALL_CAPABILITIES ${RT_TUTORIAL_CAPABILITIES})

    compile_slang(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        HEADERS_VAR GENERATED_SHADER_SLANG_HEADERS
        OPTIMIZATION_LEVEL 0
        DEBUG_LEVEL 2
        CAPABILITIES ${ALL_CAPABILITIES}
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )

    compile_glsl(
        "${SHADER_GLSL_FILES}"
        "${SHADER_OUTPUT_DIR}"
        GENERATED_SHADER_GLSL_HEADERS
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )

    # Add shader files to the project
    source_group("Shaders" FILES ${SHADER_SLANG_FILES} ${SHADER_GLSL_FILES} ${SHADER_H_FILES})
    source_group("Shaders/Compiled" FILES ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS})

    # Add the output shader headers (target) directly to the executable
    # This allow to compile the shaders when the executable is built
    target_sources(${PROJECT_NAME} PRIVATE ${SHADER_SLANG_FILES} ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS} ${SHADER_H_FILES})

    #------------------------------------------------------------------------------------------------------------------------------
    # Installation, copy files

    set(FILES_TO_COPY ${NsightAftermath_DLLS})
    if(RT_TUTORIAL_COPY_FILES)
        list(APPEND FILES_TO_COPY ${RT_TUTORIAL_COPY_FILES})
    endif()

    set(DIRS_TO_COPY)
    if(RT_TUTORIAL_COPY_DIRS)
        list(APPEND DIRS_TO_COPY ${RT_TUTORIAL_COPY_DIRS})
    endif()

    copy_to_runtime_and_install(${PROJECT_NAME}
        FILES ${FILES_TO_COPY} ${Slang_GLSLANG}
        DIRECTORIES ${DIRS_TO_COPY}
        LOCAL_DIRS "${CMAKE_CURRENT_LIST_DIR}/shaders"
        AUTO
    )

    # Expose target name for post-call customization
    set(RT_TUTORIAL_TARGET ${PROJECT_NAME} PARENT_SCOPE)
endfunction()
