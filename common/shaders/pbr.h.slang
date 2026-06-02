/*
 * Copyright (c) 2019-2026, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PBR_GLSL
#define PBR_GLSL

// Simplified PBR (metallic-roughness) for tutorial use.
// The full-featured BSDF with clearcoat, sheen, transmission, iridescence, etc.
// lives in nvpro_core2/nvshaders/bsdf_functions.h.slang.

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_1_PI
#define M_1_PI 0.31830988618379067154
#endif

#ifndef M_TWO_PI
#define M_TWO_PI 6.28318530717958647692
#endif

// Special PDF value for delta lights (directional sun) in path tracing MIS
#ifndef DIRAC
#define DIRAC (-1.0f)
#endif

#define MICROFACET_MIN_ROUGHNESS 0.0014142f


//-----------------------------------------------------------------------------
// Clamped dot product
//-----------------------------------------------------------------------------
inline float clampedDot(float3 x, float3 y)
{
  return clamp(dot(x, y), 0.0F, 1.0F);
}

//-----------------------------------------------------------------------------
// Builds an orthonormal basis: given only a normal vector, returns a
// tangent and bitangent.
//
// This uses the technique from "Improved accuracy when building an orthonormal
// basis" by Nelson Max, https://jcgt.org/published/0006/01/02.
// Any tangent-generating algorithm must produce at least one discontinuity
// when operating on a sphere (due to the hairy ball theorem); this has a
// small ring-shaped discontinuity at normal.z == -0.99998796.
//-----------------------------------------------------------------------------
void orthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
  if(normal.z < -0.99998796F)  // Handle the singularity
  {
    tangent   = float3(0.0F, -1.0F, 0.0F);
    bitangent = float3(-1.0F, 0.0F, 0.0F);
    return;
  }
  float a   = 1.0F / (1.0F + normal.z);
  float b   = -normal.x * normal.y * a;
  tangent   = float3(1.0F - normal.x * normal.x * a, b, -normal.x);
  bitangent = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
}

//-----------------------------------------------------------------------
// https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.4.pdf
// 16.6.1 COSINE-WEIGHTED HEMISPHERE ORIENTED TO THE Z-AXIS
//-----------------------------------------------------------------------
inline float3 cosineSampleHemisphere(float r1, float r2)
{
  float  r   = sqrt(r1);
  float  phi = M_TWO_PI * r2;
  float3 dir;
  dir.x = r * cos(phi);
  dir.y = r * sin(phi);
  dir.z = sqrt(max(0.F, 1.F - dir.x * dir.x - dir.y * dir.y));
  return dir;
}


// Individual flags:
#define BSDF_EVENT_ABSORB 0          // 0: invalid sample; path should be discarded (radiance 0)
#define BSDF_EVENT_DIFFUSE 1         // 1: e.g. Lambert. Lobe is always centered on the surface normal.
#define BSDF_EVENT_GLOSSY (1 << 1)   // 2; Center of lobe depends on viewing angle; not perfectly specular reflection.
#define BSDF_EVENT_IMPULSE (1 << 2)  // 4; "Perfectly specular" or "mirror-like" reflection or transmission.
#define BSDF_EVENT_REFLECTION                                                                                          \
  (1 << 3)  // 8; Both view and light directions are on the same side of the geometric normal.
#define BSDF_EVENT_TRANSMISSION                                                                                        \
  (1 << 4)  // 16; View and light directions are on opposite sides of the geometric normal.

// Combinations:
#define BSDF_EVENT_DIFFUSE_REFLECTION (BSDF_EVENT_DIFFUSE | BSDF_EVENT_REFLECTION)      // 9
#define BSDF_EVENT_DIFFUSE_TRANSMISSION (BSDF_EVENT_DIFFUSE | BSDF_EVENT_TRANSMISSION)  // 17
#define BSDF_EVENT_GLOSSY_REFLECTION (BSDF_EVENT_GLOSSY | BSDF_EVENT_REFLECTION)        // 10
#define BSDF_EVENT_GLOSSY_TRANSMISSION (BSDF_EVENT_GLOSSY | BSDF_EVENT_TRANSMISSION)    // 18
#define BSDF_EVENT_IMPULSE_REFLECTION (BSDF_EVENT_IMPULSE | BSDF_EVENT_REFLECTION)      // 12
#define BSDF_EVENT_IMPULSE_TRANSMISSION (BSDF_EVENT_IMPULSE | BSDF_EVENT_TRANSMISSION)  // 20


/*-------------------------------------------------------------------------------------------------
# struct `BsdfEvaluateData`
> Data structure for evaluating a BSDF. See the file for parameter documentation.
-------------------------------------------------------------------------------------------------*/
struct BsdfEvaluateData
{
  float3 k1;            // [in] Toward the incoming ray
  float3 k2;            // [in] Toward the sampled light
  float3 xi;            // [in] 3 random [0..1]
  float3 bsdf_diffuse;  // [out] Diffuse contribution
  float3 bsdf_glossy;   // [out] Specular contribution
  float  pdf;           // [out] PDF
};

