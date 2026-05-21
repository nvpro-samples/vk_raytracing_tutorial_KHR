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
 [[vk::binding(BindingPoints::eImplicit, 1)]]    StructuredBuffer<Sphere> allSpheres;
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

  const uint rayFlags = RAY_FLAG_NONE;
  RayDesc    ray      = generatePrimaryRay(launchID, launchSize, sceneInfo[0]);

  HitPayload payload = {};

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  float3 color             = payload.color;
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// SHARED LIGHTING FUNCTION
//-----------------------------------------------------------------------
float3 calculateLighting(float3 worldPos, float3 worldNormal, float3 albedo, float metallic, float roughness, GltfSceneInfo* sceneInfo)
{
  GltfPunctual light = processLight(sceneInfo[0], worldPos);

  float3 V = -WorldRayDirection();
  float3 N = normalize(worldNormal);
  float3 L = normalize(light.direction);

  // Test for shadows
  float shadowFactor = testShadow<HitPayload>(topLevelAS, worldPos, N, light.direction, light);

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

  return color;
}

//-----------------------------------------------------------------------
// CLOSEST HIT FOR TRIANGLES
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
  uint   instanceID   = InstanceIndex();
  uint   meshID       = InstanceID();
  uint   triID        = PrimitiveIndex();

  GltfSceneInfo*        sceneInfo = pushConst.sceneInfoAddress;
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

  payload.color = calculateLighting(worldPos, worldNormal, albedo, metallic, roughness, sceneInfo);
}

//-----------------------------------------------------------------------
// INTERSECTION SHADER
//-----------------------------------------------------------------------
[shader("intersection")]
void rintMain()
{
  // Get ray information
  float3 rayOrigin    = WorldRayOrigin();
  float3 rayDirection = WorldRayDirection();

  // Get sphere data
  Sphere sphere = allSpheres[PrimitiveIndex()];

  // Determine if this is a sphere or cube
  float tHit    = -1;
  int   hitKind = PrimitiveIndex() % 2 == 0 ? ImplicitObjectKind::eSphere : ImplicitObjectKind::eCube;

  if(hitKind == ImplicitObjectKind::eSphere)
  {
    // Sphere intersection
    tHit = hitSphere(sphere, rayOrigin, rayDirection);
  }
  else
  {
    // AABB intersection
    Aabb aabb;
    aabb.minimum = sphere.center - float3(sphere.radius);
    aabb.maximum = sphere.center + float3(sphere.radius);
    tHit         = hitAabb(aabb, rayOrigin, rayDirection);
  }

  // Report hit point
  if(tHit > 0)
  {
    // Create hit attributes
    BuiltInTriangleIntersectionAttributes attr;
    attr.barycentrics = float2(0.0, 0.0);  // For a plane, we can use (0,0) as barycentrics

    ReportHit(tHit, hitKind, attr);
  }
}

//-----------------------------------------------------------------------
// CLOSEST HIT FOR IMPLICIT OBJECTS
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain2(inout HitPayload payload)
{
  uint instanceID  = InstanceIndex();
  uint primitiveID = PrimitiveIndex();
  int  hitKind     = HitKind();

  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // Get sphere data
  Sphere sphere = allSpheres[primitiveID];

  // Calculate world position
  float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

  // Calculate normal
  float3 normal = normalize(worldPos - sphere.center);

  // For cubes, set normal to major axis
  if(hitKind == ImplicitObjectKind::eCube)
  {
    float3 absN = abs(normal);
    float  maxC = max(max(absN.x, absN.y), absN.z);
    normal      = (maxC == absN.x) ? float3(sign(normal.x), 0, 0) :
                  (maxC == absN.y) ? float3(0, sign(normal.y), 0) :
                                     float3(0, 0, sign(normal.z));
  }

  // Get material (alternating between two materials)
  GltfMetallicRoughness material;
  material.baseColorFactor = (primitiveID % 2 == 0) ? float4(0, 1, 1, 1) : float4(1, 1, 0, 1);
  material.metallicFactor  = 0.5f;
  material.roughnessFactor = 0.5f;

  // Get base color from material
  float3 albedo = material.baseColorFactor.xyz;

  // Get metallic and roughness from material
  float metallic  = material.metallicFactor;
  float roughness = material.roughnessFactor;
  if(pushConst.metallicRoughnessOverride.x >= 0.0)
    metallic = pushConst.metallicRoughnessOverride.x;
  if(pushConst.metallicRoughnessOverride.y >= 0.0)
    roughness = pushConst.metallicRoughnessOverride.y;

  payload.color = calculateLighting(worldPos, normal, albedo, metallic, roughness, sceneInfo);
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
// INTERSECTION FUNCTIONS
//-----------------------------------------------------------------------

// Ray-Sphere intersection
float hitSphere(Sphere sphere, float3 rayOrigin, float3 rayDirection)
{
  float3 oc           = rayOrigin - sphere.center;
  float  a            = dot(rayDirection, rayDirection);
  float  b            = 2.0 * dot(oc, rayDirection);
  float  c            = dot(oc, oc) - sphere.radius * sphere.radius;
  float  discriminant = b * b - 4 * a * c;
  if(discriminant < 0)
  {
    return -1.0;
  }
  else
  {
    return (-b - sqrt(discriminant)) / (2.0 * a);
  }
}

// Ray-AABB intersection
float hitAabb(Aabb aabb, float3 rayOrigin, float3 rayDirection)
{
  float3 invDir = 1.0 / rayDirection;
  float3 tbot   = invDir * (aabb.minimum - rayOrigin);
  float3 ttop   = invDir * (aabb.maximum - rayOrigin);
  float3 tmin   = min(ttop, tbot);
  float3 tmax   = max(ttop, tbot);
  float  t0     = max(tmin.x, max(tmin.y, tmin.z));
  float  t1     = min(tmax.x, min(tmax.y, tmax.z));
  return t1 > max(t0, 0.0) ? t0 : -1.0;
}
