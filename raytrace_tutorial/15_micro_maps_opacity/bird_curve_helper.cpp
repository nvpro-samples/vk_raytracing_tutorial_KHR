
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

#include <glm/glm.hpp>

#include "bird_curve_helper.hpp"
#include <unordered_map>
#include <memory>


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//---- Hash Combination ----
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3876.pdf
template <typename T>
void hashCombine(std::size_t& seed, const T& val)
{
  seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}
// Auxiliary generic functions to create a hash value using a seed
template <typename T, typename... Types>
void hashCombine(std::size_t& seed, const T& val, const Types&... args)
{
  hashCombine(seed, val);
  hashCombine(seed, args...);
}
// Optional auxiliary generic functions to support hash_val() without arguments
void hashCombine(std::size_t& seed) {}
// Generic function to create a hash value out of a heterogeneous list of arguments
template <typename... Types>
std::size_t hashVal(const Types&... args)
{
  std::size_t seed = 0;
  hashCombine(seed, args...);
  return seed;
}

//---------------------------------------------------------
// Hash key for a glm::vec3
// Used to find an index from a barycentric coordinate
std::size_t makeHash(const glm::vec3& v)
{
  return hashVal(v.x, v.y, v.z);
}
auto hash  = [](const glm::vec3& v) { return makeHash(v); };
auto equal = [](const glm::vec3& l, const glm::vec3& r) { return l == r; };


static std::unordered_map<glm::vec3, uint32_t, decltype(hash), decltype(equal)> buildMap(const BirdCurveHelper::BaryCoordinates& bary_coords)
{
  // Create a map of all level bary coordinates, so we can find later the index from a bary coordinate
  std::unordered_map<glm::vec3, uint32_t, decltype(hash), decltype(equal)> bary_to_idx(0, hash, equal);
  for(uint32_t idx = 0; idx < static_cast<uint32_t>(bary_coords.size()); idx++)
  {
    bary_to_idx[bary_coords[idx]] = idx;
  }
  return bary_to_idx;
}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


BirdCurveHelper::BirdCurveHelper(uint32_t         maxLevel /*= 5*/,
                                 const glm::vec3& w /*={1, 0, 0}*/,
                                 const glm::vec3& u /*={0, 1, 0}*/,
                                 const glm::vec3& v /*={0, 0, 1}*/)
    : m_maxLevel(maxLevel)
{
  init(w, u, v);
}


void BirdCurveHelper::init(const glm::vec3& w, const glm::vec3& u, const glm::vec3& v)
{
  m_birdValues = {};

  // Resize/reserve the micro-vertices values
  m_birdValues.resize(m_maxLevel + 1ULL);
  m_birdIndices.resize(m_maxLevel + 1ULL);
  m_triBary.resize(m_maxLevel + 1ULL);

  for(uint32_t level = 0; level <= m_maxLevel; level++)
  {
    uint32_t num_microvertex = getNumMicroVertices(level);
    m_birdValues[level].reserve(num_microvertex);

    uint32_t num_triangles = getNumMicroTriangles(level);
    m_birdIndices[level].reserve(num_triangles);
  }

  // Setting up level 0
  m_birdValues[0].push_back(w);
  m_birdValues[0].push_back(u);
  m_birdValues[0].push_back(v);

  // Recursively splitting the triangle and collects the micro-vertices barycentric values
  birdLevel(1, true, true, w, u, v);

  // Assembling the coordinates
  // Final Level1 == level0 + level1
  // Final Level2 == level0 + level1 + level2
  // ..
  for(uint32_t level = 1; level <= m_maxLevel; level++)
  {
    m_birdValues[level].insert(m_birdValues[level].begin(), m_birdValues[level - 1ULL].begin(),
                               m_birdValues[level - 1ULL].end());
  }

  // Finding the indices creating each triangles in the order of the Bird Curve
  for(uint32_t level = 0; level <= m_maxLevel; level++)
  {
    // Create a map to find the index corresponding to the WUV coordinates
    auto bary_to_idx = buildMap(m_birdValues[level]);

    for(size_t t = 0; t < m_triBary[level].size(); t++)
    {
      glm::ivec3 tri_index;
      tri_index.x = bary_to_idx[m_triBary[level][t].w];
      tri_index.y = bary_to_idx[m_triBary[level][t].u];
      tri_index.z = bary_to_idx[m_triBary[level][t].v];
      m_birdIndices[level].push_back(tri_index);
    }
  }
}


