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

// Monte Carlo Path Tracer using Vulkan Ray Query Extension
//
// This shader implements a physically-based Monte Carlo path tracer using the Vulkan
// ray query extension. Unlike traditional ray tracing pipelines that use dedicated
// ray generation, closest hit, and miss shaders, this implementation uses inline
// ray tracing within a compute shader for greater flexibility and control.
//
// Key Features:
// - Unidirectional path tracing with importance sampling
// - Physically-based materials (PBR metallic-roughness workflow)
// - Multiple light types (directional, point, spot) with proper attenuation
// - Procedural sky system with sun/atmosphere simulation
// - Russian roulette path termination for unbiased rendering
// - Temporal accumulation for progressive refinement
// - Firefly clamping to reduce noise artifacts
//
// Performance Optimizations:
// - Inline ray queries avoid shader binding table overhead
// - Early path termination using Russian roulette
// - Optimized BSDF sampling and evaluation
// - Efficient random number generation using xxhash
//
// Mathematical Foundation:
// This implementation follows the rendering equation:
// L_o(p,ω_o) = L_e(p,ω_o) + ∫ f_r(p,ω_i,ω_o) L_i(p,ω_i) (n·ω_i) dω_i
//
// Where Monte Carlo integration is used to approximate the integral over
// the hemisphere of incoming light directions.

#include "common/shaders/pbr.h.slang"
#include "nvshaders/constants.h.slang"
#include "nvshaders/random.h.slang"
#include "nvshaders/ray_utils.h.slang"
#include "nvshaders/sky_functions.h.slang"


#include "shaderio.h"

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

#define MISS_DEPTH 1000  // Sentinel value indicating ray missed all geometry

// Hit state information - processed intersection data ready for shading
struct HitState
{
  float3 pos;        // World-space hit position
  float3 nrm;        // Shading normal (interpolated and transformed)
  float3 geonrm;     // Geometric normal (face normal, for back-face detection)
  float3 shadowPos;  // Shadow position
};

// Ray payload structure - carries intersection data through ray queries
// Unlike traditional RT pipelines, ray queries return this data directly
struct HitPayload
{
  float    hitT;           // Distance to intersection (INFINITE if no hit)
  int      instanceIndex;  // Index of hit instance in the scene
  HitState hit;            // Hit state information
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
int3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh, int primitiveID)
{
  if(mesh.indices.byteStride == sizeof(int16_t))
  {
    // 16-bit indices (uint16_t) - more memory efficient for smaller meshes
    int16_t3* indices = (int16_t3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if(mesh.indices.byteStride == sizeof(int32_t))
  {
    // 32-bit indices (uint32_t) - supports larger meshes with more vertices
    int3* indices = (int3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return int3(-1);  // Error case - invalid index format
}

// Interpolate vertex attributes across a triangle using barycentric coordinates
// This performs smooth interpolation of attributes like position, normal, UV coordinates
__generic<T : IFloat> T getInterpolated(float3 barycentrics, T attr0, T attr1, T attr2)
{
  // Barycentric interpolation: each vertex contributes based on its weight
  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

// Interpolate vertex attributes across a triangle using barycentric coordinates
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
// GEOMETRY PROCESSING - Convert ray intersection to shading data
//-----------------------------------------------------------------------
// Return hit position, normal and geometric normal in world space
// This function transforms object-space intersection data to world-space shading data
HitState getHitState(const GltfMesh mesh, float2 barycentricCoords, float4x3 worldToObject4x3, float4x3 objectToWorld4x3, int triID, float3 worldRayDirection)
{
  HitState hit;

  // Convert built-in barycentric coordinates to full barycentric coordinates
  // Built-in provides (u,v) where u+v+w=1, so w = 1-u-v
  const float3 barycentrics =
      float3(1.0 - barycentricCoords.x - barycentricCoords.y, barycentricCoords.x, barycentricCoords.y);


  // Get triangle vertex indices
  int3 indices = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);

  // Interpolate vertex positions and transform to world space
  float3 pos0   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.x);
  float3 pos1   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.y);
  float3 pos2   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, indices.z);
  float3 posObj = getInterpolated(barycentrics, pos0, pos1, pos2);
  hit.pos       = float3(mul(float4(posObj, 1.0), objectToWorld4x3));


  // Interpolate vertex normals and transform to world space
  // Note: normals are transformed using the inverse-transpose matrix
  float3 nrm0   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.x);
  float3 nrm1   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.y);
  float3 nrm2   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.z);
  float3 nrmObj = normalize(getInterpolated(barycentrics, nrm0, nrm1, nrm2));
  hit.nrm       = normalize(mul(worldToObject4x3, nrmObj).xyz);

  // Hacking the shadow terminator
  float3 shadowPos = pointOffset(posObj, pos0, pos1, pos2, nrm0, nrm1, nrm2, barycentrics);
  hit.shadowPos    = float3(mul(float4(shadowPos, 1.0), objectToWorld4x3));

  // Calculate geometric normal from triangle edges (face normal)
  float3 geonrmObj = normalize(cross(pos1 - pos0, pos2 - pos0));
  hit.geonrm       = normalize(mul(float4(geonrmObj, 0.0), objectToWorld4x3).xyz);
  // Ensure geometric normal faces towards the ray origin (front-facing)
  if(dot(hit.geonrm, worldRayDirection) > 0.0f)
    hit.geonrm = -hit.geonrm;  // Flip if back-facing

  // Ensure shading normal and geometric normal are on the same side
  // This prevents lighting artifacts on back-facing surfaces
  if(dot(hit.geonrm, hit.nrm) < 0)
  {
    hit.nrm = -hit.nrm;  // Flip shading normal to match geometric normal
  }

  // For low-tessellated geometry, prevent internal reflection artifacts
  float3 r = reflect(normalize(worldRayDirection), hit.nrm);
  if(dot(r, hit.geonrm) < 0)
    hit.nrm = hit.geonrm;

  return hit;
}

