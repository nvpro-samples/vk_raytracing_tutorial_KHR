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

// Primary camera ray generation, shared by tutorial samples 03+.
//
// Requires the caller to include `shaderio.h` (for `GltfSceneInfo`) and
// `nvshaders/constants.h.slang` (for `INFINITE`).

#ifndef TUTORIAL_RAY_COMMON_H
#define TUTORIAL_RAY_COMMON_H

#include "nvshaders/constants.h.slang"

/*-------------------------------------------------------------------------------------------------
# Function `generatePrimaryRay`
> Builds a world-space primary ray through `pixelPos` on the image plane.

A half-pixel offset is applied internally so that integer `pixelPos` values
sample the pixel center (matching standard rasterization conventions).

`pixelPos` is the position within the image (pixel coordinates):
  - No jitter: pass `launchID` (samples the pixel center).
  - Subpixel jitter: pass `launchID + subpixelJitter`, with
    `subpixelJitter` in `[-0.5, 0.5)` so the sample stays inside the pixel.
-------------------------------------------------------------------------------------------------*/
RayDesc generatePrimaryRay(float2 pixelPos, float2 imageSize, GltfSceneInfo sceneInfo)
{
  // Add 0.5 so that integer pixelPos sample the pixel center, not the corner.
  float2 clipCoords = (pixelPos + 0.5) / imageSize * 2.0 - 1.0;
  float4 viewCoords = mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);

  RayDesc ray;
  ray.Origin    = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;
  ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;
  ray.TMin      = 0.001;
  ray.TMax      = INFINITE;
  return ray;
}

//-----------------------------------------------------------------------
// RAY ORIGIN OFFSET - Avoid self-intersection at bounce points
//-----------------------------------------------------------------------
// Standard ULP-based offset (Wächter & Binder, Ray Tracing Gems 2019).
// Offset along the geometric normal side the next ray travels on.
float3 safeOffsetRay(float3 worldPosition, float3 offsetDirection)
{
  const float scaleValue = 256.0f;
  const int3  scaleInt   = int3(scaleValue * offsetDirection);

  const float3 offsetPosition = float3(asfloat(asint(worldPosition.x) + ((worldPosition.x < 0) ? -scaleInt.x : scaleInt.x)),
                                       asfloat(asint(worldPosition.y) + ((worldPosition.y < 0) ? -scaleInt.y : scaleInt.y)),
                                       asfloat(asint(worldPosition.z) + ((worldPosition.z < 0) ? -scaleInt.z : scaleInt.z)));

  const float origin     = 1.0f / 32.0f;
  const float floatScale = 1.0f / 65536.0f;
  return float3(abs(worldPosition.x) < origin ? worldPosition.x + floatScale * offsetDirection.x : offsetPosition.x,
                abs(worldPosition.y) < origin ? worldPosition.y + floatScale * offsetDirection.y : offsetPosition.y,
                abs(worldPosition.z) < origin ? worldPosition.z + floatScale * offsetDirection.z : offsetPosition.z);
}

#endif  // TUTORIAL_RAY_COMMON_H
