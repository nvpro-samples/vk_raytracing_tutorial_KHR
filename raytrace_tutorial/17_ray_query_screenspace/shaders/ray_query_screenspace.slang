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

// Screen-Space Ray Query Effects using Vulkan Ray Query Extension
//
// This shader demonstrates practical ray query usage in compute shaders for
// screen-space effects like ambient occlusion. Unlike tutorial 16 which shows
// complex Monte Carlo path tracing, this tutorial focuses on simple, practical
// ray query applications that can be integrated into existing rasterization
// pipelines.
//
// Key Features:
// - Compute shader ray queries for screen-space ambient occlusion
// - Integration with rasterization pipeline using G-buffer
// - Performance optimization by processing only visible pixels
// - Real-time screen-space effects with ray tracing
//
// Performance Benefits:
// - Avoids fragment shader ray query performance issues
// - Processes only visible pixels (no wasted computation)
// - Single-ray effects are much faster than full path tracing
// - Easy integration with existing rasterization pipelines


#include "nvshaders/functions.h.slang"
#include "nvshaders/random.h.slang"
#include "nvshaders/ray_utils.h.slang"
#include "nvshaders/normal_compress.h.slang"

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
// G-buffer data for screen-space effects (read-only storage image)
 [[vk::binding(BindingPoints::eHitBuffer, 1)]]   RWTexture2D<float4> hitBuffer;
// clang-format on

// Screen-space effects parameters are now in push constants

//-----------------------------------------------------------------------
// RAY TRACING - Inline ray query implementation for screen-space effects
//-----------------------------------------------------------------------
// Shoot a single ray and return whether it hits anything
// Uses Vulkan ray query extension for efficient shadow/occlusion testing
bool traceRayQuery(RayDesc ray)
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

  // Return true if any intersection was found (surface is occluded)
  return (q.CommittedStatus() != COMMITTED_NOTHING);
}

// Shoot a ray and return the hit distance (for better AO quality)
// Returns -1.0 if no hit, otherwise returns the distance to the hit
float traceRayQueryDistance(RayDesc ray)
{
  // Use ray query to get hit distance
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
  q.TraceRayInline(topLevelAS, RAY_FLAG_NONE, 0xFF, ray);

  // Process intersections
  while(q.Proceed())
  {
    // For non-opaque triangles, force them to be opaque (simplified alpha handling)
    if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
      q.CommitNonOpaqueTriangleHit();  // Accept intersection as solid
  }

  // Return hit distance if intersection found, otherwise -1.0
  if(q.CommittedStatus() != COMMITTED_NOTHING)
  {
    return q.CommittedRayT();
  }
  return -1.0f;
}


//-----------------------------------------------------------------------
// AMBIENT OCCLUSION - Screen-space ray query implementation
//-----------------------------------------------------------------------
// Calculate ambient occlusion using ray queries in a hemisphere around the surface
// This is much more accurate than traditional screen-space AO methods
float calculateRayQueryAO(float3 worldPos, float3 worldNormal, uint seed)
{
  if(pushConst.enableRayQueryAO == 0)
    return 1.0f;  // No AO if disabled

  float ao = 0.0f;

  // Sample hemisphere around the surface normal
  for(uint i = 0; i < pushConst.aoSamples; i++)
  {
    // Generate random direction in hemisphere
    float2 random2D = float2(rand(seed), rand(seed));

    float3 tangent, bitangent;
    orthonormalBasis(worldNormal, tangent, bitangent);

    float3 sampleDir = cosineSampleHemisphere(random2D.x, random2D.y);
    sampleDir        = normalize(tangent * sampleDir.x + bitangent * sampleDir.y + worldNormal * sampleDir.z);

    // Create ray from surface point
    RayDesc aoRay;
    aoRay.Origin    = offsetRay(worldPos, worldNormal);  // Offset to avoid self-intersection
    aoRay.Direction = sampleDir;
    aoRay.TMin      = 0.0f;
    aoRay.TMax      = pushConst.aoRadius;  // Limit AO radius for performance

    // Trace ray and accumulate occlusion
    float hitDistance = traceRayQueryDistance(aoRay);
    if(hitDistance > 0)
    {
      // Ray hit something - this direction is occluded
      // Weight by distance for more realistic falloff
      float weight = 1.0f - (hitDistance / pushConst.aoRadius);
      ao += weight;
    }
  }

  // Normalize and apply intensity
  ao /= float(pushConst.aoSamples);
  ao = 1.0f - ao;
  return saturate(pow(ao, pushConst.aoIntensity));
}

