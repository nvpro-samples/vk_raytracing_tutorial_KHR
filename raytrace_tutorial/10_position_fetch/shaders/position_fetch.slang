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
#include "nvshaders/constants.h.slang"
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
  float3 color;
  float  weight;
  int    depth;
};

// Hit state information
struct HitState
{
  float3 pos;
  float3 nrm;
  float3 geonrm;
};

// Position Fetch: Get hit state using built-in functions
HitState getHitStateWithPositionFetch(float3 barycentrics)
{
  HitState hit;

  // POSITION FETCH: Use built-in function to get vertex positions
  // This is the key feature - we can access vertex positions directly
  // from the acceleration structure without additional vertex buffers!
  const float3 pos0 = HitTriangleVertexPosition(0);  // First vertex
  const float3 pos1 = HitTriangleVertexPosition(1);  // Second vertex
  const float3 pos2 = HitTriangleVertexPosition(2);  // Third vertex

  // Interpolate position using barycentrics
  const float3 localPosition = pos0 * barycentrics.x + pos1 * barycentrics.y + pos2 * barycentrics.z;

  // Transform to world space
  hit.pos = float3(mul(float4(localPosition, 1.0), ObjectToWorld4x3()));

  // Calculate geometric normal from fetched positions
  const float3 edge1     = pos1 - pos0;
  const float3 edge2     = pos2 - pos0;
  const float3 geoNormal = normalize(cross(edge1, edge2));

  // Transform normal to world space
  float3 worldGeoNormal = normalize(mul(WorldToObject4x3(), geoNormal).xyz);
  hit.geonrm            = worldGeoNormal;
  hit.nrm               = worldGeoNormal;  // Using geometric normal as shading normal for simplicity

  return hit;
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

  const uint   rayFlags   = 0;
  const float2 clipCoords = launchID / launchSize * 2.0 - 1.0;
  const float4 viewCoords = mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);

  RayDesc ray;
  ray.Origin    = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;
  ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;
  ray.TMin      = 0.001;
  ray.TMax      = INFINITE;

  // Initial state
  HitPayload payload;
  payload.color  = float3(0, 0, 0);
  payload.weight = 1;
  payload.depth  = 0;

  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
  float3 color             = payload.color;
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// SHADOW TESTING
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, GltfPunctual light)
{
  RayDesc shadowRay;
  shadowRay.Origin    = worldPos + worldNormal * 0.001;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;
  shadowRay.TMax      = light.type == GltfLightType::eDirectional ? INFINITE : length(lightDirection);

  // Create a simple payload for shadow ray
  HitPayload shadowPayload;
  shadowPayload.depth = 0;

  // Trace the shadow ray
  TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0,
           shadowRay, shadowPayload);

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
  GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

  // POSITION FETCH: Get hit state using the position fetch functionality
  // This demonstrates the key benefit - we get all geometry data without vertex buffers!
  HitState hitState = getHitStateWithPositionFetch(barycentrics);

  // Process light with sky override and distance attenuation using common function
  GltfPunctual light = processLight(sceneInfo, hitState.pos);

  float3 V = -WorldRayDirection();
  normalize(sceneInfo.cameraPosition - hitState.pos);
  float3 N = normalize(hitState.nrm);
  float3 L = normalize(light.direction);

  // Test for shadows
  float shadowFactor = testShadow(hitState.pos, N, light.direction, light);

  // Get base color from material
  float3 albedo = material.baseColorFactor.xyz;

  // Apply material overrides from UI
  float metallic  = pushConst.metallicRoughnessOverride.x;
  float roughness = pushConst.metallicRoughnessOverride.y;

  // Calculate PBR lighting with sun's color and intensity
  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
  // Apply light color and intensity, but only if not in shadow
  color *= light.color * light.intensity * shadowFactor;

  // Visualization: Add a subtle tint to show this is using position fetch
  // This helps demonstrate that the technique is working
  float3 positionFetchTint = float3(0.1, 0.0, 0.1);  // Subtle purple tint
  color += positionFetchTint * 0.1;

  payload.color += color * payload.weight;

  // Recursive bounce
  payload.depth += 1;
  payload.weight *= metallic;  // more or less reflective

  // Reflection
  float3 reflectionDir = reflect(-V, hitState.nrm);

  // We hit our max depth
  if(payload.depth >= 2)  // Hardcoded max depth for simplicity
    return;

  // Trace bouncing ray
  RayDesc ray;
  ray.Origin    = hitState.pos;
  ray.Direction = reflectionDir;
  ray.TMin      = 0.001;
  ray.TMax      = INFINITE;
  TraceRay(topLevelAS, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);
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
