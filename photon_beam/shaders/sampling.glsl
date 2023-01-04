/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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



// Generate a random unsigned int from two unsigned int values, using 16 pairs
// of rounds of the Tiny Encryption Algorithm. See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
uint tea(uint val0, uint val1)
{
  uint v0 = val0;
  uint v1 = val1;
  uint s0 = 0;

  for(uint n = 0; n < 16; n++)
  {
    s0 += 0x9e3779b9;
    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
  }

  return v0;
}

// Generate a random unsigned int in [0, 2^24) given the previous RNG state
// using the Numerical Recipes linear congruential generator
uint lcg(inout uint prev)
{
  uint LCG_A = 1664525u;
  uint LCG_C = 1013904223u;
  prev       = (LCG_A * prev + LCG_C);
  return prev & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float rnd(inout uint prev)
{
  return (float(lcg(prev)) / float(0x01000000));
}


//-------------------------------------------------------------------------------------------------
// Sampling
//-------------------------------------------------------------------------------------------------

// Randomly sampling around +Z
// pdf of the sampling is cos theta 
// where theta is the angle between the sampling vector and the norm vector(+Z) 
vec3 samplingHemisphere(inout uint seed, in vec3 x, in vec3 y, in vec3 z)
{
#define M_PI 3.141592

  float r1 = rnd(seed);
  float r2 = rnd(seed);
  float sq = sqrt(1.0 - r2);

  vec3 direction = vec3(cos(2 * M_PI * r1) * sq, sin(2 * M_PI * r1) * sq, sqrt(r2));
  direction      = direction.x * x + direction.y * y + direction.z * z;

  return direction;
}

vec3 uniformSamplingSphere(inout uint seed)
{

  float r1 = rnd(seed);
  float r2 = rnd(seed) * 2 -1;
  float sq = sqrt(1.0 - r2 * r2);

  vec3 direction = vec3(cos(2 * M_PI * r1) * sq, sin(2 * M_PI * r1) * sq, r2);

  return direction;
}

// Return the tangent and binormal from the incoming normal
void createCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb)
{
  if(abs(N.x) > abs(N.y))
    Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
  else
    Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
  Nb = cross(N, Nt);
}


// Henyey-Greenstein phase function
float heneyGreenPhaseFunc(float cosTheta, float g)
{
  // assymetriy factor
  // this value must be between -1 to 1
  // 0 value gives uniform phase function
  // value > 0 gives light out-scattering forward
  // value < 0 gives light out-scattering backward
  float g2    = g * g;
  float denom = 1 + g2 - 2 * g * cosTheta;

  return (1 - g2) / (denom * sqrt(denom)) / (4 * M_PI);
}

// normal is incoming ray direction start from the light source 
vec3 heneyGreenPhaseFuncSampling(inout uint seed, in vec3 normal, float g)
{
  float r1 = rnd(seed);
  float r2 = rnd(seed);

  float g2 = g * g;
  float g3 = g2 * g;

  float s1 = 2 * r1 - 1;
  float s2 = s1 * s1;

  float denom = 1 + g * s1;
  denom       = denom * denom * 2;

  float numerator = 2 * s1 + g * (s2 + 3) + g2 * (2 * s1) + g3 * (s2 - 1);


  if(denom == 0.0)
  {
    denom += 0.000001;
  }


  // cos theta == 1 -> front scattering, result direction is exactly same as the incoming light direction(direction start from the light source)
  // cos theta == -1 -> back ward scattering, result direction is opposite of the incoming light direction(direction start from the light source)
  float cos_theta = numerator / denom;
  float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
  float phi   = 2.0f * M_PI * r2;

  vec3 ret = vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));

  vec3 tangent, bitangent;
  createCoordinateSystem(normal, tangent, bitangent);
  ret = ret.x * bitangent + ret.y * normal + ret.z * tangent;

  // normalize at last step in order to avoid some floating point error;
  return normalize(ret);
}

// PDF for half vector
// nDotH is the dot product between half vector and surfcace normal
// half vector = normalize (incident light direction +  refliected light direction)
// both incident light and reflected light directions start from the point of the reflection
float microfacetPDF(float nDotH, float a2)
{
    if(nDotH < 0)
        return 0.0;

    if(a2 <= 0.0)
      return 0.0;

    float denom = (nDotH * nDotH * (a2 - 1.0) + 1);
    denom       = denom * denom * M_PI;
    return a2 / denom;
 
}

// PDF for the reflected light
// hDotL is the dot product between half vector and reflected light direction
float microfacetLightPDF(float hDotL, float nDotH, float a2)
{
  return microfacetPDF(nDotH, a2) / (4 * hDotL);
}

