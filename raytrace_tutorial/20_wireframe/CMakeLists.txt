# 20_wireframe - Wireframe rendering driven by ray differentials
#
# This sample reuses the ray-differentials concept from sample 19, but
# computes the differentials locally in the closest-hit shader (since only
# primary rays need them) and feeds them to the wireframe helpers in
# nvshaders/wireframe.h.slang.

setup_rt_tutorial_sample(
    USE_RT_COMMON
    SHADER_HEADERS   # Picks up shaders/*.h.slang as headers (excluded from entry-point compilation)
)