//-----------------------------------------------------------------------
// RAY TRACING - Inline ray query implementation
//-----------------------------------------------------------------------
// Shoot a ray and return the information of the closest hit
// Uses Vulkan ray query extension for inline ray tracing within compute shader
void traceRay(RayDesc ray, inout HitPayload payload)
{
  // Initialize inline ray query with no special flags
  RayQuery<RAY_FLAG_NONE> q;
  q.TraceRayInline(topLevelAS, RAY_FLAG_NONE, 0xFF, ray);

  // Process all potential intersections along the ray
  while(q.Proceed())
  {
    // For non-opaque triangles, force them to be opaque (simplified alpha handling)
    if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
      q.CommitNonOpaqueTriangleHit();  // Accept all intersections as solid
  }

  // Check if ray hit any geometry
  if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
  {
    // Extract intersection data from the committed hit
    float2   barycentricCoords = q.CommittedTriangleBarycentrics();  // (u,v) coordinates on triangle
    int      meshID            = q.CommittedInstanceID();            // Instance custom index
    int      triID             = q.CommittedPrimitiveIndex();        // Triangle index within mesh
    float4x3 worldToObject     = q.CommittedWorldToObject4x3();      // Transform matrix
    float4x3 objectToWorld     = q.CommittedObjectToWorld4x3();      // Inverse transform matrix
    float    hitT              = q.CommittedRayT();                  // Distance along ray
    int      instanceIndex     = q.CommittedInstanceIndex();         // Instance index in scene

    // Retrieve scene data structures for the hit geometry
    GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
    GltfInstance          instance  = sceneInfo.instances[instanceIndex];           // Instance data
    GltfMesh              mesh      = sceneInfo.meshes[instance.meshIndex];         // Mesh geometry
    GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];  // Material properties

    // Convert built-in barycentric coordinates to full barycentric coordinates
    const float3 barycentrics =
        float3(1.0 - barycentricCoords.x - barycentricCoords.y, barycentricCoords.x, barycentricCoords.y);

    // Process intersection data into world-space shading information
    HitState hitState = getHitState(mesh, barycentricCoords, worldToObject, objectToWorld, triID, ray.Direction);

    // Store intersection data in payload for further processing
    payload.hitT          = hitT;                // Distance to hit point
    payload.hit           = hitState;            // Hit state information
    payload.instanceIndex = instanceIndex;       // Instance index
  }
  else
  {
    // Ray missed all geometry - set distance to infinity
    payload.hitT = INFINITE;
  }
}

