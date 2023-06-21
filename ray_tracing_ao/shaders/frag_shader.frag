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

#include "raycommon.glsl"
#include "wavefront.glsl"


layout(push_constant) uniform _PushConstantRaster
{
  PushConstantRaster pcRaster;
};

// clang-format off
// Incoming 
layout(location = 1) in vec3 i_worldPos;
layout(location = 2) in vec3 i_worldNrm;
layout(location = 3) in vec3 i_viewDir;
layout(location = 4) in vec2 i_texCoord;
// Outgoing
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_gbuffer;

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {uint i[]; }; // Triangle indices
layout(buffer_reference, scalar) buffer Materials {WaveFrontMaterial m[]; }; // Array of all materials on an object
layout(buffer_reference, scalar) buffer MatIndices {int i[]; }; // Material ID for each triangle

layout(binding = eObjDescs, scalar) buffer ObjDesc_ { ObjDesc i[]; } objDesc;
layout(binding = eTextures) uniform sampler2D[] textureSamplers;
// clang-format on


void main()
{
  // Material of the object
  ObjDesc    objResource = objDesc.i[pcRaster.objIndex];
  MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
  Materials  materials   = Materials(objResource.materialAddress);

  int               matIndex = matIndices.i[gl_PrimitiveID];
  WaveFrontMaterial mat      = materials.m[matIndex];

  vec3 N = normalize(i_worldNrm);

  // Vector toward light
  vec3  L;
  float lightDistance;
  float lightIntensity = pcRaster.lightIntensity;
  if(pcRaster.lightType == 0)
  {
    vec3  lDir     = pcRaster.lightPosition - i_worldPos;
    float d        = length(lDir);
    lightIntensity = pcRaster.lightIntensity / (d * d);
    L              = normalize(lDir);
    lightDistance  = d;
  }
  else
  {
    L             = normalize(pcRaster.lightPosition - vec3(0));
    lightDistance = 10000;
  }


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, L, N);
  diffuse      = vec3(1);
  //  if(mat.textureId >= 0)
  //  {
  //    int  txtOffset  = objDesc.i[pcRaster.objIndex].txtOffset;
  //    uint txtId      = txtOffset + mat.textureId;
  //    vec3 diffuseTxt = texture(textureSamplers[nonuniformEXT(txtId)], fragTexCoord).xyz;
  //    diffuse *= diffuseTxt;
  //  }
  //lightIntensity = 1;
  // Specular
  vec3 specular  = vec3(0);  //computeSpecular(mat, viewDir, L, N);
  lightIntensity = 1;

  // Result
  o_color        = vec4(lightIntensity * (diffuse + specular), 1);
  o_gbuffer.rgba = vec4(i_worldPos, uintBitsToFloat(CompressUnitVec(N)));
}
