#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"

// During a lantern pass, this closest hit shader is invoked when
// shadow rays (rays towards lantern) hit a regular OBJ. Report back
// that no lantern was hit (-1).

// clang-format off
layout(location = 2) rayPayloadInEXT int hitLanternInstance;

// clang-format on

void main()
{
  hitLanternInstance = -1;
}