//-----------------------------------------------------------------------
// G-BUFFER PROCESSING - Extract world position and normal from G-buffer
//-----------------------------------------------------------------------
// This function extracts world-space position and normal from the G-buffer
// In a real implementation, this would read from actual G-buffer textures
// For this tutorial, we'll use the hit buffer data directly
struct GBufferData
{
  float3 worldPos;
  float3 worldNormal;
  bool   isValid;
};

GBufferData extractGBufferData(int2 pixelCoord)
{
  GBufferData gbuffer;
  gbuffer.isValid = false;

  // Read the hit data from the G-buffer
  float4 data = hitBuffer.Load(pixelCoord);

  // Check if there was a valid hit (w component indicates validity)
  if(data.w == 0)
    return gbuffer;

  // Extract the hit position in world space from xyz components
  gbuffer.worldPos = data.xyz;

  // Decompress the normal from the w component
  gbuffer.worldNormal = decompressUnitVec(asuint(data.w));

  gbuffer.isValid = true;
  return gbuffer;
}

//-----------------------------------------------------------------------
// SCREEN-SPACE EFFECTS - Main processing function
//-----------------------------------------------------------------------
// Process screen-space effects using ray queries
// This is the core function that demonstrates practical ray query usage
float3 processScreenSpaceEffects(float2 screenUV, float2 launchID, float2 screenSize, uint seed)
{
  // Extract G-buffer data
  GBufferData gbuffer = extractGBufferData(int2(launchID));

  if(!gbuffer.isValid)
    return outImage[int2(launchID)].xyz;  // Return original color for invalid pixels

  // Calculate ambient occlusion using ray queries
  float ao = calculateRayQueryAO(gbuffer.worldPos, gbuffer.worldNormal, seed);

  // Return AO result
  if(pushConst.showAOOnly == 1)
  {
    return float3(ao, ao, ao);  // Show AO as grayscale for debugging
  }
  else
  {
    float3 baseColor = outImage[int2(launchID)].xyz;  // Get base color from input image
    return baseColor * ao;  // Apply AO to base color
  }
}

//-----------------------------------------------------------------------
// COMPUTE SHADER ENTRY POINT - Screen-space effects processing
//-----------------------------------------------------------------------
// Each thread processes one pixel for screen-space effects
// This demonstrates the performance benefits of compute shader ray queries
[shader("compute")]
[numthreads(WORKGROUP_SIZE, WORKGROUP_SIZE, 1)]  // GPU workgroup size (typically 16x16)
void main(uint3 threadIdx: SV_DispatchThreadID)
{
  // Get current pixel coordinates from thread dispatch ID
  float2 launchID = (float2)threadIdx.xy;

  // Get output image dimensions
  uint2 imgSize;
  outImage.GetDimensions(imgSize.x, imgSize.y);
  float2 launchSize = imgSize;

  // Early exit for threads outside image bounds (GPU dispatch alignment)
  if(launchID.x >= launchSize.x || launchID.y >= launchSize.y)
    return;

  // Convert pixel coordinates to screen UV coordinates
  float2 screenUV = launchID / launchSize;

  // Initialize high-quality random number generator
  // Uses xxhash32 with pixel coordinates and frame number for good distribution
  uint seed = xxhash32(uint3(uint2(launchID.xy), pushConst.frame));

  // Process screen-space effects using parameters from push constants
  float3 result = processScreenSpaceEffects(screenUV, launchID, launchSize, seed);

  // Write result to output image
  outImage[int2(launchID)] = float4(result, 1.0f);
}
