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
// - Simplified metallic-roughness BSDF (common/shaders/pbr.h.slang)
// - Next Event Estimation (NEE) with Multiple Importance Sampling (MIS)
// - Multiple light types (directional, point, spot) with proper attenuation
// - Procedural sky system with sun/atmosphere simulation
// - Russian roulette path termination for unbiased rendering
// - Temporal accumulation for progressive refinement
// - Firefly clamping to reduce noise artifacts
//
// Path Tracing Loop (same structure as vk_gltf_renderer/gltf_pathtrace.slang):
//   1. Trace ray -> hit surface or environment
//   2. NEE: evaluate BSDF toward light, accumulate contribution (shadow test later)
//   3. BSDF sample: pick next bounce direction, update throughput
//   4. Shadow ray: add direct light only if unoccluded
//   5. Russian roulette: probabilistically terminate long paths
//
// Performance Optimizations:
// - Inline ray queries avoid shader binding table overhead
// - Early path termination using Russian roulette (after RR_MIN_DEPTH bounces)
// - Shadow rays use RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
// - Efficient random number generation using xxhash
//
// Mathematical Foundation:
// This implementation follows the rendering equation:
// L_o(p,ω_o) = L_e(p,ω_o) + ∫ f_r(p,ω_i,ω_o) L_i(p,ω_i) (n·ω_i) dω_i
//
// Where Monte Carlo integration is used to approximate the integral over
// the hemisphere of incoming light directions.

// Tutorial headers (scene, geometry, lights, simplified PBR)
#include "common/shaders/pbr.h.slang"          // bsdfEvaluateSimple, bsdfSampleSimple, DIRAC
#include "common/shaders/ray_common.h.slang"   // generatePrimaryRay (pulls INFINITE via nvshaders/constants)
#include "common/shaders/gltf_accessor.h.slang"
#include "common/shaders/light.h.slang"        // processLight() for punctual lights / sky sun

// nvpro_core2 utilities kept minimal on purpose
#include "nvshaders/random.h.slang"            // rand(), xxhash32
#include "nvshaders/sky_functions.h.slang"   // evalSimpleSky


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


// Maximum luminance per path sample; higher values reduce fireflies but may bias bright highlights
#define FIREFLY_CLAMP_THRESHOLD 10.0f

//-----------------------------------------------------------------------
// PATH TRACING TUNING CONSTANTS
//-----------------------------------------------------------------------
// Russian roulette is only applied after RR_MIN_DEPTH surface bounces so that
// short paths (direct view + one bounce) are never randomly killed.
static const int   RR_MIN_DEPTH   = 3;      // Minimum bounces before Russian roulette
static const float RR_PCONT_FLOOR = 0.001f; // Added to max(throughput) for continuation probability
static const float RR_PCONT_CAP   = 0.95f;  // Cap on continuation probability

//-----------------------------------------------------------------------
// DIRECT LIGHT SAMPLE - Input to Next Event Estimation (NEE)
//-----------------------------------------------------------------------
// Bundles everything needed to connect a surface hit to a punctual light source.
// This mirrors vk_gltf_renderer/pathtrace_functions.h.slang::DirectLight.
//
// - direction:        unit vector from the shading point toward the light (ω_i)
// - radianceOverPdf:  light radiance L_i already divided by the light-sampling PDF
//                     (for a directional/sun light with PDF = DIRAC, this is simply color * intensity)
// - distance:         ray parameter to the light (INFINITE for directional lights)
// - pdf:              light sampling PDF (DIRAC = -1 for delta lights like the sun)
struct DirectLight
{
  float3 direction;        // Direction from surface point to the light
  float3 radianceOverPdf;  // Radiance over PDF (Li / pdf_light)
  float  distance;         // Distance to the light (INFINITE for directional)
  float  pdf;              // Probability of sampling this light direction
};

