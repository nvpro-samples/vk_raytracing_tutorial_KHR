# 11_shader_execution_reorder - Shader Execution Reorder
#
# This sample demonstrates the use of Shader Execution Reorder to render a scene with a skybox.

# Setup with RT common sources and .h.slang file support
setup_rt_tutorial_sample(
    USE_RT_COMMON
    INCLUDE_H_SLANG_FILES
    EXTRA_CAPABILITY -capability spvShaderInvocationReorderNV+spvShaderClockKHR # +spvShaderInvocationReorderEXT
) 

# Extra link
target_link_libraries(11_shader_execution_reorder PRIVATE nvpro2::nvgpu_monitor)