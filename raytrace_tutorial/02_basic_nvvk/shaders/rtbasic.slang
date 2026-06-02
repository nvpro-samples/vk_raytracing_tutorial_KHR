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

#include "shaderio.h"

#include "common/shaders/pbr.h.slang"
#include "nvshaders/sky_functions.h.slang"

#define MISS_DEPTH 1000


// clang-format off
// Push constants containing scene information, camera data, and material overrides
[[vk::push_constant]]                           ConstantBuffer<TutoPushConstant> pushConst;
// Texture array for material textures (albedo, normal maps, etc.)
[[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
// Top-level acceleration structure containing the scene geometry hierarchy
[[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
// Output image where the final rendered result will be stored
[[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

// Ray payload structure - carries data through the ray tracing pipeline
struct HitPayload
{
  float3 color  = float3(0);  // Accumulated color along the ray path
  float  weight = 1;          // Weight/importance of this ray (for importance sampling)
  int    depth  = 0;          // Current recursion depth (for limiting bounces)
};

// Generic function to retrieve vertex attributes from GLTF buffer data
// T: Type of attribute (float, float2, float3, etc.)
// dataBufferAddress: Base address of the GLTF buffer
// bufferView: Buffer view containing offset, stride, and count information
// attributeIndex: Index of the vertex attribute to retrieve
__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint attributeIndex)
{
  if(bufferView.count > 0)
  {
    // Calculate pointer to the specific attribute using offset and stride
    T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
    return ptr[0];
  }

  return T(1);  // Error case - return default value
}

// Retrieve triangle vertex indices from the mesh index buffer
// Supports both 16-bit and 32-bit index formats as per GLTF specification
uint3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh, uint primitiveID)
{
  if(mesh.indices.byteStride == sizeof(uint16_t))
  {
    // 16-bit indices (uint16_t) - more memory efficient for smaller meshes
    uint16_t3* indices = (uint16_t3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if(mesh.indices.byteStride == sizeof(uint32_t))
  {
    // 32-bit indices (uint32_t) - supports larger meshes with more vertices
    uint3* indices = (uint3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return uint3(0xFFFFFFFFu);  // Error case - invalid index format
}

// Interpolate vertex attributes across a triangle using barycentric coordinates
// This performs smooth interpolation of attributes like position, normal, UV coordinates
// T: Type of attribute to interpolate (float, float2, float3, etc.)
// attributeIndex: Indices of the three triangle vertices
// barycentrics: Barycentric coordinates (weights for each vertex)
__generic<T : IFloat> T getTriangleAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint3 attributeIndex, float3 barycentrics)
{
  // Get the attribute value for each of the three triangle vertices
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);

  // Interpolate using barycentric coordinates (weights sum to 1.0)
  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

//-----------------------------------------------------------------------
// SHADOW TESTING - Determines if a point is in shadow from a light source
//-----------------------------------------------------------------------
// Tests visibility between a surface point and a light source
// Returns 1.0 if fully lit, 0.0 if fully shadowed
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, GltfPunctual light)
{
  // Create shadow ray from surface point towards light source
  RayDesc shadowRay;
  // Offset origin slightly along normal to avoid self-intersection artifacts
  shadowRay.Origin    = worldPos + worldNormal * 0.001;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;  // Minimum distance to avoid self-intersection
  // For directional lights, trace to infinity; for point/spot lights, trace to light position
  shadowRay.TMax = light.type == GltfLightType::eDirectional ? INFINITE : length(lightDirection);

  // Create a minimal payload for shadow ray (we only need to know if it hits something)
  HitPayload shadowPayload = {};

  // Trace the shadow ray with optimized flags for performance:
  // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: Stop at first intersection (we don't need closest)
  // RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: Skip expensive shading calculations
  TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0,
           shadowRay, shadowPayload);

  // If the shadow ray hit something (depth != MISS_DEPTH), the light is occluded
  return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

//-----------------------------------------------------------------------
// LIGHT PROCESSING - Handles sky override, distance attenuation, and spot light cone effects
//-----------------------------------------------------------------------
// Processes light properties based on type and applies appropriate attenuation
GltfPunctual processLight(GltfSceneInfo sceneInfo, float3 worldPos)
{
  // Start with the first punctual light in the scene (simplified for tutorial)
  GltfPunctual light = sceneInfo.punctualLights[0];

  // Sky override: Replace punctual light with sun parameters from sky system
  // This allows using procedural sky lighting instead of manual light setup
  if(sceneInfo.useSky == 1)
  {
    light.direction = sceneInfo.skySimpleParam.sunDirection;  // Sun direction from sky
    light.color     = sceneInfo.skySimpleParam.sunColor;      // Sun color from sky
    light.intensity = sceneInfo.skySimpleParam.sunIntensity;  // Sun intensity from sky
    light.type      = GltfLightType::eDirectional;            // Sun is always directional
  }

  // Point light: Calculate direction from light to surface and apply distance attenuation
  if(light.type == GltfLightType::ePoint)
  {
    // Direction from surface point to light position
    light.direction = light.position - worldPos;
    float d         = length(light.direction);
    // Apply inverse square law: intensity decreases with distance squared
    light.intensity /= (d * d);
  }
  // Spot light: Calculate direction, apply distance attenuation and cone falloff
  else if(light.type == GltfLightType::eSpot)
  {
    // Direction from surface point to light position
    float3 lightDir = light.position - worldPos;
    float  d        = length(lightDir);
    // Apply distance attenuation (inverse square law)
    light.intensity /= (d * d);

    // Calculate cone attenuation based on angle from spot light direction
    // theta: cosine of angle between light direction and spot direction
    float theta = dot(normalize(lightDir), normalize(light.direction));
    // Smooth falloff from inner cone (1.0) to outer cone (0.0)
    float spotIntensity = clamp((theta - cos(light.coneAngle)) / (1.0 - cos(light.coneAngle)), 0.0, 1.0);
    light.intensity *= spotIntensity;
    light.direction = lightDir;
  }

  return light;
}

//-----------------------------------------------------------------------
// RAY GENERATION SHADER - Entry point for each pixel in the output image
//-----------------------------------------------------------------------
[shader("raygeneration")]
void rgenMain()
{
  // Get the current pixel coordinates and image dimensions
  float2 launchID   = (float2)DispatchRaysIndex().xy;       // Current pixel (x,y)
  float2 launchSize = (float2)DispatchRaysDimensions().xy;  // Image size (width,height)

  // Retrieve scene information from push constants
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // Set ray tracing flags (0 = no special flags)
  const uint rayFlags = RAY_FLAG_NONE;

  // Convert pixel coordinates to normalized device coordinates (NDC)
  // Range: [-1,1] for both x and y
  const float2 clipCoords = launchID / launchSize * 2.0 - 1.0;

  // Transform from NDC to view space using inverse projection matrix
  const float4 viewCoords = mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);

  // Create the primary ray
  RayDesc ray;
  // Transform camera origin (0,0,0) from view space to world space
  ray.Origin = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;
  // Transform ray direction from view space to world space
  ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;
  ray.TMin      = 0.001;     // Minimum distance to avoid self-intersection
  ray.TMax      = INFINITE;  // Maximum distance (infinite for primary rays)

  // Initialize ray payload with default values
  HitPayload payload = {};

  // Cast the ray into the scene using the acceleration structure
  // Parameters: AS, flags, instance mask, sbt offset, sbt stride, miss offset, ray, payload
  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);

  // Get the final color from the ray tracing result
  float3 color = payload.color;

  // Write the result to the output image
  outImage[int2(launchID)] = float4(color, 1.0);
}

//-----------------------------------------------------------------------
// CLOSEST HIT SHADER - Called when ray hits geometry (performs shading)
//-----------------------------------------------------------------------
[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // Convert built-in barycentric coordinates to full barycentric coordinates
  // Built-in provides (u,v) where u+v+w=1, so w = 1-u-v
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

  // Get instance and primitive information from ray tracing built-ins
  uint instanceID = InstanceIndex();   // Which instance was hit
  uint meshID     = InstanceID();      // Instance ID (same as instanceID in this case)
  uint triID      = PrimitiveIndex();  // Which triangle within the mesh was hit

  // Retrieve scene data structures
  GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
  GltfInstance          instance  = sceneInfo.instances[instanceID];              // Instance data
  GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];         // Mesh data
  GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];  // Material data

  // Interpolate vertex attributes across the hit triangle
  uint3  indices       = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos           = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);
  float3 nrm           = getTriangleAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float2 worldTexCoord = getTriangleAttribute<float2>(mesh.gltfBuffer, mesh.triMesh.texCoords, indices, barycentrics);

  // Transform from object space to world space
  float3 worldPos    = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
  float3 worldNormal = normalize(mul(WorldToObject4x3(), nrm).xyz);

  // Extract material properties (PBR parameters)
  float3 albedo    = material.baseColorFactor.xyz;  // Base color/albedo
  float  metallic  = material.metallicFactor;       // Metallic factor (0=dielectric, 1=metal)
  float  roughness = material.roughnessFactor;      // Surface roughness (0=smooth, 1=rough)

  // Sample albedo texture if available
  if(material.baseColorTextureIndex > -1)
  {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(worldTexCoord, 0).xyz;
  }

  // Apply material overrides from push constants (for debugging/experimentation)
  if(pushConst.metallicRoughnessOverride.x >= 0.0)
    metallic = pushConst.metallicRoughnessOverride.x;
  if(pushConst.metallicRoughnessOverride.y >= 0.0)
    roughness = pushConst.metallicRoughnessOverride.y;

  // Process light with sky override and distance attenuation
  GltfPunctual light = processLight(sceneInfo, worldPos);

  // Calculate lighting vectors
  float3 N = normalize(worldNormal);      // Surface normal
  float3 V = -WorldRayDirection();        // View direction (towards camera)
  float3 L = normalize(light.direction);  // Light direction

  // Test for shadows by casting a shadow ray towards the light
  float shadowFactor = testShadow(worldPos, N, light.direction, light);

  // Calculate PBR lighting using the metallic-roughness model
  // This computes the final color based on material properties and lighting
  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);

  // Apply light color, intensity, and shadow factor
  color *= light.color * light.intensity * shadowFactor;

  // Set the final color in the payload
  payload.color = color;
}

//-----------------------------------------------------------------------
// MISS SHADER - Called when ray doesn't hit any geometry (background/sky)
//-----------------------------------------------------------------------
[shader("miss")]
void rmissMain(inout HitPayload payload)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // Check if sky system is enabled
  if(sceneInfo.useSky == 1)
  {
    // Evaluate procedural sky color based on ray direction
    // This creates realistic sky gradients based on sun position and atmospheric scattering
    float3 skyColor = evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
    payload.color += skyColor * payload.weight;
  }
  else
  {
    // Use simple solid background color when sky system is disabled
    payload.color += sceneInfo.backgroundColor * payload.weight;
  }

  // Set depth to MISS_DEPTH to indicate this ray has finished (no more bounces)
  payload.depth = MISS_DEPTH;
}
