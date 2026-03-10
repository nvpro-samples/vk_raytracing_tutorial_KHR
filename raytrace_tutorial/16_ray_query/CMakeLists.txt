# 16_ray_query - Ray Query
#
# This sample demonstrates the use of ray queries in shaders.

# Setup with RT common sources and ray query capability
setup_rt_tutorial_sample(
    USE_RT_COMMON
    CAPABILITIES spvRayQueryKHR
) 