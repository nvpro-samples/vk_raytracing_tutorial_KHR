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

#include <nvshaders/slang_types.h>
#include <nvshaders/normal_compress.h.slang>

#include "common/shaders/pbr.h.slang"
#include "shaderio.h"

// clang-format off
[[vk::push_constant]]                       ConstantBuffer<TutoPushConstant> pushConst;
[[vk::binding(BindingPoints::eTextures)]]   Sampler2D textures[];
// clang-format on

// Per-vertex attributes to be assembled from bound vertex buffers.
struct VSin
{
  float3 position : POSITION;
  float3 normal : NORMAl;
};

// Output of the vertex shader
struct VSout
{
  float4 sv_position : SV_Position;
  float3 worldPos : POSITION;
  float3 worldNormal : NORMAL;
  float2 worldTexCoord : TEXCOORD0;
};

// Output of the fragment shader
struct PSout
{
    float4 color : SV_Target0;
    float4 hit : SV_Target1;
};

__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint attributeIndex)
{
  if(bufferView.count > 0)
  {
    T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
    return ptr[0];
  }

  return T(1);  // Error case
}


// Vertex  Shader
[shader("vertex")]
VSout vertexMain(uint vertexIndex: SV_VertexID)
{
  int instanceIndex = pushConst.instanceIndex;

  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  GltfInstance          instance = sceneInfo.instances[instanceIndex];
  GltfMesh              meshIo   = sceneInfo.meshes[instance.meshIndex];
  GltfMetallicRoughness material = sceneInfo.materials[instance.materialIndex];


  // Retrieve the data
  float3 posMesh  = getAttribute<float3>(meshIo.gltfBuffer, meshIo.triMesh.positions, vertexIndex);
  float3 normal   = getAttribute<float3>(meshIo.gltfBuffer, meshIo.triMesh.normals, vertexIndex);
  float2 texCoord = getAttribute<float2>(meshIo.gltfBuffer, meshIo.triMesh.texCoords, vertexIndex);

  float4 pos = mul(float4(posMesh, 1.0), instance.transform);

  VSout output;
  output.sv_position   = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos      = pos.xyz;
  output.worldNormal   = normalize(mul(normal, float3x3(pushConst.normalMatrix)));
  output.worldTexCoord = texCoord;

  return output;
}

// Fragment Shader
[shader("pixel")]
PSout fragmentMain(VSout stage)
{
  GltfSceneInfo         sceneInfo = pushConst.sceneInfoAddress[0];
  GltfInstance          instance  = sceneInfo.instances[pushConst.instanceIndex];
  GltfMetallicRoughness material  = sceneInfo.materials[instance.materialIndex];

  GltfPunctual light = sceneInfo.punctualLights[0];  // Assuming we only use the first light for simplicity

  if(sceneInfo.useSky == 1)
  {
    light.direction = sceneInfo.skySimpleParam.sunDirection;
    light.color     = sceneInfo.skySimpleParam.sunColor;
    light.intensity = sceneInfo.skySimpleParam.sunIntensity;
    light.type      = GltfLightType::eDirectional;
  }

  if(light.type == GltfLightType::ePoint)
  {
    light.direction = light.position - stage.worldPos;
    float d         = length(light.direction);
    light.intensity /= (d * d);
  }
  else if(light.type == GltfLightType::eSpot)
  {
    // For spot lights, we need to calculate the direction and apply attenuation
    float3 lightDir = light.position - stage.worldPos;
    float  d        = length(lightDir);
    light.intensity /= (d * d);
    // Cone attenuation can be applied here if needed
    float theta         = (dot(normalize(lightDir), normalize(light.direction)));
    float spotIntensity = clamp((theta - cos(light.coneAngle)) / (1.0 - cos(light.coneAngle)), 0.0, 1.0);
    light.intensity *= spotIntensity;
    light.direction = lightDir;
  }

  float3 V = normalize(sceneInfo.cameraPosition - stage.worldPos);
  float3 N = normalize(stage.worldNormal);
  float3 L = normalize(light.direction);

  // Get base color from material or texture
  float3 albedo = material.baseColorFactor.xyz;
  if(material.baseColorTextureIndex > -1)
  {
    albedo *= textures[material.baseColorTextureIndex].Sample(stage.worldTexCoord).xyz;
  }

  // Get metallic and roughness from material
  float metallic  = material.metallicFactor;
  float roughness = material.roughnessFactor;
  if(pushConst.metallicRoughnessOverride.x >= 0.0)
    metallic = pushConst.metallicRoughnessOverride.x;
  if(pushConst.metallicRoughnessOverride.y >= 0.0)
    roughness = pushConst.metallicRoughnessOverride.y;

  // Calculate PBR lighting with sun's color and intensity
  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
  // Apply light color and intensity
  color *= light.color * light.intensity;

  // Apply ambient
  float3 ambient = sceneInfo.backgroundColor;
  if(sceneInfo.useSky == 1)
  {
    // Add ambient lighting (sky effect)
    float3 skyUpDir    = float3(0, 1, 0);
    float3 groundColor = sceneInfo.skySimpleParam.groundColor;
    float3 skyColor    = sceneInfo.skySimpleParam.skyColor;
    float3 ambient     = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
  }
  // Add ambient to final color
  color += ambient * albedo * 0.025;

  PSout output;
  output.color        = float4(clamp(color, float3(0.0), float3(1.0)), 1.0);
  
  // Store world position and compressed normal in the w component
  uint  packedNormal  = compressUnitVec(N);
  output.hit = float4(stage.worldPos, asfloat(packedNormal));
  return output;
}
