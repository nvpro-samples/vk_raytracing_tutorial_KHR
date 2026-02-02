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

// Push constant for infinite plane tutorial
struct TutoPushConstant
{
  float3x3       normalMatrix;
  int            instanceIndex;                      // Instance index for the current draw call
  GltfSceneInfo* sceneInfoAddress;                   // Address of the scene information buffer
  float2         metallicRoughnessOverride;          // Metallic and roughness override values
  int            frame;                              // Frame number for jitter camera anti-aliasing
  int            maxDepth     = 10;                  // Maximum ray depth for path tracing
  float3         planeColor   = {0.7f, 0.9f, 0.6f};  // Color of the infinite plane
  float          planeHeight  = 0.0f;                // Height of the infinite plane
  int            planeEnabled = 1;                   // Toggle for infinite plane (1 = enabled, 0 = disabled)
};

// Binding points
enum BindingPoints
{
  eTextures = 0,
  eTlas     = 1,
  eOutImage = 2
};

NAMESPACE_SHADERIO_END()