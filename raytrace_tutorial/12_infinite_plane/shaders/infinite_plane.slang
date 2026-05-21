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
#include "common/shaders/pbr.h.slang"
#include "common/shaders/ray_common.h.slang"

#include "nvshaders/random.h.slang"
#include "nvshaders/ray_utils.h.slang"
#include "nvshaders/sky_functions.h.slang"

#include "shaderio.h"

// clang-format off
 [[vk::push_constant]] ConstantBuffer<TutoPushConstant> pushConst;

 [[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
 [[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
 [[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

// Raytracing Payload
struct HitPayload
{
  float  hitT          = 0.f;
  int    instanceIndex = 0;
  float3 pos           = float3(0, 0, 0);
  float3 nrm           = float3(0, 0, 0);
};

// Hit state information
struct HitState
{
  float3 pos;
  float3 nrm;
};

//-----------------------------------------------------------------------
// Return hit position, normal and geometric normal in world space
HitState getHitState(GltfMesh mesh, uint triID, float4x3 objectToWorld, float4x3 worldToObject, float3 barycentrics)
{
  HitState hit;

  // Retrieve the data
  uint3  indices     = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos         = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);
  float3 nrm         = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float3 worldPos    = float3(mul(float4(pos, 1.0), objectToWorld));
  float3 worldNormal = normalize(mul(worldToObject, nrm).xyz);
  hit.pos            = worldPos;
  hit.nrm            = worldNormal;
  return hit;
}

//-----------------------------------------------------------------------
// Check for infinite plane intersection and update hit state if needed
//-----------------------------------------------------------------------
bool checkInfinitePlaneIntersection(RayDesc ray, inout HitPayload payload)
{
  if(pushConst.planeEnabled != 1)
    return false;

  // Plane definition
  float3 normal      = float3(0, 1, 0);        // Y-up plane normal
  float  planeHeight = pushConst.planeHeight;  // Height of plane from origin

  // Only report intersection if camera is above the plane
  if(ray.Origin.y <= planeHeight)
    return false;

  // Calculate denominator for ray-plane intersection
  float Dn = dot(ray.Direction, normal);
  if(abs(Dn) <= 1e-6)
    return false;

  float On               = dot(ray.Origin, normal);
  float intersectionDist = (-On + planeHeight) / Dn;
  if(intersectionDist <= 0 || (payload.hitT != INFINITE && intersectionDist >= payload.hitT))
    return false;

  // Update hit information
  payload.hitT          = intersectionDist;
  payload.pos           = ray.Origin + ray.Direction * payload.hitT;
  payload.nrm           = normal;
  payload.instanceIndex = -1;  // Special index for plane

  return true;
}

//-----------------------------------------------------------------------
// Shoot a ray an return the information of the closest hit, in the
// PtPayload structure (PRD)
//
void traceRay(RayDesc ray, inout HitPayload payload)
{
  payload.hitT  = 0.0F;
  ray.TMax      = INFINITE;
  uint rayFlags = RAY_FLAG_NONE;

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
}

//-----------------------------------------------------------------------
// Shadow ray - return true if a ray hits anything
//
bool traceShadow(RayDesc ray, inout HitPayload payload)
{
  payload.hitT = 0.0F;
  uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  return payload.hitT < INFINITE;
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
float3 pathTrace(RayDesc ray, inout uint seed, inout HitPayload payload)
{
  float3 radiance   = float3(0.0F, 0.0F, 0.0F);
  float3 throughput = float3(1.0F, 1.0F, 1.0F);

  for(int depth = 0; depth < pushConst.maxDepth; depth++)
  {
    // Trace ray against scene geometry first
    traceRay(ray, payload);

    // Check for infinite plane intersection and update if closer
    bool usePlane = checkInfinitePlaneIntersection(ray, payload);

    // Hitting the environment, then exit
    if(payload.hitT == INFINITE)
    {
      GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;
      float3         sky_color = evalSimpleSky(sceneInfo.skySimpleParam, ray.Direction);
      return radiance + (sky_color * throughput);
    }

    // Retrieve the Instance buffer information
    GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
    GltfMetallicRoughness material;

    if(usePlane)
    {
      // Plane material
      material.baseColorFactor = float4(pushConst.planeColor, 1);
      material.metallicFactor  = pushConst.metallicRoughnessOverride.x;
      material.roughnessFactor = max(0.0001f, pushConst.metallicRoughnessOverride.y);
    }
    else
    {
      GltfInstance instance = sceneInfo.instances[payload.instanceIndex];
      material              = sceneInfo.materials[instance.materialIndex];
    }

    // View and light direction
    float3 V = -ray.Direction;
    float3 L = normalize(sceneInfo.skySimpleParam.sunDirection);

    // Initialize PBR material
    PbrBaseMaterial pbrMat = initPbrBaseMaterial(material.baseColorFactor.xyz, material.metallicFactor,
                                                 material.roughnessFactor, payload.nrm, payload.nrm);

    float3 contrib = float3(0, 0, 0);

    // Evaluation of direct light (sun)
    bool nextEventValid = (dot(L, payload.nrm) > 0.0f);
    if(nextEventValid)
    {
      BsdfEvaluateData evalData;
      evalData.k1 = -ray.Direction;
      evalData.k2 = normalize(sceneInfo.skySimpleParam.sunDirection);
      evalData.xi = float3(rand(seed), rand(seed), rand(seed));
      bsdfEvaluateSimple(evalData, pbrMat);

      const float3 w = 1.0f;
      contrib += w * evalData.bsdf_diffuse;
      contrib += w * evalData.bsdf_glossy;
      contrib *= throughput;
    }

    // Sample BSDF
    {
      BsdfSampleData sampleData;
      sampleData.k1 = -ray.Direction;  // outgoing direction
      sampleData.xi = float3(rand(seed), rand(seed), rand(seed));

      bsdfSampleSimple(sampleData, pbrMat);
      if(sampleData.event_type == BSDF_EVENT_ABSORB)
      {
        break;  // Need to add the contribution ?
      }

      throughput *= sampleData.bsdf_over_pdf;
      ray.Origin    = offsetRay(payload.pos, payload.nrm);
      ray.Direction = sampleData.k2;
    }

    // Russian-Roulette (minimizing live state)
    float rrPcont = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001F, 0.95F);
    if(rand(seed) >= rrPcont)
      break;                // paths with low throughput that won't contribute
    throughput /= rrPcont;  // boost the energy of the non-terminated paths

    // We are adding the contribution to the radiance only if the ray is not occluded by an object.
    if(nextEventValid)
    {
      RayDesc shadowRay;
      shadowRay.Origin    = ray.Origin;
      shadowRay.Direction = L;
      shadowRay.TMin      = 0.01;
      shadowRay.TMax      = INFINITE;
      bool inShadow       = traceShadow(shadowRay, payload);
      if(!inShadow)
      {
        radiance += contrib;
      }
    }
  }

  return radiance;
}

//-----------------------------------------------------------------------
// Sampling the pixel
//-----------------------------------------------------------------------
float3 samplePixel(inout uint seed, inout HitPayload payload)
{
  float2 launchID   = (float2)DispatchRaysIndex().xy;
  float2 launchSize = float2(DispatchRaysDimensions().xy);

  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // Subpixel jitter: send the ray through a different position inside the pixel each time, to provide antialiasing.
  // Jitter is relative to the pixel center (which `generatePrimaryRay` already targets), so the offset stays in [-0.5, 0.5).
  const float2 subpixelJitter = pushConst.frame == 0 ? float2(0.0f, 0.0f) : float2(rand(seed) - 0.5f, rand(seed) - 0.5f);
  RayDesc ray = generatePrimaryRay(launchID + subpixelJitter, launchSize, sceneInfo[0]);

  float3 radiance = pathTrace(ray, seed, payload);

  // Removing fireflies
  float       lum                   = dot(radiance, float3(0.212671F, 0.715160F, 0.072169F));
  const float fireflyClampThreshold = 10.0f;
  if(lum > fireflyClampThreshold)
  {
    radiance *= fireflyClampThreshold / lum;
  }

  return radiance;
}

//-----------------------------------------------------------------------
// RAY GENERATION
//-----------------------------------------------------------------------
[shader("raygeneration")]
void rgenMain()
{
  float2 launchID   = (float2)DispatchRaysIndex().xy;
  float2 launchSize = (float2)DispatchRaysDimensions().xy;

  // Initialize the random number
  uint seed = xxhash32(uint3(uint2(launchID.xy), pushConst.frame));

  HitPayload payload    = {};
  float3     pixelColor = float3(0.0F, 0.0F, 0.0F);

  // Single sample per pixel for this version
  pixelColor = samplePixel(seed, payload);

  // Saving result
  bool firstFrame = (pushConst.frame == 0);
  if(firstFrame)
  {  // First frame, replace the value in the buffer
    outImage[int2(launchID)] = float4(pixelColor, 1.0);
  }
  else
  {  // Do accumulation over time
    float  a                 = 1.0F / float(pushConst.frame + 1);
    float3 old_color         = outImage[int2(launchID)].xyz;
    outImage[int2(launchID)] = float4(lerp(old_color, pixelColor, a), 1.0F);
  }
}

//-----------------------------------------------------------------------
// CLOSEST HIT
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  uint          instanceID = InstanceIndex();
  GltfSceneInfo sceneInfo  = pushConst.sceneInfoAddress[0];
  GltfInstance  instance   = sceneInfo.instances[instanceID];
  GltfMesh      mesh       = sceneInfo.meshes[instance.meshIndex];

  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

  HitState hit          = getHitState(mesh, PrimitiveIndex(), ObjectToWorld4x3(), WorldToObject4x3(), barycentrics);
  payload.hitT          = RayTCurrent();
  payload.pos           = hit.pos;
  payload.nrm           = hit.nrm;
  payload.instanceIndex = instanceID;
}

//-----------------------------------------------------------------------
// MISS
//-----------------------------------------------------------------------
[shader("miss")]
void rmissMain(inout HitPayload payload)
{
  payload.hitT = INFINITE;
}
