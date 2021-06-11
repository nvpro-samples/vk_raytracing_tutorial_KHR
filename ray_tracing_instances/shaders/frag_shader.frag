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
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "wavefront.glsl"


layout(push_constant) uniform shaderInformation
{
  vec3  lightPosition;
  uint  instanceId;
  float lightIntensity;
  int   lightType;
}
pushC;

// clang-format off
// Incoming 
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 viewDir;
layout(location = 4) in vec3 worldPos;
// Outgoing
layout(location = 0) out vec4 outColor;

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {uint i[]; }; // Triangle indices
layout(buffer_reference, scalar) buffer Materials {WaveFrontMaterial m[]; }; // Array of all materials on an object
layout(buffer_reference, scalar) buffer MatIndices {int i[]; }; // Material ID for each triangle

layout(binding = 1, scalar) buffer SceneDesc_ { SceneDesc i[]; } sceneDesc;
layout(binding = 2) uniform sampler2D[] textureSamplers;
// clang-format on


void main()
{
  // Material of the object
  SceneDesc  objResource = sceneDesc.i[pushC.instanceId];
  MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
  Materials  materials   = Materials(objResource.materialAddress);

  int               matIndex = matIndices.i[gl_PrimitiveID];
  WaveFrontMaterial mat      = materials.m[matIndex];

  vec3 N = normalize(fragNormal);

  // Vector toward light
  vec3  L;
  float lightIntensity = pushC.lightIntensity;
  if(pushC.lightType == 0)
  {
    vec3  lDir     = pushC.lightPosition - worldPos;
    float d        = length(lDir);
    lightIntensity = pushC.lightIntensity / (d * d);
    L              = normalize(lDir);
  }
  else
  {
    L = normalize(pushC.lightPosition - vec3(0));
  }


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, L, N);
  if(mat.textureId >= 0)
  {
    int  txtOffset  = sceneDesc.i[pushC.instanceId].txtOffset;
    uint txtId      = txtOffset + mat.textureId;
    vec3 diffuseTxt = texture(textureSamplers[nonuniformEXT(txtId)], fragTexCoord).xyz;
    diffuse *= diffuseTxt;
  }

  // Specular
  vec3 specular = computeSpecular(mat, viewDir, L, N);

  // Result
  outColor = vec4(lightIntensity * (diffuse + specular), 1);
}
