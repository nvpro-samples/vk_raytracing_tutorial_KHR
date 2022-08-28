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

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require


#include "gltf.glsl"
#include "raycommon.glsl"
#include "sampling.glsl"
#include "host_device.h"

hitAttributeEXT vec2 attribs;

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(set = 0, binding = 0 ) uniform accelerationStructureEXT topLevelAS;
layout(std430, set = 0, binding = 2) readonly buffer PhotonBeams{

    uint subBeamCount;
    uint beamCount;
    uint _padding_beams[2];
	PhotonBeam beams[];
};


layout(buffer_reference, scalar) readonly buffer Vertices  { vec3  v[]; };
layout(buffer_reference, scalar) readonly buffer Indices   { ivec3 i[]; };
layout(buffer_reference, scalar) readonly buffer Normals   { vec3  n[]; };
layout(buffer_reference, scalar) readonly buffer TexCoords { vec2  t[]; };
layout(buffer_reference, scalar) readonly buffer Materials { GltfShadeMaterial m[]; };

layout(set = 1, binding = eSceneDesc ) readonly buffer SceneDesc_ { SceneDesc sceneDesc; };
layout(set = 1, binding = eTextures) uniform sampler2D texturesMap[]; // all textures

layout(push_constant) uniform _PushConstantRay { PushConstantRay pcRay; };
// clang-format on


void main()
{

  PhotonBeam beam = beams[gl_InstanceCustomIndexEXT];
  Vertices  vertices  = Vertices(sceneDesc.beamBoxVertexAddress);
  Indices   indices   = Indices(sceneDesc.beamBoxIndexAddress);

  ivec3 triangleIndex = indices.i[gl_PrimitiveID];

  const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  const vec3 pos0           = vertices.v[triangleIndex.x];
  const vec3 pos1           = vertices.v[triangleIndex.y];
  const vec3 pos2           = vertices.v[triangleIndex.z];
  const vec3 position       = pos0 * barycentrics.x + pos1 * barycentrics.y + pos2 * barycentrics.z;
  const vec3 world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

  prd.hitValue = beam.lightColor;
}