//-----------------------------------------------------------------------
// SHADOW TESTING - Optimized visibility queries
//-----------------------------------------------------------------------
// Shadow ray - return true if a ray hits anything
// Uses early termination flags for maximum performance
bool traceShadow(RayDesc ray)
{
  // Use optimized ray query for shadow testing - terminates at first hit
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
  q.TraceRayInline(topLevelAS, RAY_FLAG_NONE, 0xFF, ray);

  // Process intersections (simplified for shadow rays)
  while(q.Proceed())
  {
    // For non-opaque triangles, force them to be opaque (simplified alpha handling)
    if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
      q.CommitNonOpaqueTriangleHit();  // Accept intersection as solid
  }

  // Return true if any intersection was found (surface is in shadow)
  return (q.CommittedStatus() != COMMITTED_NOTHING);
}

//-----------------------------------------------------------------------
// LIGHT PROCESSING - Handles sky override, distance attenuation, and spot light cone effects
//-----------------------------------------------------------------------
// Processes light properties based on type and applies appropriate attenuation
GltfPunctual processLight(GltfSceneInfo* sceneInfo, float3 worldPos)
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
// MONTE CARLO PATH TRACING - Main rendering algorithm
//-----------------------------------------------------------------------
// Implements unbiased Monte Carlo path tracing with importance sampling
// 
// Lighting Strategy:
// - Direct lighting: Next Event Estimation (NEE) with explicit light sampling
// - Indirect lighting: BSDF importance sampling
// - For delta lights (directional/sun): No MIS needed, NEE weight = 1.0
// - For area lights: Would benefit from MIS combining both strategies (not implemented)
//
// Returns the radiance (color) along the given ray path
float3 pathTrace(RayDesc ray, inout uint seed)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  HitPayload    payload;

  // Path tracing state variables
  float3 radiance   = float3(0.0F, 0.0F, 0.0F);  // Accumulated radiance (final color)
  float3 throughput = float3(1.0F, 1.0F, 1.0F);  // Path throughput (energy transmission)

  // Main path tracing loop - bounce rays through the scene
  for(int depth = 0; depth < pushConst.maxDepth; depth++)
  {
    // Cast ray into scene and get intersection information
    traceRay(ray, payload);

    // Environment hit - ray escaped the scene without hitting geometry
    if(payload.hitT == INFINITE)
    {
      float3 envColor;
      if(sceneInfo.useSky == 1)
      {
        // Sample procedural sky system for realistic environment lighting
        envColor = evalSimpleSky(sceneInfo.skySimpleParam, ray.Direction);
      }
      else
      {
        // Use simple solid background color
        envColor = sceneInfo.backgroundColor;
      }

      // Add environment contribution weighted by path throughput and terminate
      return radiance + (envColor * throughput);
    }

    // Retrieve scene data for the hit surface
    GltfInstance          instance = sceneInfo.instances[payload.instanceIndex];   // Instance data
    GltfMesh              mesh     = sceneInfo.meshes[instance.meshIndex];         // Mesh geometry
    GltfMetallicRoughness material = sceneInfo.materials[instance.materialIndex];  // Material properties

    // Process light source (handles different light types and sky override)
    GltfPunctual light = processLight(sceneInfo, payload.hit.pos);

    // Set up lighting vectors for BSDF evaluation
    float  pdf = 0.0F;                        // Probability density function (for importance sampling)
    float3 V   = -ray.Direction;              // View direction (towards camera)
    float3 L   = normalize(light.direction);  // Light direction

    // Extract PBR material properties
    float4 albedo   = material.baseColorFactor;                // Base color (albedo)
    float  metallic = material.metallicFactor;                 // Metallic factor (0=dielectric, 1=metal)
    float roughness = max(0.0001f, material.roughnessFactor);  // Surface roughness (clamped to avoid divisions by zero)

    // Apply material overrides from push constants (for debugging/experimentation)
    if(pushConst.metallicRoughnessOverride.x >= 0.0)
      metallic = pushConst.metallicRoughnessOverride.x;  // Override metallic value
    if(pushConst.metallicRoughnessOverride.y >= 0.0)
      roughness = pushConst.metallicRoughnessOverride.y;  // Override roughness value

    // Initialize PBR material structure for BSDF evaluation
    PbrBaseMaterial pbrMat  = initPbrBaseMaterial(albedo.xyz, metallic, roughness, payload.hit.nrm, payload.hit.geonrm);
    float3          contrib = float3(0, 0, 0);  // Direct lighting contribution

    // NEXT EVENT ESTIMATION - Direct lighting evaluation
    // Check if light direction is above the surface (no self-illumination)
    bool nextEventValid = (dot(L, payload.hit.nrm) > 0.0f);
    if(nextEventValid)
    {
      // Evaluate BSDF for direct lighting
      BsdfEvaluateData evalData;
      evalData.k1 = -ray.Direction;                                    // Incoming direction (from camera)
      evalData.k2 = normalize(sceneInfo.skySimpleParam.sunDirection);  // Outgoing direction (to light)
      evalData.xi = float3(rand(seed), rand(seed), rand(seed));        // Random numbers for sampling

      // Evaluate PBR BSDF (both diffuse and specular components)
      bsdfEvaluateSimple(evalData, pbrMat);

      // Accumulate direct lighting contribution
      // NOTE: Light sampling strategy and MIS
      // This implementation uses a simplified approach:
      // - NEE (Next Event Estimation) for direct lighting with weight = 1.0
      // - BSDF sampling for indirect lighting only
      // 
      // For DELTA light sources (directional lights/sun), w=1.0 is mathematically correct
      // because BSDF sampling has zero probability of hitting a delta light, so only NEE contributes.
      //
      // For AREA LIGHTS or ENVIRONMENT MAPS, proper MIS would combine both light and BSDF sampling.
      // The evalData.pdf output (currently unused) would be needed for this calculation.
      const float3 w = 1.0f;  // MIS weight (correct for delta lights, simplified for area lights)
      contrib += w * evalData.bsdf_diffuse;  // Diffuse reflection
      contrib += w * evalData.bsdf_glossy;   // Specular reflection
      contrib *= throughput;                 // Weight by path throughput
    }

    // BSDF SAMPLING - Generate next ray direction for indirect lighting
    {
      BsdfSampleData sampleData;
      sampleData.k1 = -ray.Direction;                              // Incoming direction
      sampleData.xi = float3(rand(seed), rand(seed), rand(seed));  // Random numbers for sampling

      // Sample BSDF to get next ray direction and evaluate BSDF/PDF ratio
      bsdfSampleSimple(sampleData, pbrMat);

      // Check if ray was absorbed (path termination)
      if(sampleData.event_type == BSDF_EVENT_ABSORB)
      {
        break;  // Terminate path - no more bounces
      }

      // Update path throughput with BSDF/PDF ratio (Monte Carlo estimator)
      throughput *= sampleData.bsdf_over_pdf;

      // Set up next ray with slight offset to avoid self-intersection
      ray.Origin    = offsetRay(payload.hit.pos, payload.hit.nrm);
      ray.Direction = sampleData.k2;  // New ray direction from BSDF sampling
    }

    // RUSSIAN ROULETTE - Probabilistic path termination for efficiency
    // Terminate paths with low contribution probability to reduce computation
    float rrPcont = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001F, 0.95F);
    if(rand(seed) >= rrPcont)
      break;                // Terminate paths with low throughput (won't contribute much)
    throughput /= rrPcont;  // Boost energy of surviving paths to maintain unbiased estimation

    // SHADOW TESTING - Add direct lighting only if not occluded
    if(nextEventValid)
    {
      // Create shadow ray from surface point towards light
      RayDesc shadowRay;
      shadowRay.Origin    = offsetRay(payload.hit.shadowPos, payload.hit.nrm);  // Start from offset surface point
      shadowRay.Direction = L;                                          // Direction towards light
      shadowRay.TMin      = 0.01;                                       // Avoid self-intersection
      shadowRay.TMax = light.type == GltfLightType::eDirectional ? INFINITE : length(light.direction);  // Trace to infinity (or light distance)

      // Test if light is visible from this point
      bool inShadow = traceShadow(shadowRay);
      if(!inShadow)
      {
        // Light is visible - add direct lighting contribution
        radiance += contrib * light.color * light.intensity;
      }
      // If in shadow, skip direct lighting (contrib is discarded)
    }
  }

  // Return accumulated radiance from all path bounces
  return radiance;
}


