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

// Ray-cone footprint carried alongside every ray. `width` is the world-space
// diameter of the footprint at the ray origin, `spreadAngle` is the angular
// growth per unit distance. A secondary ray inherits its parent's footprint
// just by setting `cone.width = worldFootprintAtHit` before tracing again.
struct RayCone
{
  float width;
  float spreadAngle;
};

// Raytracing Payload
struct HitPayload : IShadowPayload
{
  float3  color  = float3(0);
  float   weight = 1;
  int     depth  = 0;
  RayCone cone   = RayCone();

  bool isMissed() { return depth == MISS_DEPTH; }
};

// Hit state information
struct HitState
{
  float3 pos;
  float3 nrm;
  float3 geonrm;
};

// #DERIVATIVES
//-----------------------------------------------------------------------
// Ray-cone texture LOD (Akenine-Möller et al. 2019, "Texture Level of
// Detail Strategies for Real-Time Ray Tracing"). Three small helpers:
//
//   1. makePrimaryRayCone  - initialise the cone for a pinhole camera.
//                            projInv[1][1] == tan(fovY/2), so per-pixel
//                            angular size is 2*tan(fovY/2)/viewportHeight.
//
//   2. rayConeWorldFootprint - cone width at the hit, with surface-slant
//                              correction (1/|N·V|).
//
//   3. triangleTexelDensity  - UV-per-world ratio on the hit triangle,
//                              i.e. sqrt(uvArea/worldArea).
//
// Combined: uvFootprint = rayConeWorldFootprint() * triangleTexelDensity()
// and that scalar is used as the SampleGrad gradient magnitude.
//-----------------------------------------------------------------------
RayCone makePrimaryRayCone(float4x4 projInv, float viewportHeight)
{
  RayCone c;
  c.width       = 0.0;
  c.spreadAngle = 2.0 * abs(projInv[1][1]) / max(viewportHeight, 1.0);
  return c;
}

float rayConeWorldFootprint(RayCone cone, float hitT, float3 N, float3 V)
{
  float w = cone.width + cone.spreadAngle * hitT;
  return w / max(abs(dot(N, V)), 1e-3);  // slant correction
}

float triangleTexelDensity(float3 wp0, float3 wp1, float3 wp2, float2 uv0, float2 uv1, float2 uv2)
{
  float3 we1    = wp1 - wp0;
  float3 we2    = wp2 - wp0;
  float  wArea  = length(cross(we1, we2));
  float2 duv1   = uv1 - uv0;
  float2 duv2   = uv2 - uv0;
  float  uvArea = abs(duv1.x * duv2.y - duv1.y * duv2.x);
  return sqrt(max(uvArea, 1e-20) / max(wArea, 1e-20));
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

  const uint rayFlags = RAY_FLAG_NONE;
  RayDesc    ray      = generatePrimaryRay(launchID, launchSize, sceneInfo[0]);

  // Initial state
  HitPayload payload = {};
  payload.cone       = makePrimaryRayCone(sceneInfo.projInvMatrix, launchSize.y);

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
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
  uint   meshID       = InstanceID();
  uint   triID        = PrimitiveIndex();

  GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
  GltfInstance          instance  = sceneInfo.instances[instanceID];
  GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];
  GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

  // Per-vertex attributes
  uint3  indices = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos0    = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.x);
  float3 pos1    = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.y);
  float3 pos2    = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.z);
  float2 tex0    = getAttribute<float2>(mesh.gltfBuffer, mesh.triMesh.texCoords, indices.x);
  float2 tex1    = getAttribute<float2>(mesh.gltfBuffer, mesh.triMesh.texCoords, indices.y);
  float2 tex2    = getAttribute<float2>(mesh.gltfBuffer, mesh.triMesh.texCoords, indices.z);
  float3 nrm     = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);

  // Triangle and hit point in world space
  float4x3 objectToWorld = ObjectToWorld4x3();
  float3   wp0           = mul(float4(pos0, 1.0), objectToWorld);
  float3   wp1           = mul(float4(pos1, 1.0), objectToWorld);
  float3   wp2           = mul(float4(pos2, 1.0), objectToWorld);
  float3   worldPos      = interpolateBarycentric(wp0, wp1, wp2, barycentrics);
  float2   worldTexCoord = interpolateBarycentric(tex0, tex1, tex2, barycentrics);
  float3   worldNormal   = normalize(mul(WorldToObject4x3(), nrm).xyz);

  float3 V = -WorldRayDirection();
  float3 N = normalize(worldNormal);

  // #DERIVATIVES
  // Texture LOD via the ray cone carried in the payload.
  // For a secondary ray we would just set `payload.cone.width = worldFoot`
  // before tracing again and the next hit inherits this footprint.
  float worldFoot    = rayConeWorldFootprint(payload.cone, RayTCurrent(), N, V);
  float uvFoot       = worldFoot * triangleTexelDensity(wp0, wp1, wp2, tex0, tex1, tex2) * pushConst.texGradScale;
  payload.cone.width = worldFoot;

  GltfPunctual light = processLight(sceneInfo, worldPos);
  float3       L     = normalize(light.direction);

  // Test for shadows
  float shadowFactor = testShadow<HitPayload>(topLevelAS, worldPos, N, light.direction, light);

  // Get base color from material or texture
  float3 albedo = material.baseColorFactor.xyz;
  if(material.baseColorTextureIndex > -1)
  {
    // #DERIVATIVES
    albedo *= textures[material.baseColorTextureIndex].SampleGrad(worldTexCoord, float2(uvFoot, 0), float2(0, uvFoot)).xyz;
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

  payload.color = color;
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
