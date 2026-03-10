# 17_ray_query_screenspace - Ray Query Screen-Space Effects Tutorial
#
# This tutorial demonstrates practical ray query usage in compute shaders for
# screen-space effects like ambient occlusion. It shows how to integrate ray
# tracing with rasterization pipelines using compute shaders to avoid the
# performance issues of fragment shader ray queries.

# Setup with foundation shader and RT common sources
setup_rt_tutorial_sample(
    USE_RT_COMMON
    CAPABILITIES spvRayQueryKHR
)
