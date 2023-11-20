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
#include <glm/glm.hpp>
#include "tiny_obj_loader.h"
#include <array>
#include <iostream>
#include <stdint.h>
#include <unordered_map>
#include <vector>

// Structure holding the material
struct MaterialObj
{
  glm::vec3 ambient       = glm::vec3(0.1f, 0.1f, 0.1f);
  glm::vec3 diffuse       = glm::vec3(0.7f, 0.7f, 0.7f);
  glm::vec3 specular      = glm::vec3(1.0f, 1.0f, 1.0f);
  glm::vec3 transmittance = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 emission      = glm::vec3(0.0f, 0.0f, 0.10);
  float     shininess     = 0.f;
  float     ior           = 1.0f;  // index of refraction
  float     dissolve      = 1.f;   // 1 == opaque; 0 == fully transparent
                                   // illumination model (see http://www.fileformat.info/format/material/)
  int illum     = 0;
  int textureID = -1;
};
// OBJ representation of a vertex
// NOTE: BLAS builder depends on pos being the first member
struct VertexObj
{
  glm::vec3 pos;
  glm::vec3 nrm;
  glm::vec3 color;
  glm::vec2 texCoord;
};


struct shapeObj
{
  uint32_t offset;
  uint32_t nbIndex;
  uint32_t matIndex;
};

class ObjLoader
{
public:
  void loadModel(const std::string& filename);

  std::vector<VertexObj>   m_vertices;
  std::vector<uint32_t>    m_indices;
  std::vector<MaterialObj> m_materials;
  std::vector<std::string> m_textures;
  std::vector<int32_t>     m_matIndx;
};
