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


struct hitPayload
{

  vec3 rayOrigin;
  uint seed;
  vec3 rayDirection;
  int  instanceIndex;
  vec3 weight;
  uint padding2;
};

struct rayHitPayload
{
  vec3 hitValue;
  int instanceIndex;
  vec3 rayOrigin;
  uint padding;
  vec3 rayDirection;
  uint padding2;
  vec3 hitNormal;
  float hitRoughness;
  vec3 hitAlbedo;
  float hitMetallic;
  vec3  weight;
  uint padding3;
};
