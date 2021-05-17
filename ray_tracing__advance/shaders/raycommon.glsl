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
  vec3 hitValue;
  uint seed;
  int  depth;
  vec3 attenuation;
  int  done;
  vec3 rayOrigin;
  vec3 rayDir;
};


struct rayLight
{
  vec3  inHitPosition;
  float outLightDistance;
  vec3  outLightDir;
  float outIntensity;
};

struct Implicit
{
  vec3 minimum;
  vec3 maximum;
  int  objType;
  int  matId;
};

struct Sphere
{
  vec3  center;
  float radius;
};

struct Aabb
{
  vec3 minimum;
  vec3 maximum;
};

#define KIND_SPHERE 0
#define KIND_CUBE 1
