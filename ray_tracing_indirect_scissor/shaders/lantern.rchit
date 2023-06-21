/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"

// Closest hit shader invoked when a primary ray hits a lantern.

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload prd;

layout(binding = 2, set = 0) buffer LanternArray { LanternIndirectEntry lanterns[]; } lanterns;

// clang-format on

void main()
{
  // Just look up this lantern's color. Self-illuminating, so no lighting calculations.
  LanternIndirectEntry lantern = lanterns.lanterns[nonuniformEXT(gl_InstanceCustomIndexEXT)];
  prd.hitValue                 = vec3(lantern.red, lantern.green, lantern.blue);
  prd.additiveBlending         = false;
}
