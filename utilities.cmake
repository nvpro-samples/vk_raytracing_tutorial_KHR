


# -------------------------------------------------------------------------------------------------
# function that copies a list of files into the target directory
#
#   target_copy_to_output_dir(TARGET foo
#       [RELATIVE <path_prefix>]                                # allows to keep the folder structure starting from this level
#       FILES <absolute_file_path> [<absolute_file_path>]
#       )
#
function(TARGET_COPY_TO_OUTPUT_DIR)
    set(options)
    set(oneValueArgs TARGET RELATIVE DEST_SUBFOLDER)
    set(multiValueArgs FILES)
    cmake_parse_arguments(TARGET_COPY_TO_OUTPUT_DIR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

    foreach(_ELEMENT ${TARGET_COPY_TO_OUTPUT_DIR_FILES} )

        # handle absolute and relative paths
        if(TARGET_COPY_TO_OUTPUT_DIR_RELATIVE)
            set(_SOURCE_FILE ${TARGET_COPY_TO_OUTPUT_DIR_RELATIVE}/${_ELEMENT})
            set(_FOLDER_PATH ${_ELEMENT})
        else()
            set(_SOURCE_FILE ${_ELEMENT})
            get_filename_component(_FOLDER_PATH ${_ELEMENT} NAME)
            set (_ELEMENT "")
        endif()

        # handle directories and files slightly different
        if(IS_DIRECTORY ${_SOURCE_FILE})
            if(MDL_LOG_FILE_DEPENDENCIES)
                MESSAGE(STATUS "- folder to copy: ${_SOURCE_FILE}")
            endif()
            add_custom_command(
                TARGET ${TARGET_COPY_TO_OUTPUT_DIR_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${_SOURCE_FILE} $<TARGET_FILE_DIR:${TARGET_COPY_TO_OUTPUT_DIR_TARGET}>/${TARGET_COPY_TO_OUTPUT_DIR_DEST_SUBFOLDER}${_FOLDER_PATH}
            )
        else()   
            if(MDL_LOG_FILE_DEPENDENCIES)
                MESSAGE(STATUS "- file to copy:   ${_SOURCE_FILE}")
            endif()
            add_custom_command(
                TARGET ${TARGET_COPY_TO_OUTPUT_DIR_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_SOURCE_FILE} $<TARGET_FILE_DIR:${TARGET_COPY_TO_OUTPUT_DIR_TARGET}>/${TARGET_COPY_TO_OUTPUT_DIR_DEST_SUBFOLDER}${_ELEMENT}
            )
        endif()
    endforeach()
endfunction()

