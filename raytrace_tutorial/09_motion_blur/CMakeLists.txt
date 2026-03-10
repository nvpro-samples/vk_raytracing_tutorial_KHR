# 09_motion_blur - Motion blur
#
# This sample demonstrates motion blur with RT common sources and .h.slang file support.

# Setup with RT common sources and .h.slang file support
setup_rt_tutorial_sample(
    USE_RT_COMMON
    SHADER_HEADERS
    CAPABILITIES spvRayTracingMotionBlurNV
) 
