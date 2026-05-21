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

#pragma once


//-----------------------------------------------------------------------------
// Tutorial-local copy of the helpers in nvpro_core2/nvshaders/wireframe.h.slang.
//
// The RT-technique functions at the bottom of this file
//   - computeDifferentials   (per-pixel ray differentials in RT)
//   - computeDerivative      (the RT analogue of fwidth(bary))
//   - intersectRayTriangle   (supporting math, float-only for readability)
// are the actual subject of this sample. They reconstruct the screen-space
// barycentric derivatives that a rasterizer would have had for free.
//
// The wireframe *styling* functions above them (smoothstep edge masking,
// stipple, thickness modulation) are the same as the upstream library; they
// are inlined here only so the sample is self-contained. The upstream version
// in `nvpro_core2/nvshaders/wireframe.h.slang` is the source of truth.
//-----------------------------------------------------------------------------


// Configuration for wireframe rendering.
struct WireframeSettings
{
  float  thickness;       // Base thickness of the wireframe lines (multiplied by 0.5 internally)
  float2 thicknessVar;    // Thickness variation along edges: x = min, y = max
  float  smoothing;       // Anti-aliasing smoothing factor (multiplied by thickness)
  bool   stipple;         // Enable stipple/dash pattern on wireframe lines
  float  stippleLength;   // Length of dashed segments in stipple pattern [0..1]
  float  stippleRepeats;  // Number of stipple pattern repeats along the edge
};


//-----------------------------------------------------------------------------
// Wireframe styling - turns (barycentrics, deltas) into a [0..1] coverage.
// These are the same formulas as in nvshaders/wireframe.h.slang.
//-----------------------------------------------------------------------------

// Position [0..1] along the closest triangle edge for the current hit.
float edgePosition(float3 baryWeights)
{
  return max(baryWeights.z, max(baryWeights.y, baryWeights.x));
}

// Thickness modulation along an edge using a half-period sine.
float edgeThickness(float2 thicknessVar, float edgePos)
{
  return lerp(thicknessVar.x, thicknessVar.y, (1.0 - sin(edgePos * M_PI)));
}

// Stipple / dash pattern mask: returns 0 (gap) or 1 (dash) at edgePos.
float stipple(float stippleRepeats, float stippleLength, float edgePos)
{
  float offset  = (1.0 / stippleRepeats) * 0.5 * stippleLength;
  float pattern = frac((edgePos + offset) * stippleRepeats);
  return 1.0 - step(stippleLength, pattern);
}

// Smoothstep against the barycentric deltas to get an anti-aliased line width.
float getLineWidth(float3 deltas, float thickness, float smoothing, float3 barys)
{
  barys         = smoothstep(deltas * thickness, deltas * (thickness + smoothing), barys);
  float minBary = min(barys.x, min(barys.y, barys.z));
  return 1.0 - minBary;
}

// Main entry point: returns wireframe contribution in [0..1].
//   - barys:  hit-point barycentric coords (w, u, v)
//   - deltas: barycentric derivatives (computeDerivative in RT, fwidth(barys) in raster).
float processWireframe(WireframeSettings settings, float3 barys, float3 deltas)
{
  float thickness = settings.thickness * 0.5;  // half-width on each side of the edge
  float smoothing = settings.thickness * settings.smoothing;

  float edgePos = edgePosition(barys);
  thickness *= edgeThickness(settings.thicknessVar, edgePos);

  float lineWidth = getLineWidth(deltas, thickness, smoothing, barys);

  if(settings.stipple)
  {
    float stippleFact = stipple(settings.stippleRepeats, settings.stippleLength, edgePos);
    lineWidth *= stippleFact;
  }

  return lineWidth;
}


//-----------------------------------------------------------------------------
// RT-technique core - the part this tutorial is really about.
//-----------------------------------------------------------------------------