//-----------------------------------------------------------------------
// MATERIAL SETUP - Build PbrBaseMaterial from tutorial glTF factors
//-----------------------------------------------------------------------
// Uses the simplified BSDF in pbr.h.slang (metallic-roughness, no textures).
// vk_gltf_renderer uses the full glTF BSDF with texture fetches instead.
PbrBaseMaterial pbrMaterialFromGltf(GltfMetallicRoughness material, HitState hit, float metallicOverride, float roughnessOverride)
{
  float metallic  = material.metallicFactor;
  float roughness = max(0.0001f, material.roughnessFactor);  // Clamped to avoid divisions by zero
  // Apply material overrides from push constants (for debugging/experimentation)
  if(metallicOverride >= 0.0)
    metallic = metallicOverride;
  if(roughnessOverride >= 0.0)
    roughness = roughnessOverride;
  return initPbrBaseMaterial(material.baseColorFactor.xyz, metallic, roughness, hit.nrm, hit.geonrm);
}

//-----------------------------------------------------------------------
// LIGHT SAMPLING - Prepare direct light for NEE at a shading point
//-----------------------------------------------------------------------
// Uses common/shaders/light.h.slang::processLight() for sky override and
// distance/cone attenuation, then packs the result into DirectLight.
void sampleDirectLight(GltfSceneInfo* sceneInfo, float3 worldPos, out DirectLight directLight)
{
  // processLight takes GltfSceneInfo by value; sceneInfo is a device pointer
  GltfPunctual light = processLight(sceneInfo[0], worldPos);

  directLight.direction = normalize(light.direction);
  directLight.distance  = light.type == GltfLightType::eDirectional ? INFINITE : length(light.direction);
  // Tutorial uses a single known light (not stochastic light selection), so treat as delta
  directLight.pdf             = DIRAC;
  directLight.radianceOverPdf = light.color * light.intensity;
}

//-----------------------------------------------------------------------
// ENVIRONMENT MIS - Balance BSDF hits against environment map / sky hits
//-----------------------------------------------------------------------
// When a path eventually escapes to the sky, it may have been sampled twice:
// once via NEE toward the sun and once via a BSDF bounce that lands on the environment.
// This weight reduces double counting (see vk_gltf_renderer computeEnvHitMisWeight).
float computeEnvHitMisWeight(float lastSamplePdf, float envPdf)
{
  if(lastSamplePdf == DIRAC)
    return 1.0f;
  return lastSamplePdf / (lastSamplePdf + envPdf);
}

// Hit state information - processed intersection data ready for shading
struct HitState
{
  float3 pos       = float3(0);  // World-space hit position
  float3 nrm       = float3(0);  // Shading normal (interpolated and transformed)
  float3 geonrm    = float3(0);  // Geometric normal (face normal, for back-face detection)
  float3 shadowPos = float3(0);  // Shadow position
};

