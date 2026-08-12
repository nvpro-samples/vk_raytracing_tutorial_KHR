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

#pragma once


#include "common/io_gltf.h"

NAMESPACE_SHADERIO_BEGIN()

// Binding Points
enum BindingPoints
{
  eTextures = 0,  // Binding point for textures
  eOutImage,      // Binding point for output image
  eTlas,          // Top-level acceleration structure
};


// Push constant shared with the base class (glTF fields kept for RtBase compatibility),
// extended with the two values this chapter needs.
struct TutoPushConstant
{
  float3x3       normalMatrix;
  int            instanceIndex;              // Instance index for the current draw call
  GltfSceneInfo* sceneInfoAddress;           // Address of the scene information buffer (camera matrices)
  float2         metallicRoughnessOverride;  // Metallic and roughness override values
  float          radius = 1.0f;

  // --- Cluster chapter additions ---
  float3 lightDir;        // World-space direction toward the light
  int    colorByCluster;  // 1: color each surface by its cluster ID, 0: plain shading
};

NAMESPACE_SHADERIO_END()
