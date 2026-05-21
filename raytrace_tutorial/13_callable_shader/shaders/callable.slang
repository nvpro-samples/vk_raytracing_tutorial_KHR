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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common/shaders/gltf_accessor.h.slang"
#include "common/shaders/light.h.slang"
#include "common/shaders/pbr.h.slang"
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

// Raytracing Payload
struct HitPayload
{
  float3 color  = float3(0);
  float  weight = 1;
  int    depth  = 0;
};

// Hit state information
struct HitState
{
  float3 pos;
  float3 nrm;
  float3 geonrm;
};

// Payload structure for callable shaders
struct CallablePayload
{
  float3  color;        // Computed material color
  RayDesc ray;          // Ray for next bounce (reflection/refraction)
  float   weight;       // Weight multiplier for ray absorption
  float3  worldPos;     // Hit position in world space
  float3  worldNormal;  // Surface normal in world space
  float3  viewDir;      // View direction for ray calculation
  float3  lightDir;     // Light direction
  float3  lightColor;   // Light color
};

struct TextureCallablePayload
{
  float3 color;
  float2 uv;
};


//-----------------------------------------------------------------------
// PROCEDURAL TEXTURE CALLABLE SHADERS
//-----------------------------------------------------------------------

[shader("callable")]
void textureNoiseMain(inout TextureCallablePayload payload)
{
  float2 uv   = payload.uv;
  float  time = pushConst.time * 0.5;

  // Create animated wavy distortion
  float2 wave1 = float2(sin(uv.x * 8.0 + time * 2.0) * 0.1, cos(uv.y * 6.0 + time * 1.5) * 0.1);
  float2 wave2 = float2(cos(uv.x * 12.0 + time * 3.0) * 0.05, sin(uv.y * 10.0 + time * 2.5) * 0.05);

  // Combine waves for complex distortion
  float2 distortedUV = uv + wave1 + wave2;

  // Create polka dot pattern
  float2 dotUV    = distortedUV * 15.0;  // Scale for dots
  float2 dotPos   = floor(dotUV);
  float2 dotFract = fract(dotUV);

  // Animate dot positions
  float2 animatedPos = dotPos + float2(sin(time + dotPos.x * 0.5) * 0.3, cos(time + dotPos.y * 0.7) * 0.3);

  // Calculate distance to nearest dot center
  float2 toCenter = dotFract - 0.5;
  float  dist     = length(toCenter);

  // Create soft dots with animated size
  float dotSize = 0.3 + 0.1 * sin(time * 2.0 + dotPos.x + dotPos.y);
  float dot     = smoothstep(dotSize, dotSize - 0.1, dist);

  // Add some variation to dot colors
  float3 dotColor1 = float3(0.8, 0.2, 0.6);  // Pink
  float3 dotColor2 = float3(0.2, 0.8, 0.4);  // Green
  float3 dotColor3 = float3(0.6, 0.4, 0.9);  // Purple

  // Alternate colors based on position and time
  float  colorIndex = sin(dotPos.x * 0.7 + dotPos.y * 0.5 + time) * 0.5 + 0.5;
  float3 dotColor   = lerp(dotColor1, dotColor2, colorIndex);
  dotColor          = lerp(dotColor, dotColor3, sin(time + dotPos.x * 0.3) * 0.5 + 0.5);

  // Combine dots with background
  float3 finalColor = lerp(float3(0.1, 0.1, 0.15), dotColor, dot);

  // Add some sparkle effect
  float sparkle = sin(uv.x * 50.0 + time * 4.0) * sin(uv.y * 50.0 + time * 3.0);
  sparkle       = smoothstep(0.95, 1.0, sparkle) * 0.3;
  finalColor += sparkle;

  payload.color *= finalColor;
}


[shader("callable")]
void textureCheckerMain(inout TextureCallablePayload payload)
{
  float2 uv      = payload.uv * 8.0;
  float2 checker = floor(uv) % 2.0;
  float  pattern = (checker.x + checker.y) % 2.0;

  payload.color *= lerp(float3(0.8, 0.8, 0.8), float3(0.2, 0.2, 0.2), pattern);
}

[shader("callable")]
void textureVoronoiMain(inout TextureCallablePayload payload)
{
  float2 uv = payload.uv * 16.0;
  float2 p  = floor(uv);
  float2 f  = fract(uv);

  float  minDist = 1.0;
  float3 color   = float3(0, 0, 0);

  for(int i = -1; i <= 1; i++)
  {
    for(int j = -1; j <= 1; j++)
    {
      float2 neighbor = float2(i, j);
      float2 point    = random2(p + neighbor);
      point           = 0.5 + 0.5 * sin(pushConst.time + 6.2831 * point);
      float2 diff     = neighbor + point - f;
      float  dist     = length(diff);

      if(dist < minDist)
      {
        minDist = dist;
        color   = 0.5 + 0.5 * cos(pushConst.time + point.xyx + float3(0, 2, 4));
      }
    }
  }

  payload.color *= color;
}

float2 random2(float2 p)
{
  uint3 v = uint3(asuint(p), 0);
  // PCG3D from https://github.com/markjarzynski/pcg3d.
  // Although PCG2D may look more appropriate, it turns out PCG3D is
  // higher-quality and slightly faster when used in this way (and the
  // compiler removes the extra assignment to z at the end).
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  // Simple conversion back to unorm
  return float2(v.xy) * 0x1p-32;
}

//-----------------------------------------------------------------------
// MATERIAL CALLABLE SHADERS
//-----------------------------------------------------------------------

