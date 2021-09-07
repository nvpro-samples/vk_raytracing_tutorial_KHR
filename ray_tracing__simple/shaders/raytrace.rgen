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
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require


#include "raycommon.glsl"
#include "host_device.h"

// clang-format off
layout(location = 0) rayPayloadEXT hitPayload prd;

layout(set = 0, binding = eTlas) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = eOutImage, rgba32f) uniform image2D image;
layout(set = 1, binding = eGlobals) uniform _GlobalUniforms { GlobalUniforms uni; };
layout(push_constant) uniform _PushConstantRay { PushConstantRay pcRay; };
// clang-format on


void main()
{
  const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
  const vec2 inUV        = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
  vec2       d           = inUV * 2.0 - 1.0;

  vec4 origin    = uni.viewInverse * vec4(0, 0, 0, 1);
  vec4 target    = uni.projInverse * vec4(d.x, d.y, 1, 1);
  vec4 direction = uni.viewInverse * vec4(normalize(target.xyz), 0);

  uint  rayFlags = gl_RayFlagsOpaqueEXT;
  float tMin     = 0.001;
  float tMax     = 10000.0;

  traceRayEXT(topLevelAS,     // acceleration structure
              rayFlags,       // rayFlags
              0xFF,           // cullMask
              0,              // sbtRecordOffset
              0,              // sbtRecordStride
              0,              // missIndex
              origin.xyz,     // ray origin
              tMin,           // ray min range
              direction.xyz,  // ray direction
              tMax,           // ray max range
              0               // payload (location = 0)
  );

  imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(prd.hitValue, 1.0));
}
