# RtTutorial.cmake - Common CMake functions for Vulkan Raytracing Tutorial samples
#
# This file provides a reusable function to set up tutorial samples with consistent
# configuration, reducing duplication across individual CMakeLists.txt files.
#
# Usage:
#   setup_rt_tutorial_sample(
#     [USE_RT_COMMON]                    # Include RT common sources (default: OFF)
#     [USE_FOUNDATION_SHADER]            # Include foundation.slang (default: OFF)
#     [EXTRA_SHADER_INCLUDES <dirs>]     # Additional shader include directories
#     [EXTRA_COPY_FILES <files>]         # Additional files to copy
#     [EXTRA_COPY_DIRECTORIES <dirs>]    # Additional directories to copy
#     [INCLUDE_H_SLANG_FILES]            # Include .h.slang files in shader compilation
#   )

function(setup_rt_tutorial_sample)
    # Parse function arguments
    set(options USE_RT_COMMON USE_FOUNDATION_SHADER INCLUDE_H_SLANG_FILES)
    set(oneValueArgs)
    set(multiValueArgs EXTRA_SHADER_INCLUDES EXTRA_COPY_FILES EXTRA_COPY_DIRECTORIES)
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
        # Define RT common directory
        set(RT_COMMON_DIR "${TUTO_DIR}/common")
        
        # Add common files to make them visible in Visual Studio
        file(GLOB RT_COMMON_SOURCES "${RT_COMMON_DIR}/*.cpp" "${RT_COMMON_DIR}/*.hpp")
        source_group("RtTutorial Common" FILES ${RT_COMMON_SOURCES})
        list(APPEND ALL_SOURCES ${RT_COMMON_SOURCES})
    endif()

    # Add the executable
    add_executable(${PROJECT_NAME} ${ALL_SOURCES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "RtTutorial")

    # Link libraries and include directories (consistent across all samples)
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

    add_project_definitions(${PROJECT_NAME})

    # Include directory for generated files
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${ROOT_DIR})

    #------------------------------------------------------------------------------------------------------------------------------
    # Compile shaders
    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/_autogen")
    file(GLOB SHADER_GLSL_FILES "shaders/*.glsl")
    file(GLOB SHADER_SLANG_FILES "shaders/*.slang")
    
    # Handle .h.slang files if requested
    if(RT_TUTORIAL_INCLUDE_H_SLANG_FILES)
        file(GLOB SHADER_H_FILES "shaders/*.h" "shaders/*.h.slang")
        list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")
    else()
        file(GLOB SHADER_H_FILES "shaders/*.h")
    endif()

    # Adding standard shaders (consistent across all samples)
    list(APPEND SHADER_SLANG_FILES 
        ${NVSHADERS_DIR}/nvshaders/sky_simple.slang
        ${NVSHADERS_DIR}/nvshaders/tonemapper.slang
    )

    # Add foundation shader if requested
    if(RT_TUTORIAL_USE_FOUNDATION_SHADER)
        list(APPEND SHADER_SLANG_FILES ${COMMON_DIR}/shaders/foundation.slang)
    endif()

    # Build shader include flags
    set(SHADER_INCLUDE_FLAGS "-I${NVSHADERS_DIR}" "-I${ROOT_DIR}")
    if(RT_TUTORIAL_EXTRA_SHADER_INCLUDES)
        foreach(include_dir ${RT_TUTORIAL_EXTRA_SHADER_INCLUDES})
            list(APPEND SHADER_INCLUDE_FLAGS "-I${include_dir}")
        endforeach()
    endif()

    compile_slang(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        GENERATED_SHADER_HEADERS
        OPTIMIZATION_LEVEL 1
        DEBUG_LEVEL 1
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
    source_group("Shaders/Compiled" FILES ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS} ${GENERATED_SHADER_HEADERS})

    # Add the output shader headers (target) directly to the executable
    # This allow to compile the shaders when the executable is built
    target_sources(${PROJECT_NAME} PRIVATE ${SHADER_SLANG_FILES} ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS} ${SHADER_H_FILES})

    #------------------------------------------------------------------------------------------------------------------------------
    # Installation, copy files
    
    # Build copy files list
    set(COPY_FILES ${NsightAftermath_DLLS})
    if(RT_TUTORIAL_EXTRA_COPY_FILES)
        list(APPEND COPY_FILES ${RT_TUTORIAL_EXTRA_COPY_FILES})
    endif()

    # Build copy directories list
    set(COPY_DIRECTORIES)
    if(RT_TUTORIAL_EXTRA_COPY_DIRECTORIES)
        list(APPEND COPY_DIRECTORIES ${RT_TUTORIAL_EXTRA_COPY_DIRECTORIES})
    endif()

    # Copy files next to the executable
    copy_to_runtime_and_install(${PROJECT_NAME}
        FILES ${COPY_FILES} ${Slang_GLSLANG}
        DIRECTORIES ${COPY_DIRECTORIES}
        LOCAL_DIRS "${CMAKE_CURRENT_LIST_DIR}/shaders"
        AUTO
    )
endfunction()
