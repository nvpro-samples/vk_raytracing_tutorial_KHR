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

#define _USE_MATH_DEFINES
#include <array>
#include <map>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>  // Perlin noise


#include "mm_process.hpp"
#include "bird_curve_helper.hpp"
#include "bit_packer.hpp"
#include "nvvk/resource_allocator.hpp"
#include "nvutils/timers.hpp"
#include "nvvk/staging.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"
#include "nvutils/parallel_work.hpp"


MicromapProcess::MicromapProcess(nvvk::ResourceAllocator* allocator)
    : m_alloc(allocator)
{
  m_device = allocator->getDevice();

  // Requesting ray tracing properties
  VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  prop2.pNext = &m_oppacityProps;
  vkGetPhysicalDeviceProperties2(allocator->getPhysicalDevice(), &prop2);
}

MicromapProcess::~MicromapProcess()
{
  m_alloc->destroyBuffer(m_inputData);
  m_alloc->destroyBuffer(m_microData);
  m_alloc->destroyBuffer(m_trianglesBuffer);
  m_alloc->destroyBuffer(m_scratchBuffer);
  m_alloc->destroyBuffer(m_indexBuffer);
  vkDestroyMicromapEXT(m_device, m_micromap, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Create the data for displacement
// - Get a vector of displacement values per triangle
// - Pack the data to 11 bit (64_TRIANGLES_64_BYTES format)
// - Get the usage
// - Create the vector of VkMicromapTriangleEXT
bool MicromapProcess::createMicromapData(VkCommandBuffer               cmd,
                                         nvvk::StagingUploader&        uploader,
                                         const nvutils::PrimitiveMesh& mesh,
                                         uint16_t                      subdivLevel,
                                         float                         radius,
                                         uint16_t                      micromapFormat)
{
  nvutils::ScopedTimer stimer("Create Micromap Data");

  vkDestroyMicromapEXT(m_device, m_micromap, nullptr);
  m_alloc->destroyBuffer(m_scratchBuffer);
  m_alloc->destroyBuffer(m_inputData);
  m_alloc->destroyBuffer(m_microData);
  m_alloc->destroyBuffer(m_trianglesBuffer);
  m_alloc->destroyBuffer(m_indexBuffer);

  // Get an array of displacement per triangle
  MicroOpacity micro_dist = createOpacity(mesh, subdivLevel, radius);

  // Number of triangles in the mesh and number of micro-triangles in a triangle
  const auto num_tri       = static_cast<uint32_t>(micro_dist.rawTriangles.size());
  const auto num_micro_tri = BirdCurveHelper::getNumMicroTriangles(subdivLevel);

  // Micromesh Usage
  {
    // The usage is like an histogram; how many triangles, using a `format` and a `subdivisionLevel`.
    // Since all our triangles have the same subdivision level, and the same storage format, there is only
    // one usage.
    m_usages.resize(1);
    m_usages[0].count            = num_tri;
    m_usages[0].format           = micromapFormat;
    m_usages[0].subdivisionLevel = subdivLevel;
  }

  // Can store 8 triangle info per byte for VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
  uint32_t storage_byte = (num_micro_tri + 7) / 8;
  if(micromapFormat == VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT)
  {
    storage_byte *= 2;  // Need twice as much for the 4 state
  }

  // Micromesh Input Values
  {
    // Allocate the array to push on the GPU.
    std::vector<uint8_t> packed_data(storage_byte * num_tri);
    memset(packed_data.data(), 0U, static_cast<unsigned long long>(storage_byte) * num_tri * sizeof(uint8_t));

    // Loop over all triangles of the mesh
    for(uint32_t tri_index = 0U; tri_index < num_tri; tri_index++)
    {
      // The offset from the start of packed_data, must be a multiple of 64 bit
      uint32_t offset = storage_byte * tri_index;

      // Access to all displacement values
      const std::vector<int>& values = micro_dist.rawTriangles[tri_index].values;

      // The BitPacker will store contiguously unorm11 (float normalized on 11 bit), from the beginning of the
      // triangle (offset), plus each extra block
      BitPacker packer(&packed_data[offset]);

      // Loop for all block of 64 triangles
      for(const auto& value : values)
      {
        if(micromapFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT)
        {
          if(value == VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT)
          {
            packer.push(0, 1);
          }
          else
          {
            packer.push(1, 1);
          }
        }
        else
        {
          if(value == VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT)
          {
            packer.push(0, 2);
          }
          else if(value == VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT)
          {
            packer.push(1, 2);
          }
          else
          {
            packer.push(3, 2);
          }
        }
      }
    }


    NVVK_CHECK(m_alloc->createBuffer(m_inputData, std::span(packed_data).size_bytes(),
                                     VK_BUFFER_USAGE_2_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                         | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                     VMA_MEMORY_USAGE_AUTO));
    NVVK_CHECK(uploader.appendBuffer(m_inputData, 0, std::span(packed_data)));
    NVVK_DBG_NAME(m_inputData.buffer);
  }

  // Micromap Triangle
  {
    std::vector<VkMicromapTriangleEXT> micromap_triangles;
    micromap_triangles.reserve(num_tri);
    for(uint32_t tri_index = 0; tri_index < num_tri; tri_index++)
    {
      uint32_t offset = storage_byte * tri_index;  // Same offset as when storing the data
      micromap_triangles.push_back({offset, subdivLevel, micromapFormat});
    }
    NVVK_CHECK(m_alloc->createBuffer(m_trianglesBuffer, std::span(micromap_triangles).size_bytes(),
                                     VK_BUFFER_USAGE_2_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                         | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                     VMA_MEMORY_USAGE_AUTO));
    NVVK_CHECK(uploader.appendBuffer(m_trianglesBuffer, 0, std::span(micromap_triangles)));
    NVVK_DBG_NAME(m_trianglesBuffer.buffer);
  }

  // Index buffer: referencing the Micromap Triangle buffer
  {
    std::vector<uint32_t> index(num_tri);
    int                   cnt{0};
    for(auto& i : index)
    {
      i = cnt++;
    }

    NVVK_CHECK(m_alloc->createBuffer(m_indexBuffer, std::span(index).size_bytes(),
                                     VK_BUFFER_USAGE_2_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT
                                         | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT));
    NVVK_CHECK(uploader.appendBuffer(m_indexBuffer, 0, std::span(index)));
    NVVK_DBG_NAME(m_indexBuffer.buffer);
  }

  // Upload the data to the GPU
  uploader.cmdUploadAppended(cmd);

  // Barrier to make sure the data is ready before building the micromap
  barrier(cmd);

  // Build the micromap
  buildMicromap(cmd, VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT);

  return true;
}

//--------------------------------------------------------------------------------------------------
// Building the micromap using: triangle data, input data (values), usage
//
bool MicromapProcess::buildMicromap(VkCommandBuffer cmd, VkMicromapTypeEXT micromapType)
{
  nvutils::ScopedTimer stimer("Build Micromap");

  // Find the size required
  VkMicromapBuildSizesInfoEXT size_info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
  VkMicromapBuildInfoEXT      build_info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
  build_info.mode             = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
  build_info.flags            = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
  build_info.usageCountsCount = static_cast<uint32_t>(m_usages.size());
  build_info.pUsageCounts     = m_usages.data();
  build_info.type             = micromapType;  // Opacity
  vkGetMicromapBuildSizesEXT(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &size_info);
  assert(size_info.micromapSize && "sizeInfo.micromeshSize was zero");

  // create micromeshData buffer
  NVVK_CHECK(m_alloc->createBuffer(m_microData, size_info.micromapSize,
                                   VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_MICROMAP_STORAGE_BIT_EXT));
  NVVK_DBG_NAME(m_microData.buffer);

  uint64_t scratch_size = std::max(size_info.buildScratchSize, static_cast<VkDeviceSize>(4));
  NVVK_CHECK(m_alloc->createBuffer(m_scratchBuffer, scratch_size,
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                       | VK_BUFFER_USAGE_2_MICROMAP_STORAGE_BIT_EXT));
  NVVK_DBG_NAME(m_scratchBuffer.buffer);

  // Create micromap
  VkMicromapCreateInfoEXT mm_create_info{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
  mm_create_info.buffer = m_microData.buffer;
  mm_create_info.size   = size_info.micromapSize;
  mm_create_info.type   = micromapType;
  NVVK_CHECK(vkCreateMicromapEXT(m_device, &mm_create_info, nullptr, &m_micromap));

  {
    // Fill in the pointers we didn't have at size query
    build_info.dstMicromap                 = m_micromap;
    build_info.scratchData.deviceAddress   = m_scratchBuffer.address;
    build_info.data.deviceAddress          = m_inputData.address;
    build_info.triangleArray.deviceAddress = m_trianglesBuffer.address;
    build_info.triangleArrayStride         = sizeof(VkMicromapTriangleEXT);
    vkCmdBuildMicromapsEXT(cmd, 1, &build_info);
  }
  barrier(cmd);

  return true;
}

//--------------------------------------------------------------------------------------------------
// This can be called when the Micromap has been build
//
void MicromapProcess::cleanBuildData()
{
  m_alloc->destroyBuffer(m_scratchBuffer);
  m_alloc->destroyBuffer(m_inputData);
  m_alloc->destroyBuffer(m_trianglesBuffer);
}


//--------------------------------------------------------------------------------------------------
// Make sure all the data are ready before building the micromap
void MicromapProcess::barrier(VkCommandBuffer cmd)
{
  // barrier for upload finish
  VkMemoryBarrier2 mem_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,         nullptr,
                               VK_PIPELINE_STAGE_2_TRANSFER_BIT,           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_2_MICROMAP_READ_BIT_EXT};
  VkDependencyInfo dep_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep_info.memoryBarrierCount = 1;
  dep_info.pMemoryBarriers    = &mem_barrier;
  vkCmdPipelineBarrier2(cmd, &dep_info);
}

// Intersecting of a triangle and a circle
// Return 2 when triangle is within the circle
// Return 1 when triangle intersect, point, edge or surface
// Return 0 when it is totally outside
static uint32_t triangleCircleItersection(const std::array<glm::vec3, 3>& p, const glm::vec3& center, float radius)
{
  const float radiusSqr = radius * radius;

  // Pre-calculate center-to-vertex vectors and their squared distances to circle
  struct
  {
    glm::vec3 vec;
    float     sqrDist;
  } c[3] = {{center - p[0], 0.0f}, {center - p[1], 0.0f}, {center - p[2], 0.0f}};

  // Check vertices within circle and calculate squared distances
  int hit = 0;
  for(int i = 0; i < 3; i++)
  {
    c[i].sqrDist = glm::dot(c[i].vec, c[i].vec) - radiusSqr;
    if(c[i].sqrDist <= 0)
    {
      hit++;
      if(hit == 3)
        return 2;  // Early exit: Completely inside the circle
    }
  }

  if(hit > 0)
    return 1;  // Circle crossing the triangle - at least one vertex inside

  // Calculate edges for edge intersection tests
  const glm::vec3 edges[3] = {p[1] - p[0], p[2] - p[1], p[0] - p[2]};

  // Check if circle intersects any edge
  for(int i = 0; i < 3; i++)
  {
    const float k = glm::dot(edges[i], c[i].vec);
    if(k > 0)
    {
      const float lenSqr = glm::dot(edges[i], edges[i]);
      if(k < lenSqr && c[i].sqrDist * lenSqr <= k * k)
        return 1;  // Circle intersects this edge
    }
  }

  return 0;  // Triangle outside circle
}


//--------------------------------------------------------------------------------------------------
// Set the visibility information per micro-triangle.
// - A micro triangle will be fully opaque if all its position are within the `radius`.
//   fully transparent when all its position are outside and unknown if one position crosses
//   the radius boundary.
MicromapProcess::MicroOpacity MicromapProcess::createOpacity(const nvutils::PrimitiveMesh& mesh, uint16_t subdivLevel, float radius)
{
  nvutils::ScopedTimer stimer("Create Displacements");

  MicroOpacity displacements;  // Return of displacement values for all triangles

  const auto num_micro_tri = BirdCurveHelper::getNumMicroTriangles(subdivLevel);

  auto num_tri = static_cast<uint32_t>(mesh.triangles.size());
  displacements.rawTriangles.resize(num_tri);

  const glm::vec3 center{0.0F, 0.0F, 0.0F};

  // Find the distances in parallel
  // Faster than : for(size_t tri_index = 0; tri_index < num_tri; tri_index++)
  nvutils::parallel_batches<32>(
      num_tri,
      [&](uint64_t tri_index) {
        // Retrieve the positions of the triangle
        glm::vec3 t0 = mesh.vertices[mesh.triangles[tri_index].indices[0]].pos;
        glm::vec3 t1 = mesh.vertices[mesh.triangles[tri_index].indices[1]].pos;
        glm::vec3 t2 = mesh.vertices[mesh.triangles[tri_index].indices[2]].pos;

        // Working on this triangle
        RawTriangle& triangle = displacements.rawTriangles[tri_index];
        triangle.values.resize(num_micro_tri);
        triangle.subdivLevel = subdivLevel;

        // TODO: check if the triangle is completely in or out to avoid subdividing it
        // uint32_t hit = triangleCircleItersection({t0, t1, t2}, center, radius);

        for(uint32_t index = 0; index < num_micro_tri; index++)
        {
          // Utility to get the barycentric values
          glm::vec3 uv0, uv1, uv2;
          BirdCurveHelper::micro2bary(index, subdivLevel, uv0, uv1, uv2);

          // The sub-triangle position
          glm::vec3 p0 = getInterpolated(t0, t1, t2, uv0);
          glm::vec3 p1 = getInterpolated(t0, t1, t2, uv1);
          glm::vec3 p2 = getInterpolated(t0, t1, t2, uv2);

          // Check how many sub-triangle vertex are within the radius
          uint32_t hit = triangleCircleItersection({p0, p1, p2}, center, radius);

          // Determining the visibility of the triangle
          switch(hit)
          {
            case 2:
              triangle.values[index] = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT;
              break;
            case 0:
              triangle.values[index] = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT;
              break;
            default:
              triangle.values[index] = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_UNKNOWN_TRANSPARENT_EXT;
              break;
          }
        }
      },
      std::thread::hardware_concurrency());

  return displacements;
}