/*-------------------------------------------------------------------------------------------------
# struct `BsdfSampleData`
> Data structure for sampling a BSDF. See the file for parameter documentation.
-------------------------------------------------------------------------------------------------*/
struct BsdfSampleData
{
  float3 k1;             // [in] Toward the incoming ray
  float3 k2;             // [out] Toward the sampled light
  float3 xi;             // [in] 3 random [0..1]
  float  pdf;            // [out] PDF
  float3 bsdf_over_pdf;  // [out] contribution / PDF
  int    event_type;     // [out] one of the event above
};


struct PbrBaseMaterial
{
  float3 baseColor = { 1.F, 1.F, 1.F };  // base color
  float  opacity   = 1.F;                // 1 = opaque, 0 = fully transparent
  float2 roughness = { 1.F, 1.F };       // 0 = smooth, 1 = rough (anisotropic: x = U, y = V)
  float  metallic  = 1.F;                // 0 = dielectric, 1 = metallic
  float3 emissive  = { 0.F, 0.F, 0.F };  // emissive color
  float  occlusion = 1.f;                // occlusion factor
  float  ior1      = 1.F;                // index of refraction : current medium (i.e. air)
  float  ior2      = 1.5F;               // index of refraction : the other side (i.e. glass)

  float3 N  = { 0.F, 0.F, 1.F };  // shading normal
  float3 Ng = { 0.F, 0.F, 1.F };  // geometric normal
  float3 T  = { 1.F, 0.F, 0.F };  // tangent
  float3 B  = { 0.F, 1.F, 0.F };  // bitangent
};


//-----------------------------------------------------------------------------
// Schlick Fresnel approximation
//-----------------------------------------------------------------------------
__generic<T : IFloat> T schlickFresnel(T f0, T f90, float VdotH)
{
  return f0 + (f90 - f0) * T(pow(1.0 - VdotH, 5.0));
}

//-----------------------------------------------------------------------------
// Returns the probability that bsdfSampleSimple samples a glossy lobe.
//-----------------------------------------------------------------------------
float bsdfSimpleGlossyProbability(float NdotV, float metallic)
{
  return lerp(schlickFresnel(0.04F, 1.0F, NdotV), 1.0F, metallic);
}

