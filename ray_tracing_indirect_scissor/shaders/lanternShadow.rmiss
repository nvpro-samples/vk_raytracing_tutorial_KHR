#version 460
#extension GL_EXT_ray_tracing : require

// Miss shader invoked when tracing shadow rays (rays towards lantern)
// in lantern passes. Misses shouldn't really happen, but if they do,
// report we did not hit any lantern by setting hitLanternInstance = -1.
layout(location = 2) rayPayloadInEXT int hitLanternInstance;

void main()
{
  hitLanternInstance = -1;
}
