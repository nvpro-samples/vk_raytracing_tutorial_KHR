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

#include "shaderio.h"


[[vk::push_constant]]
VertexAnimationPushConstant pushConst;


// Helper function to get vertex attribute from gltfBuffer using BufferView
__generic<T : IFloat> T* getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint attributeIndex)
{
  T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
  return ptr;
}

// Calculate new wave position for sphere deformation
float3 calculateWavePosition(float3 originalPos, float time, float amplitude, float frequency, float speed)
{

  float radius = sqrt(1 - (originalPos.y * originalPos.y));

  // Calculate wave based on Y position (waves travel upward)
  float wave = sin(originalPos.y * frequency - time * speed) * amplitude * radius;

  // Create radial displacement - push vertices outward from sphere center
  float2 direction = normalize(originalPos.xz);

  float2 newXZ            = direction * radius;
  float2 wavePerturbation = direction * abs(wave);

  // Apply wave displacement in the radial direction
  float3 newPos = float3(newXZ.x, originalPos.y, newXZ.y) + float3(wavePerturbation.x, 0, wavePerturbation.y);  // + wave;

  return newPos;
}

// Normal calculation for radial wave displacement
float3 calculateAnimatedNormal(float3 originalPos, float3 originalNormal, float time, float amplitude, float frequency, float speed)
{
  float radius = sqrt(1 - (originalPos.y * originalPos.y));

  // Calculate wave derivative (how the wave changes in Y direction)
  float waveDerivative = cos(originalPos.y * frequency - time * speed) * frequency * amplitude * radius;

  // Since we use abs(wave) in position, we need to handle the direction properly
  float wave     = sin(originalPos.y * frequency - time * speed) * amplitude * radius;
  float waveSign = sign(wave);  // Get the sign to determine normal direction

  // XZ direction for radial displacement
  float2 xzDirection = normalize(originalPos.xz);

  // Avoid division by zero at poles
  if(length(originalPos.xz) < 0.001)
    return originalNormal;

  // Create perturbation in XZ plane based on wave gradient
  float3 perturbation = float3(xzDirection.x, 0, xzDirection.y) * abs(waveDerivative) * waveSign * 0.2;

  // Combine with original normal and normalize
  float3 animatedNormal = normalize(originalNormal + perturbation);

  return animatedNormal;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId: SV_DispatchThreadID)
{
  uint vertexIndex = threadId.x;

  // Get the mesh from push constant
  GltfMesh mesh = pushConst.meshBuffer[0];

  // Check if we're within the vertex count
  if(vertexIndex >= mesh.triMesh.positions.count)
    return;

  // Get original position and normal from gltfBuffer using BufferView
  float3* posPtr    = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.positions, vertexIndex);
  float3* normalPtr = getAttribute<float3>(mesh.gltfBuffer, mesh.triMesh.normals, vertexIndex);

  // Animation parameters for XZ plane wave
  float amplitude = 0.15f;  // Wave amplitude (increased for more visible effect)
  float frequency = 8.1f;   // Wave frequency (reduced for broader waves)
  float speed     = 2.0f;   // Wave speed (slightly slower for smoother animation)

  // Calculate new wave-deformed position
  float3 animatedPos = calculateWavePosition(*posPtr, pushConst.time, amplitude, frequency, speed);

  // Calculate animated normal
  float3 animatedNormal = calculateAnimatedNormal(*posPtr, normalPtr[0], pushConst.time, amplitude, frequency, speed);

  // Write animated vertex data directly to gltfBuffer
  posPtr[0] = animatedPos;

  // Write animated normal
  normalPtr[0] = animatedNormal;
}