/*-------------------------------------------------------------------------------------------------
# Function `sampleGGX_VNDF `
> Samples Samples a visible (Smith-masked) half vector according to the anisotropic GGX distribution.

See Eric Heitz, "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals".

The input and output will be in local space:
`float3(dot(T, k1), dot(B, k1), dot(N, k1))`.
-------------------------------------------------------------------------------------------------*/
float3 sampleGGX_VNDF(float3 k, float2 roughness, float2 xi)
{
  const float3 v = normalize(float3(k.x * roughness.x, k.y * roughness.y, k.z));

  const float3 t1 = (v.z < 0.99999f) ? normalize(cross(v, float3(0.0f, 0.0f, 1.0f))) : float3(1.0f, 0.0f, 0.0f);
  const float3 t2 = cross(t1, v);

  const float a = 1.0f / (1.0f + v.z);
  const float r = sqrt(xi.x);

  const float phi = (xi.y < a) ? xi.y / a * M_PI : M_PI + (xi.y - a) / (1.0f - a) * M_PI;
  float       sp  = sin(phi);
  float       cp  = cos(phi);
  const float p1  = r * cp;
  const float p2  = r * sp * ((xi.y < a) ? 1.0f : v.z);

  float3 h = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * v;

  h.x *= roughness.x;
  h.y *= roughness.y;
  h.z = max(0.0f, h.z);
  return normalize(h);
}

// GGX/Trowbridge-Reitz NDF (non-projected hemisphere).
float D_GGX(float NdotH, float alphaRoughness)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float f                = (NdotH * NdotH) * (alphaRoughnessSq - 1.0F) + 1.0F;
  return alphaRoughnessSq / (M_PI * f * f);
}

// Smith-GGX height-correlated visibility term `Vis = G / (4 NdotL NdotV)` (Heitz 2014).
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float GGXV             = NdotL * sqrt(NdotV * NdotV * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);
  float GGXL             = NdotV * sqrt(NdotL * NdotL * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);
  float GGX              = GGXV + GGXL;
  return (GGX > 0.0F) ? (0.5F / GGX) : 0.0F;
}

// Single-directional Smith masking term (needed for the VNDF sampling PDF).
float G1_GGX(float NdotV, float alphaRoughness)
{
  float a2 = alphaRoughness * alphaRoughness;
  return 2.0F * NdotV / (NdotV + sqrt(NdotV * NdotV * (1.0F - a2) + a2));
}

/*-------------------------------------------------------------------------------------------------
# Function `bsdfEvaluateSimple`
> Evaluates the Khronos metallic-roughness BRDF for a given pair of directions.

Uses height-correlated Smith-GGX (D_GGX * V_GGX) matching the glTF Sample Renderer.
Returns cosine-weighted values (NdotL baked in).

You must provide `data.k1` and `data.k2`, but do not need to provide `data.xi`.
-------------------------------------------------------------------------------------------------*/
void bsdfEvaluateSimple(inout BsdfEvaluateData data, PbrBaseMaterial mat)
{
  float3 H     = normalize(data.k1 + data.k2);
  float  NdotV = clampedDot(mat.N, data.k1);
  float  NdotL = clampedDot(mat.N, data.k2);
  float  VdotH = clampedDot(data.k1, H);
  float  NdotH = clampedDot(mat.N, H);

  if(NdotV == 0.0f || NdotL == 0.0f)
  {
    data.bsdf_diffuse = data.bsdf_glossy = float3(0.0f);
    data.pdf                             = 0.0f;
    return;
  }

  float alphaRoughness = mat.roughness.x;

  // Fresnel: combined metal + dielectric into a single glossy lobe.
  // f0 interpolates between dielectric reflectance (0.04) and baseColor (metal).
  float  c_min_reflectance = 0.04F;
  float3 f0                = lerp(float3(c_min_reflectance), mat.baseColor, mat.metallic);
  float3 F                 = schlickFresnel(f0, float3(1.0F), VdotH);

  // Khronos GGX specular: D * V (height-correlated Smith, Heitz 2014)
  float D   = D_GGX(NdotH, alphaRoughness);
  float Vis = V_GGX(NdotL, NdotV, alphaRoughness);

  // Cosine-weighted BSDF outputs
  data.bsdf_diffuse = (float3(1.0F) - F) * (1.0F - mat.metallic) * mat.baseColor * M_1_PI * NdotL;
  data.bsdf_glossy  = F * (Vis * NdotL) * D;

  // PDF: mixture of cosine-sampled diffuse and VNDF-sampled specular
  float diffusePdf  = M_1_PI * NdotL;
  float specularPdf = D * G1_GGX(NdotV, alphaRoughness) / (4.0f * NdotV);
  data.pdf          = lerp(diffusePdf, specularPdf, bsdfSimpleGlossyProbability(NdotV, mat.metallic));
}