// Ray payload structure - carries intersection data through ray queries
// Unlike traditional RT pipelines, ray queries return this data directly
struct HitPayload
{
  float    hitT          = INFINITE;  // Distance to intersection (INFINITE if no hit)
  int      instanceIndex = -1;        // Index of hit instance in the scene
  HitState hit           = {};        // Hit state information
};


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
  float3 posObj = interpolateBarycentric(pos0, pos1, pos2, barycentrics);
  hit.pos       = float3(mul(float4(posObj, 1.0), objectToWorld4x3));


  // Interpolate vertex normals and transform to world space
  // Note: normals are transformed using the inverse-transpose matrix
  float3 nrm0   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.x);
  float3 nrm1   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.y);
  float3 nrm2   = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, indices.z);
  float3 nrmObj = normalize(interpolateBarycentric(nrm0, nrm1, nrm2, barycentrics));
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
    payload.hitT          = hitT;           // Distance to hit point
    payload.hit           = hitState;       // Hit state information
    payload.instanceIndex = instanceIndex;  // Instance index
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
// MONTE CARLO PATH TRACING - Main rendering algorithm
//-----------------------------------------------------------------------
// Implements unbiased Monte Carlo path tracing with importance sampling.
//
// Lighting strategy (same bounce order as vk_gltf_renderer/gltf_pathtrace.slang):
// - Direct lighting:  Next Event Estimation (NEE) — explicitly sample the light
// - Indirect lighting: BSDF importance sampling — sample a new bounce direction
// - MIS combines light and BSDF strategies when both can hit the same direction
// - For delta lights (directional/sun): MIS weight = 1.0 (BSDF cannot randomly hit the sun)
//
// Per-bounce order (important for correctness):
//   NEE eval -> BSDF sample -> shadow test -> Russian roulette
//
// Returns accumulated radiance (color) along the camera ray path.
float3 pathTrace(RayDesc ray, inout uint seed)
{
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  HitPayload     payload;

  // Path tracing state (see vk_gltf_renderer PathTracerState)
  float3 radiance      = float3(0.0F, 0.0F, 0.0F);  // Accumulated radiance (final pixel color)
  float3 throughput    = float3(1.0F, 1.0F, 1.0F);  // Path throughput (energy along the path)
  float  lastSamplePdf = DIRAC;                     // PDF of last BSDF sample (for environment MIS)
  int    surfaceDepth  = 0;                       // Number of surface bounces so far

  // Main path tracing loop — each iteration is one surface interaction
  while(surfaceDepth < pushConst.maxDepth)
  {
    //-----------------------------------------------------------------------
    // STEP 1: Trace ray into the scene
    //-----------------------------------------------------------------------
    traceRay(ray, payload);

    //-----------------------------------------------------------------------
    // STEP 2: Environment hit — ray escaped without hitting geometry
    //-----------------------------------------------------------------------
    if(payload.hitT == INFINITE)
    {
      float3 envColor;
      if(sceneInfo.useSky == 1)
      {
        // Sample procedural sky (sun + gradient atmosphere)
        envColor = evalSimpleSky(sceneInfo.skySimpleParam, ray.Direction);
      }
      else
      {
        envColor = sceneInfo.backgroundColor;
      }

      // MIS: if we also reached the sky via a BSDF sample, down-weight to avoid double counting
      // (approximate hemisphere PDF — evalSimpleSky does not expose a direction PDF)
      const float envPdf    = M_1_PI * 0.5f;
      float       misWeight = computeEnvHitMisWeight(lastSamplePdf, envPdf);
      radiance += throughput * misWeight * envColor;
      break;
    }

    //-----------------------------------------------------------------------
    // STEP 3: Retrieve hit material and build PbrBaseMaterial for BSDF
    //-----------------------------------------------------------------------
    GltfInstance          instance = sceneInfo.instances[payload.instanceIndex];
    GltfMetallicRoughness material = sceneInfo.materials[instance.materialIndex];

    PbrBaseMaterial pbrMat = pbrMaterialFromGltf(material, payload.hit, pushConst.metallicRoughnessOverride.x,
                                                 pushConst.metallicRoughnessOverride.y);

    //-----------------------------------------------------------------------
    // STEP 4: NEXT EVENT ESTIMATION (NEE) — direct lighting toward the light
    //-----------------------------------------------------------------------
    // Sample the scene punctual light (or sky sun via processLight).
    DirectLight directLight;
    sampleDirectLight(sceneInfo, payload.hit.pos, directLight);

    // Light can contribute if it illuminates the front face (N·L > 0)
    float3 contribution = float3(0.0F);
    bool   nextEventValid = (dot(directLight.direction, payload.hit.nrm) > 0.0f) && directLight.pdf != 0.0f;

    if(nextEventValid)
    {
      // Evaluate simplified BSDF (pbr.h.slang) for the fixed light direction
      // k1 = view direction V (toward camera); k2 = light direction L (toward light)
      // ray.Direction points into the scene, so -ray.Direction points back toward the camera.
      BsdfEvaluateData evalData;
      evalData.k1 = -ray.Direction;
      evalData.k2 = directLight.direction;
      evalData.xi = float3(rand(seed), rand(seed), rand(seed));
      bsdfEvaluateSimple(evalData, pbrMat);

      // Monte Carlo estimator for direct lighting:
      //   L_direct += throughput * (Li / pdf_light) * MIS * f(ω_i, ω_o)
      // bsdf_diffuse and bsdf_glossy already include cos(theta) (N·L)
      if(evalData.pdf > 0.0)
      {
        const float misWeight =
            (directLight.pdf == DIRAC) ? 1.0F : directLight.pdf / (directLight.pdf + evalData.pdf);
        const float3 w = throughput * directLight.radianceOverPdf * misWeight;
        contribution += w * evalData.bsdf_diffuse;
        contribution += w * evalData.bsdf_glossy;
      }
    }

    //-----------------------------------------------------------------------
    // STEP 5: BSDF SAMPLING — pick the next bounce direction (indirect path)
    //-----------------------------------------------------------------------
    bool pathContinue = true;
    {
      BsdfSampleData sampleData;
      sampleData.k1 = -ray.Direction;                              // View direction V (toward camera)
      sampleData.xi = float3(rand(seed), rand(seed), rand(seed));  // Random numbers for lobe selection

      // Sample diffuse or glossy lobe and get bsdf/pdf (see pbr.h.slang)
      bsdfSampleSimple(sampleData, pbrMat);

      if(sampleData.event_type == BSDF_EVENT_ABSORB)
      {
        // Invalid direction (e.g. below the surface) — path ends after this bounce's NEE
        pathContinue = false;
      }
      else
      {
        // Update throughput with BSDF/PDF ratio (standard path tracing Monte Carlo weight)
        throughput *= sampleData.bsdf_over_pdf;
        ray.Direction = sampleData.k2;  // Sampled outgoing direction (away from surface, into scene)
        lastSamplePdf = sampleData.pdf; // Remember PDF for environment MIS on a later miss

        // Offset next ray origin along geometric normal (avoids self-intersection)
        float3 offsetDir = dot(ray.Direction, payload.hit.geonrm) > 0.0f ? payload.hit.geonrm : -payload.hit.geonrm;
        ray.Origin       = safeOffsetRay(payload.hit.pos, offsetDir);
      }
    }

    //-----------------------------------------------------------------------
    // STEP 6: SHADOW TESTING — add direct light only if not occluded
    //-----------------------------------------------------------------------
    // Deferred from step 4 to keep less live state per bounce (same as vk_gltf_renderer).
    // Still runs when pathContinue is false so the last bounce's NEE is not lost.
    if(nextEventValid)
    {
      // Shadow terminator fix: use shadowPos on the lit side, pos on the back side
      bool   shadowSideForward = dot(directLight.direction, payload.hit.nrm) > 0.0F;
      float3 shadowOffsetDir   = shadowSideForward ? payload.hit.geonrm : -payload.hit.geonrm;
      float3 shadowOffsetBase  = shadowSideForward ? payload.hit.shadowPos : payload.hit.pos;

      RayDesc shadowRay;
      shadowRay.Origin    = safeOffsetRay(shadowOffsetBase, shadowOffsetDir);
      shadowRay.Direction = directLight.direction;
      shadowRay.TMin      = 0.0;  // Safe with the offsetRay
      shadowRay.TMax      = directLight.distance;

      if(!traceShadow(shadowRay))
      {
        radiance += contribution;
      }
    }

    // If the path does not continue, break out of the loop
    // This means the path hit an absorbing surface
    if(!pathContinue)
      break;

    //-----------------------------------------------------------------------
    // STEP 7: RUSSIAN ROULETTE — unbiased early termination of low-throughput paths
    //-----------------------------------------------------------------------
    // Survival probability scales with throughput; surviving paths are boosted by 1/P
    // so the estimator remains unbiased. Only applied after RR_MIN_DEPTH bounces.
    if(surfaceDepth >= RR_MIN_DEPTH)
    {
      float rrPcont = min(max(throughput.x, max(throughput.y, throughput.z)) + RR_PCONT_FLOOR, RR_PCONT_CAP);
      if(rand(seed) >= rrPcont)
        break;  // Terminate — path unlikely to contribute much more
      throughput /= rrPcont;  // Boost energy of surviving paths
    }

    surfaceDepth++;
  }

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
  // First frame uses pixel center, subsequent frames use random subpixel positions.
  // Jitter is relative to the pixel center (which `generatePrimaryRay` already targets), so the offset stays in [-0.5, 0.5).
  const float2 subpixelJitter = pushConst.frame == 0 ? float2(0.0f, 0.0f) : float2(rand(seed) - 0.5f, rand(seed) - 0.5f);
  RayDesc ray = generatePrimaryRay(launchID + subpixelJitter, launchSize, sceneInfo[0]);

  // Trace the primary ray through the scene
  float3 radiance = pathTrace(ray, seed);

  // FIREFLY CLAMPING - Remove bright noise artifacts
  // Calculate luminance using standard RGB-to-luminance conversion
  float lum = dot(radiance, float3(0.212671F, 0.715160F, 0.072169F));
  if(lum > FIREFLY_CLAMP_THRESHOLD)
  {
    // Clamp overly bright pixels while preserving color ratios
    radiance *= FIREFLY_CLAMP_THRESHOLD / lum;
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
