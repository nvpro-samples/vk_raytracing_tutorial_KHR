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
#include "nvshaders/sky_functions.h.slang"
#include "shaderio.h"

// clang-format off
 [[vk::push_constant]]                           ConstantBuffer<TutoPushConstant> pushConst;
 [[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
 [[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
 [[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

#define MISS_DEPTH 1000

// Raytracing Payload
struct HitPayload
{
  float3 color  = float3(0);
  float  weight = 1;
  int    depth  = 0;
  uint   seed   = 0;
  // Time of the current primary motion sample, carried through to secondary
  // (shadow) rays so they hit the scene at the same moment in time.
  float time = 0.0;
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

  // Initialize the random number generator
  payload.seed = xxhash32(uint3(DispatchRaysIndex().xy, 0));

  // Accumulate multiple samples for motion blur
  float  time;
  float3 result = float3(0, 0, 0);
  for(int i = 0; i < pushConst.numSamples; i++)
  {
    // Generate a random time between 0.0 and 1.0 for motion blur
    time = rand(payload.seed);

    // Initial state for each sample
    payload.color  = float3(0, 0, 0);
    payload.weight = 1;
    payload.depth  = 0;
    // Forward the primary sample time so shadow rays see the same scene snapshot
    payload.time = time;

    // Trace ray with time parameter for motion blur
    TraceMotionRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, time, payload);
    result += payload.color;
  }

  // Average the accumulated samples and apply tonemapping
  float3 color             = result / float(pushConst.numSamples);
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// SHADOW TESTING
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, GltfPunctual light, float time)
{
  RayDesc shadowRay;
  shadowRay.Origin    = worldPos + worldNormal * 0.001;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;
  shadowRay.TMax      = light.type == GltfLightType::eDirectional ? INFINITE : length(lightDirection);

  // Create a simple payload for shadow ray
  HitPayload shadowPayload = {};

  // Trace the shadow ray at the SAME time as the primary motion sample, so the
  // shadow is computed against the exact geometry snapshot we just hit.
  TraceMotionRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0,
                 shadowRay, time, shadowPayload);

  // If the shadow ray hit something, the light is occluded
  return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

//-----------------------------------------------------------------------
// Handles sky override, point light distance attenuation, and spot light cone attenuation
//-----------------------------------------------------------------------
GltfPunctual processLight(GltfSceneInfo sceneInfo, float3 worldPos)
{
  GltfPunctual light = sceneInfo.punctualLights[0];  // Assuming we only use the first light for simplicity

  // Sky override: replace punctual light with sun parameters
  if(sceneInfo.useSky == 1)
  {
    light.direction = sceneInfo.skySimpleParam.sunDirection;
    light.color     = sceneInfo.skySimpleParam.sunColor;
    light.intensity = sceneInfo.skySimpleParam.sunIntensity;
    light.type      = GltfLightType::eDirectional;
  }

  // Point light: calculate direction and apply distance attenuation
  if(light.type == GltfLightType::ePoint)
  {
    light.direction = light.position - worldPos;
    float d         = length(light.direction);
    light.intensity /= (d * d);
  }
  // Spot light: calculate direction, apply distance and cone attenuation
  else if(light.type == GltfLightType::eSpot)
  {
    float3 lightDir = light.position - worldPos;
    float  d        = length(lightDir);
    light.intensity /= (d * d);
    // Cone attenuation
    float theta         = (dot(normalize(lightDir), normalize(light.direction)));
    float spotIntensity = clamp((theta - cos(light.coneAngle)) / (1.0 - cos(light.coneAngle)), 0.0, 1.0);
    light.intensity *= spotIntensity;
    light.direction = lightDir;
  }

  return light;
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
  float3 N = normalize(worldNormal);
  float3 L = normalize(light.direction);

  // Test for shadows (use the primary sample's time, carried via the payload)
  float shadowFactor = testShadow(worldPos, N, light.direction, light, payload.time);

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