// https://schuttejoe.github.io/post/ggximportancesamplingpart1/
// https://agraphicsguy.wordpress.com/2015/11/01/sampling-microfacet-brdf/
// incomiing LightDir is direction start from light source and goes toward the point of the interaction.
vec3 microfacetReflectedLightSampling(inout uint seed, in vec3 incomingLightDir, in vec3 normal, float roughness)
{
    float r1 = rnd(seed);
    float r2 = rnd(seed);

    float a = roughness * roughness;
    float theta = atan(a * sqrt(r1 / (1 - r1) )); 
    float phi   = 2 * M_PI * r2;

    vec3 halfVec = vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
                                                                                                                                                                                                                         
    vec3 tangent, bitangent;
    createCoordinateSystem(normal, tangent, bitangent);

    halfVec = halfVec.x * bitangent + halfVec.y * normal + halfVec.z * tangent;

    // normalize at last step in order to avoid some floating point error;
    return normalize(incomingLightDir - 2 * dot(halfVec, incomingLightDir) * halfVec);
}

// both incoming light and reflected light directions start from the point of the reflection
vec3 gltfBrdf(in vec3 incomingLightDir, in vec3 reflectedLightDir, in vec3 normal, in vec3 baseColor, float roughness, float metallic)
{
  float a2      = pow(roughness, 4.0);
  vec3  halfVec = normalize(incomingLightDir + reflectedLightDir);
  float nDotH   = dot(normal, halfVec);
  float nDotL   = dot(normal, incomingLightDir);
  float vDotH   = dot(reflectedLightDir, halfVec);
  float hDotL   = dot(incomingLightDir, halfVec);
  float vDotN   = dot(reflectedLightDir, normal);

  vec3 c_diff    = (1.0 - metallic) * baseColor;
  vec3 f0        = 0.04 * (1 - metallic) + baseColor * metallic;
  vec3 frsnel    = f0 + (1 - f0) * pow(1 - abs(vDotH), 5);
  vec3 f_diffuse = (1.0 - frsnel) / M_PI * c_diff;

  float dVal = 0.0;
  // roughness = 0.0, nDotH = 1.0 -> microfacetPDF = inf
  if(roughness > 0.0 || nDotH < 0.9999)
  {
    dVal = microfacetPDF(nDotH, roughness);
  }
  // I am actually not sure what to do in this case
  // For now just get microfacetPDF(1.0, 0.000001)
  else
  {
    dVal = microfacetPDF(1.0, 0.000001);
  }

  float gVal = 0.0;
  if(hDotL > 0 && vDotH > 0)
  {
    float denom1 = sqrt(a2 + (1 - a2) * nDotL * nDotL);
    denom1 += abs(nDotL);

    float denom2 = sqrt(a2 + (1 - a2) * vDotN * vDotN);
    denom2 += abs(vDotN);

    gVal = 1.0 / (denom1 * denom2);
  }

  vec3 f_specular = frsnel * dVal * gVal;
  return f_specular + f_diffuse;
}

// weight gltfBRDF value with the pdf value of the reflected light
vec3 pdfWeightedGltfBrdf(in vec3 incomingLightDir, in vec3 reflectedLightDir, in vec3 normal, in vec3 baseColor, float roughness, float metallic)
{
  float a2      = pow(roughness, 4.0);
  vec3  halfVec = normalize(incomingLightDir + reflectedLightDir);
  float nDotH   = dot(normal, halfVec);
  float nDotL   = dot(normal, incomingLightDir);
  float vDotH   = dot(reflectedLightDir, halfVec);
  float hDotL   = dot(incomingLightDir, halfVec);
  float vDotN   = dot(reflectedLightDir, normal);

  vec3 c_diff    = (1.0 - metallic) * baseColor;
  vec3 f0        = 0.04 * (1 - metallic) + baseColor * metallic;
  vec3 frsnel    = f0 + (1 - f0) * pow(1 - abs(vDotH), 5);
  vec3 f_diffuse = vec3(0.0);

  // hDotL = 0 ->  microfacetLightPDF = inf -> f_diffuse = 0
  // roughness = 0.0, nDotH = 1.0 -> microfacetPDF = inf -> microfacetLightPDF = inf -> f_diffuse = 0
  if((roughness > 0.0 || nDotH < 0.999) && hDotL > 0.0)
  {
    f_diffuse = (1.0 - frsnel) / M_PI * c_diff / microfacetLightPDF(hDotL, nDotH, a2);
  }

  float gVal = 0.0;
  if(hDotL > 0 && vDotH > 0)
  {
    float denom1 = sqrt(a2 + (1 - a2) * nDotL * nDotL);
    denom1 += abs(nDotL);

    float denom2 = sqrt(a2 + (1 - a2) * vDotN * vDotN);
    denom2 += abs(vDotN);

    gVal = 1.0 / (denom1 * denom2);
  }

  vec3 f_specular = frsnel * gVal * (4 * hDotL);
  return f_specular + f_diffuse;
}
