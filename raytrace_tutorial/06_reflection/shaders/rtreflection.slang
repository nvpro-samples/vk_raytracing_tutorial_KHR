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
#include "common/shaders/ray_common.h.slang"

#include "nvshaders/sky_functions.h.slang"
#include "shaderio.h"

// clang-format off
[[vk::push_constant]]                           ConstantBuffer<TutoPushConstant> pushConst;
[[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
[[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
[[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

#define MISS_DEPTH 1000

// Reflection mode (must match C++ define in 06_reflection.cpp)
// IMPORTANT: Iterative mode (0) is RECOMMENDED for production use because:
//   - Only needs maxPipelineRayRecursionDepth=2 (significantly fewer registers per ray!)
//   - NOT LIMITED by hardware ray recursion depth - can trace 50, 100, or more bounces!
//   - Recursive needs maxPipelineRayRecursionDepth=MAX_DEPTH+2 (each reflection traces a shadow ray!)
//   - Recursive is limited by GPU's maxRayRecursionDepth (typically 4-31, varies by vendor)
//
// 0 = Iterative (RECOMMENDED): Loop-based, unlimited depth, better performance
// 1 = Recursive: Hardware recursion, hardware-limited depth, uses more registers
#define USE_RECURSIVE_REFLECTION 0

// Raytracing Payload
struct HitPayload : IShadowPayload
{
  float3 color  = float3(0);
  float  weight = 1;
  int    depth  = 0;

  // For iterative reflection
  float3 rayOrigin    = float3(0);
  float3 rayDirection = float3(0);

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

  const uint rayFlags = RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
  RayDesc    ray      = generatePrimaryRay(launchID, launchSize, sceneInfo[0]);

  HitPayload payload = {};

#if USE_RECURSIVE_REFLECTION
  // Recursive reflection (handled in closest hit shader)
  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  float3 color = payload.color;
#else
  // Iterative reflection loop
  float3 accumulatedColor = float3(0, 0, 0);

  while(payload.depth < pushConst.depthMax && payload.weight > 0.01)
  {
    float prevWeight = payload.weight;
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);

    accumulatedColor += payload.color * prevWeight;

    // Update ray for next iteration if continuing
    ray.Origin    = payload.rayOrigin;
    ray.Direction = payload.rayDirection;
  }

  float3 color = accumulatedColor;
#endif

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
  uint   meshID       = InstanceID();
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
  -WorldRayDirection();
  float3 N = normalize(worldNormal);
  float3 L = normalize(light.direction);

  if(dot(N, V) < 0.0)
  {
    // Flip normal if facing away from camera
    N = -N;
  }


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
  // Apply light color and intensity, but only if not in shadow
  color *= light.color * light.intensity * shadowFactor;

  // Apply ambient
  float3 ambient = sceneInfo.backgroundColor;
  if(sceneInfo.useSky == 1)
  {
    // Add ambient lighting (sky effect)
    float3 skyUpDir    = float3(0, 1, 0);
    float3 groundColor = sceneInfo.skySimpleParam.groundColor;
    float3 skyColor    = sceneInfo.skySimpleParam.skyColor;
    ambient            = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
  }
  // Add ambient to final color
  color += ambient * albedo * 0.025;

  // Handle reflection based on material properties
  float3 reflectionColor = float3(0, 0, 0);

#if USE_RECURSIVE_REFLECTION
  // Recursive reflection
  if(payload.depth < pushConst.depthMax && metallic > 0.01)
  {
    // Calculate reflection direction
    float3 reflectionDir = reflect(-V, N);

    // Create reflection ray
    RayDesc reflectionRay;
    reflectionRay.Origin    = worldPos + N * 0.001;  // Offset to avoid self-intersection
    reflectionRay.Direction = reflectionDir;
    reflectionRay.TMin      = 0.001;
    reflectionRay.TMax      = INFINITE;

    // Create payload for reflection ray
    HitPayload reflectionPayload;
    reflectionPayload.color  = float3(0, 0, 0);
    reflectionPayload.weight = payload.weight * metallic;
    reflectionPayload.depth  = payload.depth + 1;

    // Trace reflection ray
    TraceRay(topLevelAS, 0, 0xff, 0, 0, 0, reflectionRay, reflectionPayload);

    // Add reflection contribution
    reflectionColor = reflectionPayload.color * metallic;
  }

  payload.color = color + reflectionColor;

#else
  // Iterative reflection - set up next iteration
  payload.rayOrigin    = worldPos;        // + N * 0.001;
  payload.rayDirection = reflect(-V, N);  // Calculate reflection direction
  payload.color        = color;
  payload.weight *= metallic;
  payload.depth++;
#endif
}

//-----------------------------------------------------------------------
// MISS
//-----------------------------------------------------------------------
[shader("miss")]
void rmissMain(inout HitPayload payload)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  float3         missColor = float3(0, 0, 0);

  if(sceneInfo.useSky == 1)
  {
    float3 skyColor = evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
    missColor       = skyColor;
  }
  else
  {
    missColor = sceneInfo.backgroundColor;
  }

  payload.color += missColor * payload.weight;

  payload.depth = MISS_DEPTH;  // Stop
}
