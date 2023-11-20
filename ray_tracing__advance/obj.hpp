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

struct ObjInstance
{
  glm::mat4 transform;    // Matrix of the instance
  uint32_t  objIndex{0};  // Model index reference
};


enum EObjType
{
  eSphere = 0,
  eCube
};

// One single implicit object
struct ObjImplicit
{
  glm::vec3 minimum{0, 0, 0};  // Aabb
  glm::vec3 maximum{0, 0, 0};  // Aabb
  int       objType{0};        // 0: Sphere, 1: Cube
  int       matId{0};
};

// All implicit objects
struct ImplInst
{
  std::vector<ObjImplicit> objImpl;     // All objects
  std::vector<MaterialObj> implMat;     // All materials used by implicit obj
  nvvk::Buffer             implBuf;     // Buffer of objects
  nvvk::Buffer             implMatBuf;  // Buffer of material
  int                      blasId{0};
  glm::mat4                transform{1};
};
