# 11_shader_execution_reorder - Shader Execution Reorder
#
# This sample demonstrates the use of Shader Execution Reorder to render a scene with a skybox.

setup_rt_tutorial_sample(
    USE_RT_COMMON
    SHADER_HEADERS
    CAPABILITIES
        spvShaderInvocationReorderNV
        spvShaderInvocationReorderEXT
        spvShaderClockKHR
    LINK_LIBRARIES
        nvpro2::nvgpu_monitor
)
