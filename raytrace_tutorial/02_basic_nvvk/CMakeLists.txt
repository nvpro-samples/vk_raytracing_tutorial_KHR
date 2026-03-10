# 02_basic - Basic raytracing with foundation shader
#
# This sample demonstrates basic raytracing with the foundation shader and
# additional shader include directories.

# Setup with foundation shader and extra includes
setup_rt_tutorial_sample(
    USE_FOUNDATION_SHADER
    SHADER_INCLUDE_DIRS
        ${TUTO_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/shaders
    COPY_DIRS
        ${COMMON_DIR}/shaders
)
