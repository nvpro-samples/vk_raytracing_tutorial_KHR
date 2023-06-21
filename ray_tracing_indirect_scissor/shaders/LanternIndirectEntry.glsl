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

struct LanternIndirectEntry
{
  // VkTraceRaysIndirectCommandKHR
  int indirectWidth;
  int indirectHeight;
  int indirectDepth;

  // Pixel coordinate of scissor rect upper-left.
  int offsetX;
  int offsetY;

  // Lantern starts here:
  // Can't use vec3 due to alignment.
  float x, y, z;
  float red, green, blue;
  float brightness;
  float radius;
};