//--------------------------------------------------------------------------------------------------
// The Bird Curve (is called the bird curve because of it's             **************************
// resemblance to an Escher-like repeated bird pattern)                 *            V           *
// We enter at W, then go to 1 and 2, and finally exit at V.            *           /\           *
// To maintain that the hierarchical ordering is contiguous,            *          /  \          *
// some triangles are flipped and wound differently.                    *         / 3  \         *
// The 0 triangle is constructed with the w, uw, and vw vertices.       *        /      \        *
// The middle triangle then starts at vw, goes to uv, and then uw.      *    VW /________\ UV    *
// To avoid duplicated vertices, only triangle pointing up will         *      /\        /\      *
// store vertices.                                                      *     /  \  1   /  \     *
//                                                                      *    /    \    /    \    *
//                                                                      *   /  0   \  /  2   \   *
//                                                                      *  /        \/        \  *
//                                                                      * W ________UW________ U *
//                                                                      **************************
//
void BirdCurveHelper::birdLevel(int level, bool triPointUp, bool counterClockwise, const glm::vec3& w, const glm::vec3& u, const glm::vec3& v)
{
  m_triBary[level - 1ULL].push_back({w, u, v});  // Adding triangle barycentric coordinates

  if(level >= static_cast<int>(m_birdValues.size()))
    return;

  // Finding the mid-points of the triangle
  glm::vec3 vw = (v + w) * 0.5F;
  glm::vec3 uv = (u + v) * 0.5F;
  glm::vec3 uw = (w + u) * 0.5F;

  if(triPointUp)  // Store mid-points only triangles pointing up
  {
    if(counterClockwise)  // Sub-triangle 0 - 2
    {
      m_birdValues[level].push_back(vw);
      m_birdValues[level].push_back(uv);
      m_birdValues[level].push_back(uw);
    }
    else  // Sub-triangle 1 - 3
    {
      m_birdValues[level].push_back(uv);
      m_birdValues[level].push_back(vw);
      m_birdValues[level].push_back(uw);
    }
  }

  // Recursively call for each sub-triangles
  birdLevel(level + 1, triPointUp, counterClockwise, w, uw, vw);
  birdLevel(level + 1, !triPointUp, !counterClockwise, vw, uv, uw);
  birdLevel(level + 1, triPointUp, counterClockwise, uw, u, uv);
  birdLevel(level + 1, triPointUp, !counterClockwise, uv, vw, v);
}


//--------------------------------------------------------------------------------------------------
// Displacement block are used for uncompress data using VK_DISPLACEMENT_MICROMAP_FORMAT_64_TRIANGLES_64_BYTES_NV
//
BirdCurveHelper::DisplacementBlocks BirdCurveHelper::createDisplacementBlocks(uint32_t level)
{
  DisplacementBlocks displacement_blocks;

  // Level-0..3 are using only one block, the indices are linear
  // and follow the BirdCurve since there is less or equal to 64 triangles
  if(level <= 3)
  {
    displacement_blocks.resize(1);
    auto& block = displacement_blocks[0];
    block.reserve(m_birdValues[level].size());
    for(size_t idx = 0; idx < m_birdValues[level].size(); idx++)
    {
      block.push_back(static_cast<uint32_t>(idx));
    }
    return displacement_blocks;
  }

  // Starting bary coordinates
  glm::vec3 w{1, 0, 0};
  glm::vec3 u{0, 1, 0};
  glm::vec3 v{0, 0, 1};

  // Create a map of all level bary coordinates, so we can find later the index from a bary coordinate
  auto bary_to_idx = buildMap(m_birdValues[level]);

  // Level-4 uses 4 displacement blocks, each having 64 triangles
  // The order of the indices in each block aren't linear, since
  // it is taken only a part of the coordinates.
  displacement_blocks.resize(4);  // level 4 has 4 blocks

  {
    // All sub-triangles for level 4
    std::vector<SubTriangle> sub_triangles = splitTriangles(w, u, v);

    if(level == 5)
    {
      displacement_blocks.resize(16);  // level 5 has 16 blocks

      // Splitting in 4, the 4 sub-triangles of level 4
      auto t0 = splitTriangles(sub_triangles[0].w, sub_triangles[0].u, sub_triangles[0].v);
      auto t1 = splitTriangles(sub_triangles[1].w, sub_triangles[1].u, sub_triangles[1].v);
      auto t2 = splitTriangles(sub_triangles[2].w, sub_triangles[2].u, sub_triangles[2].v);
      auto t3 = splitTriangles(sub_triangles[3].w, sub_triangles[3].u, sub_triangles[3].v);

      sub_triangles = {};
      sub_triangles.insert(sub_triangles.end(), t0.begin(), t0.end());
      sub_triangles.insert(sub_triangles.end(), t1.begin(), t1.end());
      sub_triangles.insert(sub_triangles.end(), t2.begin(), t2.end());
      sub_triangles.insert(sub_triangles.end(), t3.begin(), t3.end());
    }

    // Help constructing the coordinated for the sub-triangle
    std::unique_ptr<BirdCurveHelper> bary_sub_triangle;

    // By creating a level-3 of the sub-triangle, we get the bary coordinates
    // that is forming it, then we get the index of each coordinate to form
    // the block needed for the uncompressed format.
    // This will fill all four blocks using the triangles described above
    for(int sub_tri_idx = 0; sub_tri_idx < static_cast<int>(sub_triangles.size()); sub_tri_idx++)
    {
      auto& sub_tri = sub_triangles[sub_tri_idx];
      auto& block   = displacement_blocks[sub_tri_idx];

      // Create the bary coordinates, using the sub-triangle
      bary_sub_triangle = std::make_unique<BirdCurveHelper>(3, sub_tri.w, sub_tri.u, sub_tri.v);

      // From the bary-coordinates, find and add the index to the block
      for(const auto& c : bary_sub_triangle->getVertexCoord(3))
      {
        block.push_back(bary_to_idx[c]);
      }
    }
  }

  return displacement_blocks;
}

