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

#include <nvvk/staging.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/mipmaps.hpp>
#include <nvutils/timers.hpp>
#include <nvutils/file_operations.hpp>

#include <stb/stb_image.h>

#include "utils.hpp"


namespace nvsamples {

nvvk::Image loadAndCreateImage(VkCommandBuffer              cmd,
                               nvvk::StagingUploader&       staging,
                               VkDevice                     device,
                               const std::filesystem::path& filename,
                               bool                         sRgb,
                               bool                         generateMipmaps)
{
  // Load the image from disk
  int            w, h, comp, req_comp{4};
  std::string    filenameUtf8 = nvutils::utf8FromPath(filename);
  const stbi_uc* data         = stbi_load(filenameUtf8.c_str(), &w, &h, &comp, req_comp);
  assert((data != nullptr) && "Could not load texture image!");

  const VkExtent2D extent{uint32_t(w), uint32_t(h)};

  // Define how to create the image
  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.format            = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.usage             = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.extent            = {extent.width, extent.height, 1};

  // Mipmap generation needs the image as a blit source and the full mip chain
  // up front. TRANSFER_DST_BIT is added implicitly by the allocator for staging.
  if(generateMipmaps)
  {
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.mipLevels = nvvk::mipLevels(extent);
  }

  nvvk::ResourceAllocator* allocator = staging.getResourceAllocator();

  // Use the VMA allocator to create the image
  const std::span dataSpan(data, w * h * req_comp);
  nvvk::Image     texture;
  NVVK_CHECK(allocator->createImage(texture, imageInfo, DEFAULT_VkImageViewCreateInfo));
  NVVK_CHECK(staging.appendImage(texture, dataSpan, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  if(generateMipmaps && imageInfo.mipLevels > 1)
  {
    // The upload above leaves every mip in SHADER_READ_ONLY_OPTIMAL (mip 0 has
    // real data, the rest were transitioned via barrier only). The mipmap
    // helper expects that as its `currentLayout` and restores it on exit.
    staging.cmdUploadAppended(cmd);
    nvvk::cmdGenerateMipmaps(cmd, texture.image, extent, imageInfo.mipLevels);
  }

  return texture;
}

}  // namespace nvsamples