// Float-only ray-triangle intersection. Returns barycentric coordinates
// (w, u, v) of the hit, where w + u + v == 1. Used by computeDerivative to
// re-intersect the same triangle with the differential rays.
float3 intersectRayTriangle(float3 origin, float3 direction, float3 v0, float3 v1, float3 v2)
{
  float3 e1          = v1 - v0;
  float3 e2          = v2 - v0;
  float3 planeNormal = cross(e1, e2);

  float  nDotDir = dot(planeNormal, direction);
  float  t       = dot(planeNormal, v0 - origin) / nDotDir;
  float3 p       = origin + t * direction;
  float3 temp    = p - v0;

  float det = dot(cross(e1, e2), planeNormal);
  float u   = dot(cross(temp, e2), planeNormal) / det;
  float v   = dot(cross(e1, temp), planeNormal) / det;
  float w   = 1.0 - u - v;

  return float3(w, u, v);
}

// Per-pixel ray differentials for a pinhole camera. Produces two world-space
// direction vectors that point from the camera toward the centres of the two
// neighbouring pixels along the screen X and Y axes. Together with the
// primary ray's origin, they define the ray-tracing equivalent of the
// rasterizer's 2x2 quad.
//
// In sample 19 the analogue of these two vectors lives in the payload as a
// RayCone {width, spreadAngle}. Here, because we only need them on primary-
// ray hits, we recompute them inside the closest hit instead - no payload
// growth.
void computeDifferentials(float2 pixelPos, float2 imageSize, float4x4 viewInverse, float4x4 projInverse, out float3 differentialX, out float3 differentialY)
{
  const float2 pixelCenter = pixelPos + float2(0.5);
  const float2 inUV        = pixelCenter / imageSize;
  const float2 d           = inUV * 2.0 - 1.0;

  // One pixel offset in screen X and screen Y (centres of the right/down neighbours).
  const float2 uvOffset = (pixelPos + float2(1.5, 1.5)) / imageSize;
  const float2 dOffset  = uvOffset * 2.0 - 1.0;

  // View-space positions for the centres of the neighbouring pixels.
  const float4 targetOffsetX = mul(float4(dOffset.x, d.y, 1, 1), projInverse);
  const float4 targetOffsetY = mul(float4(d.x, dOffset.y, 1, 1), projInverse);

  // Promote the unit view-space direction to world space. viewInverse is a
  // rigid (rotation+translation) transform applied to a direction (w=0), so
  // it preserves length - no outer normalize needed.
  differentialX = mul(float4(normalize(targetOffsetX.xyz), 0), viewInverse).xyz;
  differentialY = mul(float4(normalize(targetOffsetY.xyz), 0), viewInverse).xyz;
}

// Barycentric "fwidth" at the current hit - the ray-tracing analogue of
// fwidth(bary) in a rasterizer.
//
// How it works: take the primary-ray hit's barycentrics, then re-intersect
// the same triangle with each differential ray (the primary ray's origin
// shifted by offsetX / offsetY in world space). The absolute difference
// between the perturbed barycentrics and the original is, by definition,
// the rate of change of the barycentric coordinates with respect to one
// pixel of screen movement.
//
// The intersection runs entirely in object space (the triangle is already
// there), so the world-space ray origin and offsets are pushed through the
// worldToObject matrix on the way in.
float3 computeDerivative(in float4x3 worldToObject,
                         in float3   worldRayOrigin,
                         in float3   objectRayOrigin,
                         in float3   triangleVertexPositions[3],
                         in float3   offsetX,
                         in float3   offsetY,
                         in float3   bary)
{
  // Differential ray along screen X, re-intersected in object space.
  float3 derivTargetX = mul(float4(worldRayOrigin + offsetX, 1), worldToObject);
  float3 derivDirX    = derivTargetX - objectRayOrigin;
  float3 baryX        = intersectRayTriangle(objectRayOrigin, derivDirX, triangleVertexPositions[0],
                                             triangleVertexPositions[1], triangleVertexPositions[2]);
  baryX               = abs(baryX - bary);

  // Differential ray along screen Y.
  float3 derivTargetY = mul(float4(worldRayOrigin + offsetY, 1), worldToObject);
  float3 derivDirY    = derivTargetY - objectRayOrigin;
  float3 baryY        = intersectRayTriangle(objectRayOrigin, derivDirY, triangleVertexPositions[0],
                                             triangleVertexPositions[1], triangleVertexPositions[2]);
  baryY               = abs(baryY - bary);

  // Combined per-axis spread, matches the rasterizer's fwidth() semantics
  // (sum of |ddx| and |ddy|).
  return baryX + baryY;
}
