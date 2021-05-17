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
 
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "wavefront.glsl"

// clang-format off
layout(binding = 2, set = 0, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
// clang-format on

layout(binding = 0) uniform UniformBufferObject
{
  mat4 view;
  mat4 proj;
  mat4 viewI;
}
ubo;

layout(push_constant) uniform shaderInformation
{
  vec3  lightPosition;
  float lightIntensity;
  vec3  lightDirection;
  float lightSpotCutoff;
  float lightSpotOuterCutoff;
  uint  instanceId;
  int   lightType;
}
pushC;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;


//layout(location = 0) flat out int matIndex;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 viewDir;
layout(location = 4) out vec3 worldPos;

out gl_PerVertex
{
  vec4 gl_Position;
};


void main()
{
  mat4 objMatrix   = scnDesc.i[pushC.instanceId].transfo;
  mat4 objMatrixIT = scnDesc.i[pushC.instanceId].transfoIT;

  vec3 origin = vec3(ubo.viewI * vec4(0, 0, 0, 1));

  worldPos     = vec3(objMatrix * vec4(inPosition, 1.0));
  viewDir      = vec3(worldPos - origin);
  fragTexCoord = inTexCoord;
  fragNormal   = vec3(objMatrixIT * vec4(inNormal, 0.0));
  //  matIndex     = inMatID;

  gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