/*-------------------------------------------------------------------------------------------------
# Function `bsdfSampleSimple`
> Evaluates the simple BSDF model using the given material and input and output directions.

You must provide `data.k1` and `data.xi`. For one sample of pure reflection
(e.g. vk_mini_samples/ray_trace), use `data.xi == float2(0,0)`.

After calling this function, you should check if `data.event_type` is
`BSDF_EVENT_ABSORB`. If so, the sampling code sampled a direction below the
surface, and the light path ends here (it should be treated as a reflectance of 0).

This code cannot currently return a PDF of `DIRAC`, but that might change in
the future.
-------------------------------------------------------------------------------------------------*/
void bsdfSampleSimple(inout BsdfSampleData data, PbrBaseMaterial mat)
{
  float3 tint        = mat.baseColor;
  data.bsdf_over_pdf = float3(0.0F);

  float nk1 = clampedDot(mat.N, data.k1);
  if(data.xi.z <= bsdfSimpleGlossyProbability(nk1, mat.metallic))
  {
    // Glossy GGX
    data.event_type = BSDF_EVENT_GLOSSY_REFLECTION;
    // Transform to local space
    float3 localK1    = float3(dot(mat.T, data.k1), dot(mat.B, data.k1), nk1);
    float3 halfVector = sampleGGX_VNDF(localK1, mat.roughness, data.xi.xy);
    // Transform from local space
    halfVector = mat.T * halfVector.x + mat.B * halfVector.y + mat.N * halfVector.z;
    data.k2    = reflect(-data.k1, halfVector);
  }
  else
  {
    // Diffuse
    data.event_type = BSDF_EVENT_DIFFUSE_REFLECTION;
    float3 localDir = cosineSampleHemisphere(data.xi.x, data.xi.y);
    data.k2         = mat.T * localDir.x + mat.B * localDir.y + mat.N * localDir.z;
  }

  BsdfEvaluateData evalData;
  evalData.k1 = data.k1;
  evalData.k2 = data.k2;
  bsdfEvaluateSimple(evalData, mat);
  data.pdf          = evalData.pdf;
  float3 bsdf_total = evalData.bsdf_diffuse + evalData.bsdf_glossy;
  if(data.pdf <= 0.00001F || any(isnan(bsdf_total)))
  {
    data.bsdf_over_pdf = float3(0.0f);
    data.event_type    = BSDF_EVENT_ABSORB;
  }
  else
  {
    data.bsdf_over_pdf = bsdf_total / data.pdf;
  }
}

PbrBaseMaterial initPbrBaseMaterial(float3 baseColor, float metallic, float roughness, float3 N, float3 Ng)
{
  PbrBaseMaterial mat = {};
  mat.baseColor       = baseColor;
  mat.metallic        = metallic;
  float roughness2    = max(roughness * roughness, MICROFACET_MIN_ROUGHNESS);
  mat.roughness       = float2(roughness2);
  mat.Ng              = Ng;
  mat.N               = N;
  orthonormalBasis(mat.N, mat.T, mat.B);

  return mat;
}


float3 pbrMetallicRoughness(float3 albedo, float metallic, float roughness, float3 N, float3 V, float3 L)
{
  BsdfEvaluateData data;
  data.k1 = V;
  data.k2 = L;
  data.xi = float3(0.0f);  // Not used in evaluation
  PbrBaseMaterial mat;
  mat.baseColor = albedo;
  mat.metallic  = metallic;
  float roughness2    = max(roughness * roughness, MICROFACET_MIN_ROUGHNESS);
  mat.roughness       = float2(roughness2);
  mat.N         = N;
  orthonormalBasis(mat.N, mat.T, mat.B);
  bsdfEvaluateSimple(data, mat);

  return data.bsdf_diffuse + data.bsdf_glossy;
}


#endif  // PBR_GLSL