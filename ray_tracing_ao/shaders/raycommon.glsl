/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


//-
// This utility compresses a normal(x,y,z) to a uint and decompresses it


#define C_Stack_Max 3.402823466e+38f
uint CompressUnitVec(vec3 nv)
{
  // map to octahedron and then flatten to 2D (see 'Octahedron Environment Maps' by Engelhardt & Dachsbacher)
  if((nv.x < C_Stack_Max) && !isinf(nv.x))
  {
    const float d = 32767.0f / (abs(nv.x) + abs(nv.y) + abs(nv.z));
    int         x = int(roundEven(nv.x * d));
    int         y = int(roundEven(nv.y * d));
    if(nv.z < 0.0f)
    {
      const int maskx = x >> 31;
      const int masky = y >> 31;
      const int tmp   = 32767 + maskx + masky;
      const int tmpx  = x;
      x               = (tmp - (y ^ masky)) ^ maskx;
      y               = (tmp - (tmpx ^ maskx)) ^ masky;
    }
    uint packed = (uint(y + 32767) << 16) | uint(x + 32767);
    if(packed == ~0u)
      return ~0x1u;
    return packed;
  }
  else
  {
    return ~0u;
  }
}

float ShortToFloatM11(const int v)  // linearly maps a short 32767-32768 to a float -1-+1 //!! opt.?
{
  return (v >= 0) ? (uintBitsToFloat(0x3F800000u | (uint(v) << 8)) - 1.0f) :
                    (uintBitsToFloat((0x80000000u | 0x3F800000u) | (uint(-v) << 8)) + 1.0f);
}
vec3 DecompressUnitVec(uint packed)
{
  if(packed != ~0u)  // sanity check, not needed as isvalid_unit_vec is called earlier
  {
    int       x     = int(packed & 0xFFFFu) - 32767;
    int       y     = int(packed >> 16) - 32767;
    const int maskx = x >> 31;
    const int masky = y >> 31;
    const int tmp0  = 32767 + maskx + masky;
    const int ymask = y ^ masky;
    const int tmp1  = tmp0 - (x ^ maskx);
    const int z     = tmp1 - ymask;
    float     zf;
    if(z < 0)
    {
      x  = (tmp0 - ymask) ^ maskx;
      y  = tmp1 ^ masky;
      zf = uintBitsToFloat((0x80000000u | 0x3F800000u) | (uint(-z) << 8)) + 1.0f;
    }
    else
    {
      zf = uintBitsToFloat(0x3F800000u | (uint(z) << 8)) - 1.0f;
    }
    return normalize(vec3(ShortToFloatM11(x), ShortToFloatM11(y), zf));
  }
  else
  {
    return vec3(C_Stack_Max);
  }
}


//-------------------------------------------------------------------------------------------------
// Avoiding self intersections (see Ray Tracing Gems, Ch. 6)
//
vec3 OffsetRay(in vec3 p, in vec3 n)
{
  const float intScale   = 256.0f;
  const float floatScale = 1.0f / 65536.0f;
  const float origin     = 1.0f / 32.0f;

  ivec3 of_i = ivec3(intScale * n.x, intScale * n.y, intScale * n.z);

  vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
                  intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
                  intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

  return vec3(abs(p.x) < origin ? p.x + floatScale * n.x : p_i.x,  //
              abs(p.y) < origin ? p.y + floatScale * n.y : p_i.y,  //
              abs(p.z) < origin ? p.z + floatScale * n.z : p_i.z);
}


//////////////////////////// AO //////////////////////////////////////
#define EPS 0.05
const float M_PI = 3.141592653589;

void ComputeDefaultBasis(const vec3 normal, out vec3 x, out vec3 y)
{
  // ZAP's default coordinate system for compatibility
  vec3        z  = normal;
  const float yz = -z.y * z.z;
  y = normalize(((abs(z.z) > 0.99999f) ? vec3(-z.x * z.y, 1.0f - z.y * z.y, yz) : vec3(-z.x * z.z, yz, 1.0f - z.z * z.z)));

  x = cross(y, z);
}

//-------------------------------------------------------------------------------------------------
// Random
//-------------------------------------------------------------------------------------------------


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

uvec2 pcg2d(uvec2 v)
{
  v = v * 1664525u + 1013904223u;

  v.x += v.y * 1664525u;
  v.y += v.x * 1664525u;

  v = v ^ (v >> 16u);

  v.x += v.y * 1664525u;
  v.y += v.x * 1664525u;

  v = v ^ (v >> 16u);

  return v;
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
float rnd(inout uint seed)
{
  return (float(lcg(seed)) / float(0x01000000));
}
