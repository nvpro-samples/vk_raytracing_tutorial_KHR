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


// Mirror of nvshaders/wireframe.h.slang::WireframeSettings, with bools packed
// as ints so the layout matches across the C++ host and Slang shader sides.
struct WireframeSettingsIO
{
  float  thickness      = 1.0f;
  float2 thicknessVar   = float2(1.0f, 1.0f);
  float  smoothing      = 1.0f;
  int    stipple        = 0;  // bool: 0 = off, non-zero = on
  float  stippleLength  = 0.5f;
  float  stippleRepeats = 5.0f;
};


struct TutoPushConstant
{
  // Base-class managed fields (do not remove or rename).
  float3x3       normalMatrix;
  int            instanceIndex;
  GltfSceneInfo* sceneInfoAddress;
  float2         metallicRoughnessOverride;

  // Wireframe-specific fields.
  float3              wireframeColor = float3(0.0f, 0.0f, 0.0f);
  WireframeSettingsIO wf;
};

NAMESPACE_SHADERIO_END()