[shader("callable")]
void material_diffuse_main(inout CallablePayload payload)
{
  // Diffuse material - simple Lambertian lighting
  payload.weight = 0.0;  // Absorb all light

  float dotNL = max(dot(payload.worldNormal, payload.lightDir), 0.0);
  payload.color *= dotNL * payload.lightColor;
}

[shader("callable")]
void materialPlasticMain(inout CallablePayload payload)
{
  // Metallic material - create reflection ray
  float metallic = 0.25f;
  payload.weight = metallic * metallic;  // More it is reflective, the less it absorbes

  // Calculate reflection ray
  float3 R              = reflect(-payload.viewDir, payload.worldNormal);
  payload.ray.Origin    = payload.worldPos + payload.worldNormal * 0.001;
  payload.ray.Direction = R;
  payload.ray.TMin      = 0.001;
  payload.ray.TMax      = 1000.0;

  // Metallic material has high reflectivity
  payload.color = pbrMetallicRoughness(payload.color, metallic, 0.5, payload.worldNormal, payload.viewDir, payload.lightDir);
  payload.color *= payload.lightColor;
}

[shader("callable")]
void materialGlassMain(inout CallablePayload payload)
{
  // Glass material - create refraction ray
  float transparency = 0.9f;  // 90% transparent
  payload.weight     = 1.0;   // Absorb only 10% of light (glass is mostly transparent)

  float3 D   = -payload.viewDir;
  float  ior = 1.0 / 1.125;  // Glass IOR

  // Flip normal in the same direction as the view direction
  if(dot(D, payload.worldNormal) > 0.0)
  {
    payload.worldNormal = -payload.worldNormal;
    ior                 = 1.0 / ior;
  }
  // Calculate fresnel effect for reflection/refraction balance
  float cosTheta = abs(dot(payload.viewDir, payload.worldNormal));
  float fresnel  = pow(1.0 - cosTheta, 5.0);

  // Add some base reflectivity for glass (about 4% at normal incidence)
  float baseReflectivity = 0.04;
  fresnel                = baseReflectivity + (1.0 - baseReflectivity) * fresnel;

  // Calculate refraction ray
  float3 T = refract(D, payload.worldNormal, ior);

  if(length(T) > 0.0)  // Check if refraction is possible
  {
    // Create refraction ray
    payload.ray.Origin    = payload.worldPos - payload.worldNormal * 0.001;
    payload.ray.Direction = T;
    payload.color         = (1 - transparency) * payload.color * (1.0 - fresnel);  //* payload.lightColor;
  }
  else
  {
    // Total internal reflection if refraction fails
    float3 R              = reflect(D, payload.worldNormal);
    payload.ray.Origin    = payload.worldPos + payload.worldNormal * 0.001;
    payload.ray.Direction = R;
    payload.color         = transparency * payload.color * payload.lightColor * fresnel;
  }
  // Fake
  float3 R        = reflect(D, payload.worldNormal);
  float  specular = pow(max(dot(R, payload.lightDir), 0.0), 232.0);  // Phong specular
  payload.color += specular;

  payload.ray.TMin = 0.01;
  payload.ray.TMax = 1000.0;
}

[shader("callable")]
void materialConstantMain(inout CallablePayload payload)
{
  // Emissive material - emits its own light
  payload.weight = 0.0;  // Absorb all light

  // Emissive materials emit light regardless of lighting
  payload.color = payload.color;  // Bright emission
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
  float3 color             = payload.color;
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// SHADOW TESTING
// Local override: this sample returns 0.3 (not 0.0) in shadow to keep callable
// material colors visible in shadowed regions. See common/shaders/shadow.h.slang
// for the canonical 0.0/1.0 version used by the other tutorial samples.
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, GltfPunctual light)
{
  RayDesc shadowRay;
  shadowRay.Origin    = worldPos + worldNormal * 0.001;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;
  shadowRay.TMax      = light.type == GltfLightType::eDirectional ? INFINITE : length(lightDirection);

  HitPayload shadowPayload;
  shadowPayload.depth = 0;
  TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0,
           shadowRay, shadowPayload);

  return shadowPayload.depth != MISS_DEPTH ? 0.3 : 1.0;
}

//-----------------------------------------------------------------------
// CLOSEST HIT
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
  uint instanceID = InstanceIndex();
  uint materialID = InstanceID();
  uint triID      = PrimitiveIndex();

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
  float shadowFactor = testShadow(worldPos, N, light.direction, light);

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

  // Set up payload for callable shaders
  CallablePayload callablePayload;
  callablePayload.color       = albedo;
  callablePayload.worldPos    = worldPos;
  callablePayload.worldNormal = N;
  callablePayload.viewDir     = V;
  callablePayload.lightDir    = L;
  callablePayload.lightColor  = light.color * light.intensity * shadowFactor;

  // Execute material callable shader based on object material
  int materialType = materialID;
  CallShader<CallablePayload>(materialType, callablePayload);

  // Use the color computed by the material callable shader
  float3 color = callablePayload.color;

  // Apply texture if specified - (Other callable - different payload)
  if(pushConst.textureType != TextureType::eNone)
  {
    TextureCallablePayload texturePayload;
    texturePayload.color = color;
    texturePayload.uv    = worldTexCoord;

    int textureType = pushConst.textureType + MaterialType::eMaterialCount - 1;
    CallShader<TextureCallablePayload>(textureType, texturePayload);
    color = texturePayload.color;
  }

  // Add contribution
  payload.color += color * payload.weight;

  // Recursive bounce
  payload.depth += 1;
  payload.weight *= callablePayload.weight;  // more or less reflective

  // We hit our max depth
  if(payload.depth >= MAX_RECURSION_DEPTH)
    return;

  TraceRay(topLevelAS, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, callablePayload.ray, payload);
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
