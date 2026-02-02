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

#include "nvshaders/slang_types.h"

#include "nvshaders/bsdf_types.h.slang"
#include "nvshaders/bsdf_functions.h.slang"

#include "nvshaders/functions.h.slang"

struct PbrBaseMaterial
{
  float3 baseColor = {1.F, 1.F, 1.F};  // base color
  float  opacity   = 1.F;                    // 1 = opaque, 0 = fully transparent
  float2 roughness = { 1.F, 1.F };           // 0 = smooth, 1 = rough (anisotropic: x = U, y = V)
  float  metallic  = 1.F;                    // 0 = dielectric, 1 = metallic
  float3 emissive  = { 0.F, 0.F, 0.F };      // emissive color
  float  occlusion = 1.f;                    // occlusion factor
  float  ior1      = 1.F;                    // index of refraction : current medium (i.e. air)
  float  ior2      = 1.5F;                   // index of refraction : the other side (i.e. glass)

  float3 N  = { 0.F, 0.F, 1.F };  // shading normal
  float3 Ng = { 0.F, 0.F, 1.F };  // geometric normal
  float3 T  = { 1.F, 0.F, 0.F };  // tangent
  float3 B  = { 0.F, 1.F, 0.F };  // bitangent
};

/*-------------------------------------------------------------------------------------------------
# Function `bsdfEvaluateSimple`
> Evaluates the simple BSDF model using the given material and input and output directions.

You must provide `data.k1` and `data.k2`, but do not need to provide `data.xi`.
-------------------------------------------------------------------------------------------------*/
void bsdfEvaluateSimple(inout BsdfEvaluateData data, PbrBaseMaterial mat)
{
  // Specular reflection
  float3 H     = normalize(data.k1 + data.k2);
  float  NdotV = clampedDot(mat.N, data.k1);
  float  NdotL = clampedDot(mat.N, data.k2);
  float  VdotH = clampedDot(data.k1, H);
  float  NdotH = clampedDot(mat.N, H);

  if(NdotV == 0.0f || NdotL == 0.0f || VdotH == 0.0f || NdotH == 0.0f)
  {
    data.bsdf_diffuse = data.bsdf_glossy = float3(0.0f);
    data.pdf                             = 0.0f;
    return;
  }

  // We combine the metallic and specular lobes into a single glossy lobe.
  // The metallic weight is     metallic *    fresnel(f0 = baseColor)
  // The specular weight is (1-metallic) *    fresnel(f0 = c_min_reflectance)
  // The diffuse weight is  (1-metallic) * (1-fresnel(f0 = c_min_reflectance)) * baseColor

  // Fresnel terms
  float  c_min_reflectance = 0.04F;
  float3 f0                = lerp(float3(c_min_reflectance), mat.baseColor, mat.metallic);
  float3 fGlossy           = schlickFresnel(f0, float3(1.0F), VdotH);  // Metallic + specular
  float  fDiffuse          = schlickFresnel(1.0F - c_min_reflectance, 0.0F, VdotH) * (1.0F - mat.metallic);

  // Specular GGX
  float3 localH  = float3(dot(mat.T, H), dot(mat.B, H), NdotH);
  float  d       = hvd_ggx_eval(1.0f / mat.roughness, localH);
  float3 localK1 = float3(dot(mat.T, data.k1), dot(mat.B, data.k1), NdotV);
  float3 localK2 = float3(dot(mat.T, data.k2), dot(mat.B, data.k2), NdotL);
  float  G1 = 0.0f, G2 = 0.0f;
  ggx_smith_shadow_mask(G1, G2, localK1, localK2, mat.roughness);

  float diffusePdf  = M_1_PI * NdotL;
  float specularPdf = G1 * d * 0.25f / (NdotV * NdotH);
  data.pdf          = lerp(diffusePdf, specularPdf, bsdfSimpleGlossyProbability(NdotV, mat.metallic));

  data.bsdf_diffuse = mat.baseColor * fDiffuse * diffusePdf;  // Lambertian
  data.bsdf_glossy  = fGlossy * G2 * specularPdf;             // GGX-Smith
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
    float3 halfVector = hvd_ggx_sample_vndf(localK1, mat.roughness, data.xi.xy);
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
  mat.roughness       = float2(roughness * roughness);
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
  mat.roughness = float2(roughness, roughness);
  mat.N         = N;
  orthonormalBasis(mat.N, mat.T, mat.B);
  bsdfEvaluateSimple(data, mat);

  return data.bsdf_diffuse + data.bsdf_glossy;
}


#endif  // PBR_GLSL