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

#include "common/shaders/gltf_accessor.h.slang"
#include "common/shaders/light.h.slang"
#include "common/shaders/pbr.h.slang"
#include "common/shaders/shadow.h.slang"
#include "nvshaders/sky_functions.h.slang"
#include "nvshaders/random.h.slang"
#include "shaderio.h"
#include "common/shaders/ray_common.h.slang"

// clang-format off
[[vk::push_constant]]                           ConstantBuffer<TutoPushConstant> pushConst;
[[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
[[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
[[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

#define MISS_DEPTH 1000

// Raytracing Payload
struct HitPayload : IShadowPayload
{
  float3 color  = float3(0);
  float  weight = 1;
  int    depth  = 0;

  bool isMissed() { return depth == MISS_DEPTH; }
};

// Hit state information
struct HitState
{
  float3 pos;
  float3 nrm;
  float3 geonrm;
};

//-----------------------------------------------------------------------
// RAY GENERATION
//-----------------------------------------------------------------------
[shader("raygeneration")]
void rgenMain()
{
  float2 launchID   = (float2)DispatchRaysIndex().xy;
  float2 launchSize = (float2)DispatchRaysDimensions().xy;

  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  RayDesc ray = generatePrimaryRay(launchID, launchSize, sceneInfo[0]);

  HitPayload payload = {};
  TraceRay(topLevelAS, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
  float3 color             = payload.color;
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// CLOSEST HIT
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
  uint   instanceID   = InstanceIndex();
  uint   triID        = PrimitiveIndex();

  GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
  GltfInstance          instance  = sceneInfo.instances[instanceID];
  GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];
  GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

  // Retrieve the data
  uint3  indices       = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos           = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);
  float3 nrm           = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float3 worldPos      = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
  float3 worldNormal   = normalize(mul(WorldToObject4x3(), nrm).xyz);
  float2 worldTexCoord = getTriangleAttribute<float2>(mesh.gltfBuffer, mesh.triMesh.texCoords, indices, barycentrics);

  GltfPunctual light = processLight(sceneInfo, worldPos);

  float3 V = -WorldRayDirection();
  float3 N = normalize(worldNormal);
  float3 L = normalize(light.direction);

  // Test for shadows
  float shadowFactor = testShadow<HitPayload>(topLevelAS, worldPos, N, light.direction, light);

  // Get base color from material or texture
  float3 albedo = material.baseColorFactor.xyz;
  if(material.baseColorTextureIndex > -1)
  {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(worldTexCoord, 0).xyz;
  }

  // Get metallic and roughness from material
  float metallic  = material.metallicFactor;
  float roughness = material.roughnessFactor;
  if(pushConst.metallicRoughnessOverride.x >= 0.0)
    metallic = pushConst.metallicRoughnessOverride.x;
  if(pushConst.metallicRoughnessOverride.y >= 0.0)
    roughness = pushConst.metallicRoughnessOverride.y;

  // Calculate PBR lighting with sun's color and intensity
  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
  // Apply light color and intensity
  color *= light.color * light.intensity * shadowFactor;

  // Apply transparency weight from any-hit shader
  // For mode 2, add the lighting contribution to any accumulated transparent color
  if(pushConst.transparencyMode == 2)
  {
    payload.color += color * payload.weight;
  }
  else
  {
    payload.color = color;
  }
}

//-----------------------------------------------------------------------
// MISS
//-----------------------------------------------------------------------
[shader("miss")]
void rmissMain(inout HitPayload payload)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  if(sceneInfo.useSky == 1)
  {
    float3 skyColor = evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
    payload.color += skyColor * payload.weight;
  }
  else
  {
    payload.color += sceneInfo.backgroundColor * payload.weight;
  }

  payload.depth = MISS_DEPTH;  // Stop
}

//-----------------------------------------------------------------------
// ANYHIT - With Transparency Support
//-----------------------------------------------------------------------
[shader("anyhit")]
void anyhitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // Find where the ray hit
  float3 pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

  // Get material properties to apply color tinting
  uint                  instanceID = InstanceIndex();
  GltfSceneInfo         sceneInfo  = pushConst.sceneInfoAddress[0];
  GltfInstance          instance   = sceneInfo.instances[instanceID];
  GltfMetallicRoughness material   = sceneInfo.materials[instance.materialIndex];

  float opacity = material.baseColorFactor.a == 1 ? 1 : material.baseColorFactor.a * pushConst.opacity;

  // Mode 0: Original cutout pattern
  if(pushConst.transparencyMode == 0)
  {
    // Checkerboard pattern
    float3 uv = pos * 10.1;
    if((frac(uv.x) >= 0.5 != frac(uv.y) >= 0.5) && pos.y > 0.01)
    {
      return;
    }

    // Circular cutout
    if(length(pos) > pushConst.radius)
    {
      IgnoreHit();
      return;
    }
  }
  // Mode 1: Stochastic transparency
  else if(pushConst.transparencyMode == 1)
  {
    if(opacity == 1)
      return;

    // Create per-intersection seed using xxhash32 for better distribution.
    // Mixing pixel coordinates + frame for temporal variation, and PrimitiveIndex/InstanceID
    // so each intersection along the same ray draws a decorrelated random value
    // (otherwise every transparent any-hit in a pixel reuses the same random stream).
    // xxhash32 only takes a uint3, so we hash in two stages to fold in all 5 values.
    uint2 launchID  = DispatchRaysIndex().xy;
    uint  pixelHash = xxhash32(uint3(launchID, pushConst.frameNumber));
    uint  seed      = xxhash32(uint3(pixelHash, PrimitiveIndex(), InstanceID()));

    // Generate random value using the high-quality PCG generator
    float randValue = rand(seed);

    // Ignore hit based on opacity threshold
    if(randValue > opacity)
    {
      IgnoreHit();
    }
  }
  // Mode 2: Improved transparency with proper opaque surface handling
  else if(pushConst.transparencyMode == 2)
  {
    // For transparent surfaces, accumulate color and continue tracing
    if(opacity < 1.0)
    {
      float3 tintColor = material.baseColorFactor.rgb;
      // Add the transparent surface color contribution
      payload.color += tintColor * payload.weight * opacity;
      // Reduce weight for remaining transparency
      payload.weight *= (1.0 - opacity);
      // Continue tracing to find opaque surfaces or more transparent ones
      IgnoreHit();
    }
    // For opaque surfaces, accept the hit (don't call IgnoreHit)
    // This allows the closest hit shader to run and provide proper lighting
  }
}
