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

// glTF-style accessors.
//
// These helpers are typed views over a `BufferView` (offset / byteStride / count),
// exactly the role a glTF "accessor" plays. They are context-free: no globals,
// no payload dependency. The caller is responsible for including `shaderio.h`
// (which defines `BufferView` and `TriangleMesh`).

#ifndef GLTF_ACCESSOR_H
#define GLTF_ACCESSOR_H

#include <nvshaders/slang_types.h>

__generic<T : IFloat> T interpolateBarycentric(T attr0, T attr1, T attr2, float3 barycentrics)
{
    return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

// Reads a single attribute of type T from a buffer view at the given element index.
// Returns T(1) if the buffer view is empty (used as a benign fallback when an
// optional attribute is missing).
__generic<T : IFloat>
T getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint32_t attributeIndex)
{
  if(bufferView.count > 0)
  {
    T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
    return ptr[0];
  }

  return T(1);  // Error case
}

// Reads the three vertex indices of a triangle. Handles both 16-bit and 32-bit index buffers.
// Returns uint3(-1) if the index stride is unsupported.
uint3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh, uint32_t primitiveID)
{
  if(mesh.indices.byteStride == sizeof(uint16_t))
  {
    uint16_t3* indices = (uint16_t3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if(mesh.indices.byteStride == sizeof(uint32_t))
  {
    uint3* indices = (uint3*)(dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return uint3(0xFFFFFFFFu); // Error case
}

// Reads three per-vertex attributes for a triangle and barycentrically blends them.
__generic<T : IFloat>
T getTriangleAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint3 attributeIndex, float3 barycentrics)
{
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);
  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

//-----------------------------------------------------------------------
// Shadow-terminator fix ("Hacking the shadow terminator", Johannes Hanika).
// Adjusts hit position so shadow rays align with interpolated vertex normals.
//-----------------------------------------------------------------------
float3 pointOffset(float3 p, float3 pa, float3 pb, float3 pc, float3 na, float3 nb, float3 nc, float3 bary)
{
  float3 tmpu = p - pa;
  float3 tmpv = p - pb;
  float3 tmpw = p - pc;

  float dotu = min(0.0F, dot(tmpu, na));
  float dotv = min(0.0F, dot(tmpv, nb));
  float dotw = min(0.0F, dot(tmpw, nc));

  tmpu -= dotu * na;
  tmpv -= dotv * nb;
  tmpw -= dotw * nc;

  return p + tmpu * bary.x + tmpv * bary.y + tmpw * bary.z;
}

#endif  // GLTF_ACCESSOR_H
