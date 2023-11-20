/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#include "raycommon.glsl"


const int GROUP_SIZE = 16;
layout(local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE) in;
layout(set = 0, binding = 0, rgba32f) uniform image2D inImage;
layout(set = 0, binding = 1, r32f) uniform image2D outImage;
layout(set = 0, binding = 2) uniform accelerationStructureEXT topLevelAS;


// See AoControl
layout(push_constant) uniform params_
{
  float rtao_radius;
  int   rtao_samples;
  float rtao_power;
  int   rtao_distance_based;
  int   frame_number;
  int   max_samples;
};


//----------------------------------------------------------------------------
// Tracing a ray and returning the weight based on the distance of the hit
//
float TraceRay(in rayQueryEXT rayQuery, in vec3 origin, in vec3 direction)
{
  uint flags = gl_RayFlagsNoneEXT;
  if(rtao_distance_based == 0)
    flags = gl_RayFlagsTerminateOnFirstHitEXT;

  rayQueryInitializeEXT(rayQuery, topLevelAS, flags, 0xFF, origin, 0.0f, direction, rtao_radius);

  // Start traversal: return false if traversal is complete
  while(rayQueryProceedEXT(rayQuery))
  {
  }

  // Returns type of committed (true) intersection
  if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
  {
    // Got an intersection == Shadow
    if(rtao_distance_based == 0)
      return 1;
    float length = 1 - (rayQueryGetIntersectionTEXT(rayQuery, true) / rtao_radius);
    return length;  // * length;
  }

  return 0;
}


void main()
{
  float occlusion = 0.0;

  ivec2 size = imageSize(inImage);
  // Check if not outside boundaries
  if(gl_GlobalInvocationID.x >= size.x || gl_GlobalInvocationID.y >= size.y)
    return;

  // Initialize the random number
  uint seed = tea(size.x * gl_GlobalInvocationID.y + gl_GlobalInvocationID.x, frame_number);

  // Retrieving position and normal
  vec4 gBuffer = imageLoad(inImage, ivec2(gl_GlobalInvocationID.xy));

  // Shooting rays only if a fragment was rendered
  if(gBuffer != vec4(0))
  {
    vec3 origin = gBuffer.xyz;
    vec3 normal = DecompressUnitVec(floatBitsToUint(gBuffer.w));
    vec3 direction;

    // Move origin slightly away from the surface to avoid self-occlusion
    origin = OffsetRay(origin, normal);

    // Finding the basis (tangent and bitangent) from the normal
    vec3 n, tangent, bitangent;
    ComputeDefaultBasis(normal, tangent, bitangent);

    // Sampling hemiphere n-time
    for(int i = 0; i < rtao_samples; i++)
    {
      // Cosine sampling
      float r1        = rnd(seed);
      float r2        = rnd(seed);
      float sq        = sqrt(1.0 - r2);
      float phi       = 2 * M_PI * r1;
      vec3  direction = vec3(cos(phi) * sq, sin(phi) * sq, sqrt(r2));
      direction       = direction.x * tangent + direction.y * bitangent + direction.z * normal;
      // Initializes a ray query object but does not start traversal
      rayQueryEXT rayQuery;

      occlusion += TraceRay(rayQuery, origin, direction);
    }

    // Computing occlusion
    occlusion = 1 - (occlusion / rtao_samples);
    occlusion = pow(clamp(occlusion, 0, 1), rtao_power);
  }


  // Writting out the AO
  if(frame_number == 0)
  {
    imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), vec4(occlusion));
  }
  else
  {
    // Accumulating over time
    float old_ao     = imageLoad(outImage, ivec2(gl_GlobalInvocationID.xy)).x;
    float new_result = mix(old_ao, occlusion, 1.0f / float(frame_number + 1));
    imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), vec4(new_result));
  }
}
