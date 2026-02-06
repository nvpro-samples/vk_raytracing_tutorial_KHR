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

#include "common/shaders/pbr.h.slang"
#include "nvshaders/bsdf_functions.h.slang"
#include "nvshaders/constants.h.slang"
#include "nvshaders/random.h.slang"
#include "nvshaders/ray_utils.h.slang"
#include "nvshaders/sky_functions.h.slang"

#include "shaderio.h"

// clang-format off
 [[vk::push_constant]] ConstantBuffer<TutoPushConstant> pushConst;

 [[vk::constant_id(0)]] int USE_SER = 1;

 [[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
 [[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
 [[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
 [[vk::binding(BindingPoints::eHeatStats, 1)]]   RWStructuredBuffer<HeatStats> heatStats;
 [[vk::binding(BindingPoints::eHeatmap, 1)]]     RWTexture2D<float4> outHeatmap;

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

__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint attributeIndex)
{
  if(bufferView.count > 0)
  {
    T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
    return ptr[0];
  }

  return T(1);  // Error case
}

int3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh, int primitiveID)
{
  if(mesh.indices.byteStride == sizeof(int16_t))
  {
    int16_t3* indices = (int16_t3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if(mesh.indices.byteStride == sizeof(int32_t))
  {
    int3* indices = (int3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return int3(-1);  // Error case
}

__generic<T : IFloat> T getTriangleAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint3 attributeIndex, float3 barycentrics)
{
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);
  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

//-----------------------------------------------------------------------
// Return hit position, normal and geometric normal in world space
HitState getHitState(GltfMesh mesh, uint triID, float4x3 objectToWorld, float4x3 worldToObject, float3 barycentrics)
{
  HitState hit;

  // Retrieve the data
  int3   indices     = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos         = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);
  float3 nrm         = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float3 worldPos    = float3(mul(float4(pos, 1.0), objectToWorld));
  float3 worldNormal = normalize(mul(worldToObject, nrm).xyz);
  hit.pos            = worldPos;
  hit.nrm            = worldNormal;
  return hit;
}

// Utility for temperature and landscapeColor:
// Smoothly transforms the range [low, high] to [0, 1], with 0 derivative at
// low, high, and (low + high) * 0.5.
float fade(float low, float high, float value)
{
  float mid   = (low + high) * 0.5;
  float range = (high - low) * 0.5;
  float x     = 1.0 - clamp(abs(mid - value) / range, 0.0, 1.0);
  return smoothstep(0.0, 1.0, x);
}

// Return a cold-hot color based on intensity [0-1]
float3 temperature(float intensity)
{
  const float3 blue   = float3(0.0, 0.0, 1.0);
  const float3 cyan   = float3(0.0, 1.0, 1.0);
  const float3 green  = float3(0.0, 1.0, 0.0);
  const float3 yellow = float3(1.0, 1.0, 0.0);
  const float3 red    = float3(1.0, 0.0, 0.0);

  float3 color = (fade(-0.25, 0.25, intensity) * blue    //
                  + fade(0.0, 0.5, intensity) * cyan     //
                  + fade(0.25, 0.75, intensity) * green  //
                  + fade(0.5, 1.0, intensity) * yellow   //
                  + smoothstep(0.75, 1.0, intensity) * red);
  return color;
}

uint64_t ReadClock()
{
  return uint64_t(getRealtimeClock().x) | (uint64_t(getRealtimeClock().y) << 32);
}

//-----------------------------------------------------------------------
// Shoot a ray an return the information of the closest hit, in the
// PtPayload structure (PRD)
//
void traceRay(RayDesc ray, inout HitPayload payload, int depth)
{
  payload.hitT  = 0.0F;
  ray.TMax      = INFINITE;
  uint rayFlags = 0;
  if(USE_SER == 1)
  {
    HitObject hitObj = HitObject::TraceRay(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray, payload);

    uint materialID = hitObj.GetInstanceID();  // Get the material ID from the instance ID (custom index)
    // Using 7 bits for the material ID and 3 bits for the depth:
    uint coherenceHint = (materialID << 3) | (uint(depth) & 7);
    ReorderThread(hitObj, coherenceHint, 10);

    float2 barycentrics = hitObj.GetAttributes<float2>();

    RayDesc rayDesc = hitObj.GetRayDesc();
    payload.hitT    = rayDesc.TMax;

    // Either this code or call Invoke() to get the hit information
    // This code is not needed if we use the Invoke() method
    if(hitObj.IsHit())
    {
      uint     instanceID    = hitObj.GetInstanceIndex();
      uint     triID         = hitObj.GetPrimitiveIndex();
      float4x3 objectToWorld = hitObj.GetObjectToWorld();
      float4x3 worldToObject = hitObj.GetWorldToObject();

      float3 barycentrics = float3(1 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

      GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
      GltfInstance          instance  = sceneInfo.instances[instanceID];
      GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];
      GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

      HitState hit = getHitState(mesh, triID, objectToWorld, worldToObject, barycentrics);

      payload.pos           = hit.pos;
      payload.nrm           = hit.nrm;
      payload.instanceIndex = hitObj.GetInstanceIndex();
    }

    // HitObject::Invoke(topLevelAS, hitObj, payload);
  }
  else
  {
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  }
}

//-----------------------------------------------------------------------
// Shadow ray - return true if a ray hits anything
//
bool traceShadow(RayDesc ray, inout HitPayload payload)
{
  payload.hitT  = 0.0F;
  uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
  bool isHit;
  if(USE_SER == 1)
  {
    HitObject hit = HitObject::TraceRay(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray, payload);
    isHit         = hit.IsHit();
  }
  else
  {
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
    isHit = payload.hitT < INFINITE;
  }
  return isHit;
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
float3 pathTrace(RayDesc ray, inout uint seed, inout HitPayload payload)
{
  float3 radiance   = float3(0.0F, 0.0F, 0.0F);
  float3 throughput = float3(1.0F, 1.0F, 1.0F);

  for(int depth = 0; depth < pushConst.maxDepth; depth++)
  {
    traceRay(ray, payload, depth);

    // Hitting the environment, then exit
    if(payload.hitT == INFINITE)
    {
      GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

      float3 sky_color = evalSimpleSky(sceneInfo.skySimpleParam, ray.Direction);
      return radiance + (sky_color * throughput);
    }

    // Retrieve the Instance buffer information
    GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
    GltfInstance          instance  = sceneInfo.instances[payload.instanceIndex];
    GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];
    GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

    // Retrieve the material color
    float4 albedo = material.baseColorFactor;

    float  pdf = 0.0F;
    float3 V   = -ray.Direction;
    float3 L   = normalize(sceneInfo.skySimpleParam.sunDirection);

    // Setting up the material
    float metallic  = pushConst.metallicRoughnessOverride.x;
    float roughness = max(0.0001f, pushConst.metallicRoughnessOverride.y);

    PbrBaseMaterial pbrMat = initPbrBaseMaterial(albedo.xyz, metallic, roughness, payload.nrm, payload.nrm);


    // Add dummy divergence
    // This sample shader is too simple to show the benefits of sorting
    // rays, with this we create some artificial workload depending on the
    // instance ID
    float3 dummy     = payload.nrm;
    uint   dummyLoop = (payload.instanceIndex * 128) & (1024 - 1);
    for(uint i = 0; i < dummyLoop; i++)
    {
      dummy = sin(dummy);
    }

    pbrMat.baseColor.xyz += dummy * 0.01;

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
  const float2 subpixelJitter = pushConst.frame == 0 ? float2(0.5f, 0.5f) : float2(rand(seed), rand(seed));
  const float2 pixelCenter    = launchID + subpixelJitter;
  const float2 clipCoords     = pixelCenter / launchSize * 2.0 - 1.0;
  const float4 viewCoords     = mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);

  RayDesc ray;
  ray.Origin    = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;
  ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;
  ray.TMin      = 0.001;
  ray.TMax      = INFINITE;

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

  uint64_t start = ReadClock();  // Debug - Heatmap

  // Initialize the random number
  uint seed = xxhash32(uint3(uint2(launchID.xy), pushConst.frame));

  HitPayload payload    = {};
  float3     pixelColor = float3(0.0F, 0.0F, 0.0F);
  for(int s = 0; s < pushConst.maxSamples; s++)
  {
    pixelColor += samplePixel(seed, payload);
  }
  pixelColor /= pushConst.maxSamples;

  // Debug - Heatmap
  {
    uint64_t end      = ReadClock();
    uint     duration = uint(end - start);

    // log maximum of current frame
    uint statsIndex = uint(pushConst.frame) & 1;
    InterlockedMax(heatStats[0].maxDuration[statsIndex], duration);
    // use previous frame's maximum
    uint maxDuration = heatStats[0].maxDuration[statsIndex ^ 1];

    // lower ceiling to see some more red ;)
    float high = float(maxDuration) * 0.50;

    float  val       = clamp(float(duration) / high, 0.0F, 1.0F);
    float3 heatColor = temperature(val);

    // Wrap & SM visualization
    // heat_color = temperature(float(gl_SMIDNV) / float(gl_SMCountNV - 1)) * float(gl_WarpIDNV) / float(gl_WarpsPerSMNV - 1);

    outHeatmap[int2(launchID)] = float4(heatColor, 1.0);
  }

  // Saving result
  bool firstFrame = (pushConst.frame == 0);
  if(firstFrame)
  {  // First frame, replace the value in the buffer
    outImage[int2(launchID)] = float4(pixelColor, 1.0);
  }
  else
  {  // Do accumulation over time
    float  a                 = 1.0F / float(pushConst.frame + 1);
    float3 old_color         = outImage[int2(launchID)].xyz;  // imageLoad(image, ivec2(gl_LaunchIDEXT.xy)).xyz;
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
