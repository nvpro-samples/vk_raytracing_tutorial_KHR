/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
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

#pragma once
#include "obj_loader.h"

// The OBJ model
struct ObjModel
{
  uint32_t     nbIndices{0};
  uint32_t     nbVertices{0};
  nvvk::Buffer vertexBuffer;    // Device buffer of all 'Vertex'
  nvvk::Buffer indexBuffer;     // Device buffer of the indices forming triangles
  nvvk::Buffer matColorBuffer;  // Device buffer of array of 'Wavefront material'
  nvvk::Buffer matIndexBuffer;  // Device buffer of array of 'Wavefront material'
};

// Instance of the OBJ
struct ObjInstance
{
  uint32_t      objIndex{0};     // Reference to the `m_objModel`
  uint32_t      txtOffset{0};    // Offset in `m_textures`
  nvmath::mat4f transform{1};    // Position of the instance
  nvmath::mat4f transformIT{1};  // Inverse transpose
};

// Information pushed at each draw call
struct ObjPushConstants
{
  nvmath::vec3f lightPosition{10.f, 15.f, 8.f};
  float         lightIntensity{100.f};
  nvmath::vec3f lightDirection{-1, -1, -1};
  float         lightSpotCutoff{cos(deg2rad(12.5f))};
  float         lightSpotOuterCutoff{cos(deg2rad(17.5f))};
  int           instanceId{0};  // To retrieve the transformation matrix
  int           lightType{0};   // 0: point, 1: infinite
  int           frame{0};
};

enum EObjType
{
  eSphere = 0,
  eCube
};

// One single implicit object
struct ObjImplicit
{
  nvmath::vec3f minimum{0, 0, 0};  // Aabb
  nvmath::vec3f maximum{0, 0, 0};  // Aabb
  int           objType{0};        // 0: Sphere, 1: Cube
  int           matId{0};
};

// All implicit objects
struct ImplInst
{
  std::vector<ObjImplicit> objImpl;     // All objects
  std::vector<MaterialObj> implMat;     // All materials used by implicit obj
  nvvk::Buffer             implBuf;     // Buffer of objects
  nvvk::Buffer             implMatBuf;  // Buffer of material
  int                      blasId{0};
  nvmath::mat4f            transform{1};
};