//-----------------------------------------------------------------------
// PIXEL SAMPLING - Anti-aliasing and camera ray generation
//-----------------------------------------------------------------------
// Sample a single pixel with subpixel jittering for anti-aliasing
float3 samplePixel(inout uint seed, float2 launchID, float2 launchSize)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // ANTI-ALIASING: Subpixel jittering for temporal supersampling
  // First frame uses pixel center, subsequent frames use random subpixel positions
  const float2 subpixelJitter = pushConst.frame == 0 ? float2(0.5f, 0.5f) : float2(rand(seed), rand(seed));
  const float2 pixelCenter    = launchID + subpixelJitter;                                   // Jittered pixel position
  const float2 clipCoords     = pixelCenter / launchSize * 2.0 - 1.0;                        // Convert to NDC [-1,1]
  const float4 viewCoords     = mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);  // Transform to view space

  // Generate primary camera ray
  RayDesc ray;
  ray.Origin = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;  // Camera position in world space
  ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;  // Ray direction in world space
  ray.TMin = 0.001;     // Minimum distance to avoid numerical issues
  ray.TMax = INFINITE;  // Maximum distance (infinite for primary rays)

  // Trace the primary ray through the scene
  float3 radiance = pathTrace(ray, seed);

  // FIREFLY CLAMPING - Remove bright noise artifacts
  // Calculate luminance using standard RGB-to-luminance conversion
  float       lum                   = dot(radiance, float3(0.212671F, 0.715160F, 0.072169F));
  const float fireflyClampThreshold = 10.0f;
  if(lum > fireflyClampThreshold)
  {
    // Clamp overly bright pixels while preserving color ratios
    radiance *= fireflyClampThreshold / lum;
  }

  return radiance;
}


