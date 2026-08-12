/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

//
// Ray tracing a grid of instances held in a Partitioned Top-Level Acceleration Structure
// (PTLAS, VK_NV_partitioned_acceleration_structure). The PTLAS is referenced by its buffer's device
// address (passed in the push constant, converted with OpConvertUToAccelerationStructureKHR) - not
// bound as a descriptor - but in the shader it is used exactly like an ordinary acceleration structure.
// On a hit we read the per-instance ID (the partition index, written on the host) to colorize by
// partition, and shade by the instance's update zone (NEAR/MID/FAR) so the zones read apart.
//

#include "common/shaders/ray_common.h.slang"
#include "shaderio.h"

// clang-format off
[[vk::push_constant]]                        ConstantBuffer<TutoPushConstant> pushConst;
[[vk::binding(BindingPoints::eOutImage, 0)]] RWTexture2D<float4>              outImage;
// clang-format on


// Ray tracing payload
struct HitPayload
{
  float3 color;
};

// Hash a partition ID to a distinct, pleasant color (IQ cosine palette).
float3 partitionColor(uint id)
{
  float  t = frac(float(id) * 0.6180339887);  // golden-ratio spread in [0,1)
  float3 a = float3(0.5, 0.5, 0.5);
  float3 b = float3(0.5, 0.5, 0.5);
  float3 c = float3(1.0, 1.0, 1.0);
  float3 d = float3(0.00, 0.33, 0.67);
  return a + b * cos(6.2831853 * (c * t + d));
}

// Classify a partition into its per-frame update zone by the distance of its center to the camera
// (the same test the host uses), and re-shade the base color so the zones read apart. The main zone
// cue is now the LEVEL OF DETAIL (near = smooth, far = faceted); this adds a gentle "fog of distance"
// on top - NEAR vivid, MID a touch muted, FAR softer - without crushing the far spheres to black.
float3 applyZone(float3 base, uint partition)
{
  int   px = int(partition) % PT_PART_AXIS;
  int   pz = int(partition) / PT_PART_AXIS;
  float cx = ((px * PT_TILE + 0.5 * (PT_TILE - 1)) - 0.5 * (PT_GRID_N - 1)) * PT_SPACING;
  float cz = ((pz * PT_TILE + 0.5 * (PT_TILE - 1)) - 0.5 * (PT_GRID_N - 1)) * PT_SPACING;
  float d  = length(float2(cx, cz) - pushConst.cameraXZ);

  float saturation, brightness;
  if(d < pushConst.nearRadius)
  {
    saturation = 2.50;
    brightness = 2.00;
  }  // NEAR
  else if(d < pushConst.midRadius)
  {
    saturation = 0.50;
    brightness = 0.50;
  }  // MID
  else
  {
    saturation = 0.05;
    brightness = 0.32;
  }  // FAR

  float lum = dot(base, float3(0.299, 0.587, 0.114));
  return lerp(float3(lum), base, saturation) * brightness;
}

//-----------------------------------------------------------------------
// RAY GENERATION
//-----------------------------------------------------------------------
[shader("raygeneration")]
void rgenMain()
{
  float2 launchID   = (float2)DispatchRaysIndex().xy;
  float2 launchSize = (float2)DispatchRaysDimensions().xy;

  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  RayDesc        ray       = generatePrimaryRay(launchID, launchSize, sceneInfo[0]);

  RaytracingAccelerationStructure topLevelAS = RaytracingAccelerationStructure(pushConst.tlasAddress);

  HitPayload payload = {};
  payload.color      = float3(0.0);
  TraceRay(topLevelAS, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

  outImage[int2(launchID)] = float4(payload.color, 1.0);
}

//-----------------------------------------------------------------------
// CLOSEST HIT
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // InstanceID() carries what we wrote into the PTLAS instance's instanceID: the partition index for
  // the grid spheres, or a sentinel for the ground plane.
  uint id = InstanceID();

  float3 normal;
  float3 base;
  if(id == PT_GROUND_ID)
  {
    normal = float3(0.0, 1.0, 0.0);     // the ground plane is axis-aligned
    base   = float3(0.35, 0.35, 0.38);  // neutral
  }
  else
  {
    // Every grid instance is a unit sphere centered at its object origin, so the object-space hit
    // position is the surface normal - no vertex/normal buffers needed.
    float3 objPos = ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection();
    normal        = normalize(mul((float3x3)ObjectToWorld4x3(), objPos));
    base          = applyZone(partitionColor(id), id);  // re-shade by update zone (NEAR/MID/FAR)
  }

  float diffuse = max(dot(normal, normalize(pushConst.lightDir)), 0.0);
  payload.color = base * (0.15 + 0.85 * diffuse);
}

//-----------------------------------------------------------------------
// MISS
//-----------------------------------------------------------------------
[shader("miss")]
void rmissMain(inout HitPayload payload)
{
  payload.color = float3(0.10, 0.11, 0.13);  // background
}
