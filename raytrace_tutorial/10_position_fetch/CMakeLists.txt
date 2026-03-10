# 10_position_fetch - Position fetch
#
# This sample demonstrates the use of position fetch to render a scene with a skybox.

# Setup with RT common sources and .h.slang file support
setup_rt_tutorial_sample(
    USE_RT_COMMON
    SHADER_HEADERS
    CAPABILITIES spvRayTracingPositionFetchKHR
) 
