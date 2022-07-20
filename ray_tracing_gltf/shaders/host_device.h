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


#ifndef COMMON_HOST_DEVICE
#define COMMON_HOST_DEVICE

#ifdef __cplusplus
#include "nvmath/nvmath.h"
#include <stdint.h> /* for uint64_t */
// GLSL Type
using vec2 = nvmath::vec2f;
using vec3 = nvmath::vec3f;
using vec4 = nvmath::vec4f;
using mat4 = nvmath::mat4f;
using uint = unsigned int;
#endif

// clang-format off
#ifdef __cplusplus // Descriptor binding helper for C++ and GLSL
 #define START_BINDING(a) enum a {
 #define END_BINDING() }
#else
 #define START_BINDING(a)  const uint
 #define END_BINDING() 
#endif

START_BINDING(SceneBindings)
  eGlobals   = 0,  // Global uniform containing camera matrices
  eSceneDesc = 1,  // Access to the scene buffers
  eTextures  = 2   // Access to textures
END_BINDING();

START_BINDING(RtxBindings)
  eTlas       = 0,  // Top-level acceleration structure
  eOutImage   = 1,  // Ray tracer output image
  ePrimLookup = 2   // Lookup of objects
END_BINDING();
// clang-format on

// Scene buffer addresses
struct SceneDesc
{
  uint64_t vertexAddress;    // Address of the Vertex buffer
  uint64_t normalAddress;    // Address of the Normal buffer
  uint64_t uvAddress;        // Address of the texture coordinates buffer
  uint64_t indexAddress;     // Address of the triangle indices buffer
  uint64_t materialAddress;  // Address of the Materials buffer (GltfShadeMaterial)
  uint64_t primInfoAddress;  // Address of the mesh primitives buffer (PrimMeshInfo)
};

// Uniform buffer set at each frame
struct GlobalUniforms
{
  mat4 viewProj;     // Camera view * projection
  mat4 viewInverse;  // Camera inverse view matrix
  mat4 projInverse;  // Camera inverse projection matrix
};

// Push constant structure for the raster
struct PushConstantRaster
{
  mat4  modelMatrix;  // matrix of the instance
  vec3  lightPosition;
  uint  objIndex;
  float lightIntensity;
  int   lightType;
  int   materialId;
};


// Push constant structure for the ray tracer
struct PushConstantRay
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;
  int   frame;
};

// Structure used for retrieving the primitive information in the closest hit
struct PrimMeshInfo
{
  uint indexOffset;
  uint vertexOffset;
  int  materialIndex;
};

struct GltfShadeMaterial
{
  vec4 pbrBaseColorFactor;
  vec3 emissiveFactor;
  int  pbrBaseColorTexture;
};

#endif