//-----------------------------------------------------------------------
// COMPUTE SHADER ENTRY POINT - GPU thread dispatch and accumulation
//-----------------------------------------------------------------------
// Each thread processes one pixel using Monte Carlo path tracing
// Results are accumulated over multiple frames for progressive refinement
[shader("compute")]
[numthreads(WORKGROUP_SIZE, WORKGROUP_SIZE, 1)]  // GPU workgroup size (typically 16x16)
void main(uint3 threadIdx: SV_DispatchThreadID)
{
  // Get current pixel coordinates from thread dispatch ID
  float2 launchID = (float2)threadIdx.xy;

  // Get output image dimensions
  uint2 imgSize;
  outImage.GetDimensions(imgSize.x, imgSize.y);  // Query texture dimensions
  float2 launchSize = imgSize;

  // Early exit for threads outside image bounds (GPU dispatch alignment)
  if(launchID.x >= launchSize.x || launchID.y >= launchSize.y)
    return;

  // Initialize high-quality random number generator
  // Uses xxhash32 with pixel coordinates and frame number for good distribution
  uint seed = xxhash32(uint3(uint2(launchID.xy), pushConst.frame));

  // Sample the pixel using Monte Carlo path tracing
  float3 pixel_color = float3(0.0F, 0.0F, 0.0F);
  pixel_color += samplePixel(seed, launchID, launchSize);  // Single sample per frame

  bool first_frame = (pushConst.frame == 0);

  // TEMPORAL ACCUMULATION - Progressive refinement over multiple frames
  if(first_frame)
  {
    // First frame: Initialize with current sample
    outImage[int2(launchID)] = float4(pixel_color, 1.0);
  }
  else
  {
    // Subsequent frames: Blend with accumulated result
    // Uses exponential moving average for stable convergence
    float  a                 = 1.0F / float(pushConst.frame + 1);              // Blend factor
    float3 old_color         = outImage[int2(launchID)].xyz;                   // Previous accumulated result
    outImage[int2(launchID)] = float4(lerp(old_color, pixel_color, a), 1.0F);  // Blend and store
  }
}
