#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"

// Closest hit shader invoked when a primary ray hits a lantern.

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload prd;

layout(binding = 2, set = 0) buffer LanternArray { LanternIndirectEntry lanterns[]; } lanterns;

// clang-format on

void main()
{
  // Just look up this lantern's color. Self-illuminating, so no lighting calculations.
  LanternIndirectEntry lantern = lanterns.lanterns[nonuniformEXT(gl_InstanceCustomIndexEXT)];
  prd.hitValue = vec3(lantern.red, lantern.green, lantern.blue);
  prd.additiveBlending = false;
}
