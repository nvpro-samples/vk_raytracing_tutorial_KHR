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
#include "common/shaders/ray_common.h.slang"
#include "nvshaders/sky_functions.h.slang"

#include "shaderio.h"
#include "wireframe_helpers.h.slang"

// clang-format off
 [[vk::push_constant]]                           ConstantBuffer<TutoPushConstant> pushConst;
 [[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
 [[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
 [[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];

// clang-format on

#define MISS_DEPTH 1000

// Raytracing payload
struct HitPayload
{
  float3 color = float3(0);
  int    depth = 0;
};


// Build a wireframe.h.slang::WireframeSettings from the push-constant mirror.
// `bool` in Slang cannot be portably round-tripped through a push-constant
// from C++, so the mirror packs them as ints and we unpack here.
WireframeSettings unpackWireframeSettings(WireframeSettingsIO io)
{
  WireframeSettings s;
  s.thickness      = io.thickness;
  s.thicknessVar   = io.thicknessVar;
  s.smoothing      = io.smoothing;
  s.stipple        = (io.stipple != 0);
  s.stippleLength  = io.stippleLength;
  s.stippleRepeats = io.stippleRepeats;
  return s;
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

  HitPayload payload = {};

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  outImage[int2(launchID)] = float4(payload.color, 1.0);
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

  // Per-vertex object-space positions (needed by computeDerivative).
  uint3    indices       = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3   pos0          = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.x);
  float3   pos1          = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.y);
  float3   pos2          = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.z);
  float4x3 objectToWorld = ObjectToWorld4x3();
  float3   wp0           = mul(float4(pos0, 1.0), objectToWorld);
  float3   wp1           = mul(float4(pos1, 1.0), objectToWorld);
  float3   wp2           = mul(float4(pos2, 1.0), objectToWorld);
  float3   worldPos      = interpolateBarycentric(wp0, wp1, wp2, barycentrics);

  float3 nrm         = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float3 worldNormal = normalize(mul(WorldToObject4x3(), nrm).xyz);

  // #DERIVATIVES
  // Because this sample only traces primary rays, the per-pixel ray
  // differentials can be reconstructed locally from DispatchRaysIndex()
  // and the scene's inverse matrices. No payload growth required.
  float3 diffX, diffY;
  computeDifferentials(float2(DispatchRaysIndex().xy), float2(DispatchRaysDimensions().xy), sceneInfo.viewInvMatrix,
                       sceneInfo.projInvMatrix, diffX, diffY);

  // Barycentric "fwidth" deltas at the hit, the ray-tracing analogue of fwidth(bary) in a rasterizer.
  float3 triVerts[3] = { pos0, pos1, pos2 };
  float3 deltas = computeDerivative(WorldToObject4x3(), WorldRayOrigin(), ObjectRayOrigin(), triVerts, diffX, diffY, barycentrics);

  // Wireframe coverage in [0..1], 0 = away from any edge, 1 = on an edge.
  WireframeSettings settings = unpackWireframeSettings(pushConst.wf);
  float             t        = processWireframe(settings, barycentrics, deltas);

  GltfPunctual light = processLight(sceneInfo, worldPos);
  float3       L     = normalize(light.direction);

  float3 V = -WorldRayDirection();
  float3 N = normalize(worldNormal);

  // Test for shadows
  // float shadowFactor = testShadow(worldPos, N, light.direction, light);

  // Get base color from material or texture
  float3 albedo = material.baseColorFactor.xyz;

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
  color *= light.color * light.intensity;  //* shadowFactor;

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

  // Pure wireframe look: background where there's no edge, wireframe colour on edges.
  payload.color = lerp(color, pushConst.wireframeColor, t);
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
    payload.color = evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
  }
  else
  {
    payload.color = sceneInfo.backgroundColor;
  }

  payload.depth = MISS_DEPTH;
}
