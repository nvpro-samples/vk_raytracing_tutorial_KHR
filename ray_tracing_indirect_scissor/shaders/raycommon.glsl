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

#include "LanternIndirectEntry.glsl"

struct hitPayload
{
  vec3 hitValue;
  bool additiveBlending;
};

layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;          // 0: point, 1: infinite
  int   lanternPassNumber;  // -1 if this is the full-screen pass. Otherwise, used to lookup trace indirect parameters.
  int   screenX;
  int   screenY;
  int   lanternDebug;
}
pushC;