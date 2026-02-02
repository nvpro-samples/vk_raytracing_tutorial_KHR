/*
 * Copyright (c) 2024-2026, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */


#pragma once

#include <filesystem>

#include <glm/glm.hpp>

#include "io_gltf.h"  // Contains definitions for GLTF GltfMesh, BufferView, TriangleMesh and more

#include "nvutils/bounding_box.hpp"
#include "nvvk/resources.hpp"
#include "nvvk/staging.hpp"
#include "nvutils/primitives.hpp"
#include "tinygltf/tiny_gltf.h"

namespace nvsamples {

// Simple scene resource that holds meshes, instances, and materials
struct GltfSceneResource
{
  std::vector<shaderio::GltfMesh>              meshes;     // All meshes in the scene
  std::vector<shaderio::GltfInstance>          instances;  // All instances in the scene
  std::vector<shaderio::GltfMetallicRoughness> materials;  // All materials in the scene
  shaderio::GltfSceneInfo sceneInfo;  // Scene information (camera matrices, meshes, instances, materials, etc.)

  // GPU buffers for the scene data
  std::vector<nvvk::Buffer> bGltfDatas;  // Buffers containing the GLTF binary data for each loaded scene
  nvvk::Buffer              bMeshes;     // Buffer containing all GltfMesh data
  nvvk::Buffer              bInstances;  // Buffer containing all GltfInstance data
  nvvk::Buffer              bMaterials;  // Buffer containing all GltfMetallicRoughness data
  nvvk::Buffer              bSceneInfo;  // Buffer containing GltfSceneInfo

  // Mapping from mesh index to buffer index in bGltfDatas
  std::vector<uint32_t> meshToBufferIndex;  // meshToBufferIndex[meshIndex] = bufferIndex
};

// This is a utility function to load a GLTF file and return the model data.
tinygltf::Model loadGltfResources(const std::filesystem::path& filename);

// This is a utility function to import the GLTF data into the scene resource.
void importGltfData(GltfSceneResource&     sceneResource,
                    const tinygltf::Model& model,
                    nvvk::StagingUploader& stagingUploader,
                    bool                   importInstance = false);

// This is a utility function to create the scene info buffer.
void createGltfSceneInfoBuffer(GltfSceneResource& sceneResource, nvvk::StagingUploader& stagingUploader);

// This is a utility function to convert a primitive mesh to a GltfMeshResource.
void primitiveMeshToResource(GltfSceneResource& sceneResource, nvvk::StagingUploader& stagingUploader, const nvutils::PrimitiveMesh& primMesh);


}  // namespace nvsamples