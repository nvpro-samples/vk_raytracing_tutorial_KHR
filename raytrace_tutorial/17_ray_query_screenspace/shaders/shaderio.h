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

#define WORKGROUP_SIZE 16

// Binding Points
enum BindingPoints
{
  eTextures = 0,  // Binding point for textures
  eOutImage,      // Binding point for output image
  eTlas,          // Top-level acceleration structure
  eHitBuffer,     // Hit data buffer
};


struct TutoPushConstant
{
  float3x3       normalMatrix;
  int            instanceIndex;              // Instance index for the current draw call
  GltfSceneInfo* sceneInfoAddress;           // Address of the scene information buffer
  float2         metallicRoughnessOverride;  // Metallic and roughness override values
  uint32_t       frame = 0;                  // Current frame index

  // Screen-space effects parameters
  int      enableRayQueryAO = 1;     // Enable ray query ambient occlusion
  float    aoRadius         = 1.0f;  // AO sampling radius
  uint32_t aoSamples        = 64;    // Number of AO samples
  float    aoIntensity      = 2.0f;  // AO intensity multiplier
  int      showAOOnly       = 0;     // Show only AO result (grayscale)
};

NAMESPACE_SHADERIO_END()