//--------------------------------------------------------------------------------------------------
// Split the triangle in four, used by createDisplacementBlocks()
//
std::vector<BirdCurveHelper::BirdCurveHelper::SubTriangle> BirdCurveHelper::splitTriangles(const glm::vec3& w,
                                                                                           const glm::vec3& u,
                                                                                           const glm::vec3& v)
{
  glm::vec3 vw = (v + w) * 0.5F;
  glm::vec3 uv = (u + v) * 0.5F;
  glm::vec3 uw = (w + u) * 0.5F;

  std::vector<SubTriangle> triangles = {{w, uw, vw}, {vw, uv, uw}, {uw, u, uv}, {uv, vw, v}};

  return triangles;
}

const std::vector<glm::vec3>& BirdCurveHelper::getVertexCoord(uint16_t level)
{
  return m_birdValues[level];
}

const BirdCurveHelper::BaryIndices& BirdCurveHelper::getTriangleIndices(uint16_t level)
{
  return m_birdIndices[level];
}

const std::vector<BirdCurveHelper::SubTriangle>& BirdCurveHelper::getTriangleCoord(uint16_t level)
{
  return m_triBary[level];
}


static inline float __uint_as_float(uint32_t x)
{
  union
  {
    float    f;
    uint32_t i;
  } var{};
  var.i = x;
  return var.f;
}

// Extract even bits
static inline uint32_t extractEvenBits(uint32_t x)
{
  x &= 0x55555555;
  x = (x | (x >> 1)) & 0x33333333;
  x = (x | (x >> 2)) & 0x0f0f0f0f;
  x = (x | (x >> 4)) & 0x00ff00ff;
  x = (x | (x >> 8)) & 0x0000ffff;
  return x;
}


// Calculate exclusive prefix or (log(n) XOR's and SHF's)
static inline uint32_t prefixEor(uint32_t x)
{
  x ^= x >> 1;
  x ^= x >> 2;
  x ^= x >> 4;
  x ^= x >> 8;
  return x;
}


// Convert distance along the curve to discrete barycentrics
static inline void index2dbary(uint32_t index, uint32_t& u, uint32_t& v, uint32_t& w)
{
  uint32_t b0 = extractEvenBits(index);
  uint32_t b1 = extractEvenBits(index >> 1);

  uint32_t fx = prefixEor(b0);
  uint32_t fy = prefixEor(b0 & ~b1);

  uint32_t t = fy ^ b1;

  u = (fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t);
  v = fy ^ b0;
  w = (~fx & ~t) | (b0 & ~t) | (~b0 & fx & t);
}


//--------------------------------------------------------------------------------------------------
// This returns the 3 barycentric coordinates of a micro-triangle.
//
void BirdCurveHelper::micro2bary(uint32_t index, uint32_t subdivisionLevel, glm::vec3& uv0, glm::vec3& uv1, glm::vec3& uv2)
{
  if(subdivisionLevel == 0)
  {
    uv0 = {1, 0, 0};
    uv1 = {0, 1, 0};
    uv2 = {0, 0, 1};
    return;
  }

  uint32_t iu, iv, iw;
  index2dbary(index, iu, iv, iw);

  // we need to only look at "level" bits
  iu = iu & ((1 << subdivisionLevel) - 1);
  iv = iv & ((1 << subdivisionLevel) - 1);
  iw = iw & ((1 << subdivisionLevel) - 1);

  bool upright = (iu & 1) ^ (iv & 1) ^ (iw & 1);
  if(!upright)
  {
    iu = iu + 1;
    iv = iv + 1;
  }

  const float levelScale = __uint_as_float((127u - subdivisionLevel) << 23);

  // scale the barycentic coordinate to the global space/scale
  float du = 1.f * levelScale;
  float dv = 1.f * levelScale;

  // scale the barycentic coordinate to the global space/scale
  float u = (float)iu * levelScale;
  float v = (float)iv * levelScale;

  if(!upright)
  {
    du = -du;
    dv = -dv;
  }

  uv0 = {1 - u - v, u, v};
  uv1 = {1 - (u + du) - v, u + du, v};
  uv2 = {1 - u - (v + dv), u, v + dv};
}
