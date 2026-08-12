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

// Binding Points (shared with the RtBase base class, which references these slots). This chapter's ray
// tracing set binds only the output image; the PTLAS is referenced by device address (passed in the
// push constant), not through the eTlas slot, so eTextures/eTlas stay unused here.
enum BindingPoints
{
  eTextures = 0,  // textures (RtBase convention; unused in this chapter)
  eOutImage,      // the ray tracing output image
  eTlas,          // TLAS slot (RtBase convention; unused here - the PTLAS is passed by device address)
};

// Sentinel instanceID marking the ground plane (all other instances carry their partition index)
#define PT_GROUND_ID 0xFFFFFFFF

// Grid layout, shared by host and shader so the shader can recompute a partition's center and classify
// its update zone (NEAR / MID / FAR) exactly like the host does.
#define PT_GRID_N 32                        // instances per axis
#define PT_TILE 4                           // instances per partition-axis
#define PT_PART_AXIS (PT_GRID_N / PT_TILE)  // partitions per axis
#define PT_SPACING 1.1f                     // world spacing between instances

// Push constant shared with the base class (glTF fields kept for RtBase compatibility),
// extended with the values this chapter needs.
struct TutoPushConstant
{
  float3x3       normalMatrix;
  int            instanceIndex;     // Instance index for the current draw call
  GltfSceneInfo* sceneInfoAddress;  // Address of the scene information buffer (camera matrices)
  uint64_t       tlasAddress;       // Device address of the PTLAS (traced via OpConvertUToAccelerationStructure)
  float2         metallicRoughnessOverride;  // Metallic and roughness override values
  float          radius = 1.0f;

  // --- Partitioned-TLAS chapter additions ---
  float3 lightDir;    // World-space direction toward the light
  float2 cameraXZ;    // Camera position on the ground plane (for zone classification in the shader)
  float  nearRadius;  // < nearRadius : NEAR zone (per-instance updates)
  float  midRadius;   // < midRadius  : MID zone (per-partition translation); beyond : FAR (retained)
};

NAMESPACE_SHADERIO_END()
