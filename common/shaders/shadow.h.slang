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

// Shadow-ray helper, shared by tutorial samples 03+.
//
// `testShadow` re-uses the sample's existing miss shader (no extra SBT entry)
// by sending a small payload that lets the miss shader signal "no occluder".
// The payload layout differs across samples (some carry color/weight, others
// add a RayCone, etc.), so the helper is generic over a `TPayload : IShadowPayload`
// interface — each sample's `HitPayload` implements two trivial members.
//
// Requires the caller to include `shaderio.h` (for `GltfPunctual`,
// `GltfLightType`) and `nvshaders/constants.h.slang` (for `INFINITE`).

#ifndef TUTORIAL_SHADOW_H
#define TUTORIAL_SHADOW_H

// Contract that the sample's ray payload must satisfy to be usable by `testShadow`.
//   - Default construction builds the payload state expected by the miss shader.
//   - `isMissed()` returns true iff the miss shader actually ran (no occluder hit).
interface IShadowPayload
{
  bool isMissed();
};

// Traces a shadow ray from `worldPos` toward `lightDirection`.
// Uses RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH + RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
// so any-hit (for transparency) can run but closest-hit is skipped.
//
//   missShaderIndex   - which miss shader receives the payload (default 0 reuses
//                       the main miss shader; sample 05 uses a dedicated
//                       shadow-miss shader at index 1).
//   surfaceOffset     - distance along `worldNormal` used to bias the ray origin
//                       away from the surface (default 0.001; sample 18 uses 0.01
//                       to avoid self-intersection on swept-sphere boundaries).
// Returns 1.0 if unoccluded, 0.0 if occluded.
__generic<TPayload : IShadowPayload>
float testShadow(RaytracingAccelerationStructure tlas,
                 float3                          worldPos,
                 float3                          worldNormal,
                 float3                          lightDirection,
                 GltfPunctual                    light,
                 uint                            missShaderIndex = 0,
                 float                           surfaceOffset   = 0.001)
{
  RayDesc shadowRay;
  shadowRay.Origin    = worldPos + worldNormal * surfaceOffset;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin      = 0.001;
  shadowRay.TMax      = light.type == GltfLightType::eDirectional ? INFINITE : length(lightDirection);

  TPayload shadowPayload = TPayload();

  TraceRay(tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0,
           missShaderIndex, shadowRay, shadowPayload);

  return shadowPayload.isMissed() ? 1.0 : 0.0;
}

#endif  // TUTORIAL_SHADOW_H
