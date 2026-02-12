# XX_template - Template for new tutorial samples
#
# This is a template CMakeLists.txt file for creating new tutorial samples.
# Copy this file to a new tutorial directory and modify the function call as needed.

# Basic setup with RT common sources (most common case)
setup_rt_tutorial_sample(
	USE_RT_COMMON
	EXTRA_CAPABILITY -capability spvRayQueryKHR
) 