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

#pragma once

#define MAX_RECURSION_DEPTH 4

#ifdef __cplusplus
// C++ specific
constexpr int PIPELINE_DEPTH = MAX_RECURSION_DEPTH + 2;
#endif

#include "common/io_gltf.h"

NAMESPACE_SHADERIO_BEGIN()

// Binding Points
enum BindingPoints
{
  eTextures = 0,  // Binding point for textures
  eOutImage,      // Binding point for output image
  eTlas,          // Top-level acceleration structure
};

// Material Types
enum MaterialType
{
  eDiffuse = 0,
  ePlastic,
  eGlass,
  eConstant,
  eMaterialCount
};

// Texture Types
enum TextureType
{
  eNone = 0,
  eNoise,
  eChecker,
  eVoronoi,
  eTextureCount
};

// Callable data for material evaluation
struct MaterialData
{
  float3 worldPos;
  float3 worldNormal;
  float3 viewDir;
  float2 texCoord;
  float3 baseColor;
  float  metallic;
  float  roughness;
  float3 emissive;
  float3 albedo;
  float3 normal;
  float3 emission;
  float  alpha;
};

// Callable data for texture evaluation
struct TextureData
{
  float2 texCoord;
  float3 color;
  float3 normal;
  float  roughness;
  float  metallic;
};

struct TutoPushConstant
{
  float3x3       normalMatrix;
  int            instanceIndex;              // Instance index for the current draw call
  GltfSceneInfo* sceneInfoAddress;           // Address of the scene information buffer
  float2         metallicRoughnessOverride;  // Metallic and roughness override values
  float          time;                       // Time for animated textures
  int            textureType;                // Texture type for callable shader selection (global override)
};

NAMESPACE_SHADERIO_END()