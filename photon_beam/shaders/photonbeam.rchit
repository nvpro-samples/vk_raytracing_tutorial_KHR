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

//#extension GL_EXT_debug_printf : require

#include "gltf.glsl"
#include "raycommon.glsl"
#include "sampling.glsl"
#include "host_device.h"

hitAttributeEXT vec2 attribs;

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(set = 0, binding = 0 ) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) readonly buffer _InstanceInfo {PrimMeshInfo primInfo[];};


layout(buffer_reference, scalar) readonly buffer Vertices  { vec3  v[]; };
layout(buffer_reference, scalar) readonly buffer Indices   { ivec3 i[]; };
layout(buffer_reference, scalar) readonly buffer Normals   { vec3  n[]; };
layout(buffer_reference, scalar) readonly buffer TexCoords { vec2  t[]; };
layout(std430, buffer_reference, scalar) readonly buffer Materials { GltfShadeMaterial m[]; };

layout(set = 1, binding = eSceneDesc ) readonly buffer SceneDesc_ { SceneDesc sceneDesc; };
layout(set = 1, binding = eTextures) uniform sampler2D texturesMap[]; // all textures

layout(push_constant) uniform _PushConstantRay { PushConstantRay pcRay; };
// clang-format on


bool randomScatterOccured(const vec3 world_position){
    float max_extinct = max(max(pcRay.airExtinctCoff.x, pcRay.airExtinctCoff.y), pcRay.airExtinctCoff.z);

    if (max_extinct <= 0.0)
        return false;

    float max_scatter = max(max(pcRay.airScatterCoff.x, pcRay.airScatterCoff.y), pcRay.airScatterCoff.z);


    // random walk within participating media(air) scattering
    float rayLength = length(prd.rayOrigin - world_position);
    float airScatterAt = -log(1.0 - rnd(prd.seed)) / length(pcRay.airExtinctCoff);

    if (rayLength < airScatterAt) {
        return false;
    }

    vec3 rayOrigin = prd.rayOrigin + prd.rayDirection * airScatterAt;
    vec3 rayDirection = heneyGreenPhaseFuncSampling(prd.seed, prd.rayDirection, pcRay.airHGAssymFactor);

    vec3 weight = exp(-pcRay.airExtinctCoff * airScatterAt);
    // use russian roulett to decide whether scatter or absortion occurs
    if (rnd(prd.seed) > max_scatter/max_extinct){
        prd.weight = vec3(0.0);
    }

    prd.rayOrigin    = rayOrigin;
    prd.rayDirection = rayDirection;
    prd.weight       = weight;
    prd.instanceIndex = -1;

    return true;
}



void main()
{
  // Retrieve the Primitive mesh buffer information
    PrimMeshInfo pinfo = primInfo[gl_InstanceCustomIndexEXT];
    prd.instanceIndex = gl_InstanceCustomIndexEXT;

    // Getting the 'first index' for this mesh (offset of the mesh + offset of the triangle)
    uint indexOffset  = (pinfo.indexOffset / 3) + gl_PrimitiveID;
    uint vertexOffset = pinfo.vertexOffset;           // Vertex offset as defined in glTF
    uint matIndex     = max(0, pinfo.materialIndex);  // material of primitive mesh

    Vertices  vertices  = Vertices(sceneDesc.vertexAddress);
    Indices   indices   = Indices(sceneDesc.indexAddress);
    Normals   normals   = Normals(sceneDesc.normalAddress);
    TexCoords texCoords = TexCoords(sceneDesc.uvAddress);
    Materials materials = Materials(sceneDesc.materialAddress);

    // Getting the 3 indices of the triangle (local)
    ivec3 triangleIndex = indices.i[indexOffset];
    triangleIndex += ivec3(vertexOffset);  // (global)

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    // Vertex of the triangle
    const vec3 pos0           = vertices.v[triangleIndex.x];
    const vec3 pos1           = vertices.v[triangleIndex.y];
    const vec3 pos2           = vertices.v[triangleIndex.z];
    const vec3 position       = pos0 * barycentrics.x + pos1 * barycentrics.y + pos2 * barycentrics.z;
    const vec3 world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));

    // if random scatter occured in media before hitting a surface, return
    if (randomScatterOccured(world_position))
        return;

    // Normal
    const vec3 nrm0         = normals.n[triangleIndex.x];
    const vec3 nrm1         = normals.n[triangleIndex.y];
    const vec3 nrm2         = normals.n[triangleIndex.z];
    vec3       normal       = normalize(nrm0 * barycentrics.x + nrm1 * barycentrics.y + nrm2 * barycentrics.z);
    const vec3 world_normal = normalize(vec3(normal * gl_WorldToObjectEXT));
    const vec3 geom_normal  = normalize(cross(pos1 - pos0, pos2 - pos0));


    // TexCoord
    const vec2 uv0       = texCoords.t[triangleIndex.x];
    const vec2 uv1       = texCoords.t[triangleIndex.y];
    const vec2 uv2       = texCoords.t[triangleIndex.z];
    const vec2 texcoord0 = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;

    // https://en.wikipedia.org/wiki/Path_tracing
    // Material of the object
    GltfShadeMaterial mat       = materials.m[matIndex];
    // vec3              emittance = mat.emissiveFactor;

    // Pick a random direction from here and keep going.
    vec3 tangent, bitangent;
    createCoordinateSystem(world_normal, tangent, bitangent);
    vec3 rayOrigin    = world_position;
    vec3 rayDirection = microfacetBrdfSampling(prd.seed, world_normal, prd.rayDirection, mat.roughness);
    vec3        halfVec   = normalize(-prd.rayDirection + rayDirection);
    float       nDotH     = dot(world_normal, halfVec);
    // microfacetPDF(nDotH, mat.roughness) gives pdf for half-vector, not the out-going ray.
    float halfVecPdfVal = microfacetPDF(nDotH, mat.roughness);
    float rayPdfVal = halfVecPdfVal / (4 * dot(halfVec, -prd.rayDirection));

    // for now ignore rays going bellow surface
    if(dot(rayDirection, world_normal) < 0)
    {
        //debugPrintfEXT("Hello from invocation (%f, %f, %f), (%f, %f, %f)!\n", rayDirection.x, rayDirection.y, rayDirection.z, world_normal.x, world_normal.y, world_normal.z);
        prd.rayOrigin    = rayOrigin;
        prd.rayDirection = rayDirection;
        prd.weight = vec3(0.0);
        return;
            
    }
    float cos_theta = dot(rayDirection, world_normal); 
    vec3  albedo    = mat.pbrBaseColorFactor.xyz;

    if(mat.pbrBaseColorTexture > -1)
    {
        uint txtId = mat.pbrBaseColorTexture;
        albedo *= texture(texturesMap[nonuniformEXT(txtId)], texcoord0).xyz;
    }

    // grdf BDRF
    // both light and viewDir are supposed start from the shading location
    float       a2    = pow(mat.roughness, 4.0);
    
    
    float       nDotL     = dot(world_normal, -prd.rayDirection);
    float       vDotH     = dot(rayDirection, halfVec);
    float       hDotL     = dot(-prd.rayDirection, halfVec);
    float       vDotN     = dot(rayDirection, world_normal);

    vec3       c_diff = (1.0 - mat.metallic) * albedo;
    vec3       f0     = 0.04 * (1 - mat.metallic) + albedo * mat.metallic;
    vec3       frsnel    = f0 + (1 - f0) * pow(1 - abs(vDotH), 5);
    vec3  pdf_weighted_f_diffuse = (1.0 - frsnel) * c_diff / M_PI;


    float simplifiedGVal = 0.0;

    if(hDotL > 0 && vDotH > 0){
        float denom1 = sqrt(a2 + (1 - a2) * nDotL * nDotL); 
        denom1 += abs(nDotL);
        float denom2 = sqrt(a2 + (1 - a2) * vDotN * vDotN); 
        denom2 += abs(vDotN);
        simplifiedGVal = 1.0 / (denom1 * denom2);
    }

    if (mat.roughness == 0){
        pdf_weighted_f_diffuse = vec3(0.0);
    }
    else {
        //if(nDotH < 0)
           // debugPrintfEXT("Hello from invocation (%f, %f, %f)!\n", max(max(f_diffuse.x, f_diffuse.y), f_diffuse.z) /dVal, nDotH, dVal);
        pdf_weighted_f_diffuse =  pdf_weighted_f_diffuse / rayPdfVal;
    }

    // actually, gVal = simplifiedGVal *  (4 * abs(vDotn) * abs(nDotL) 
    // and, f_specular = frsnel * gVal * halfVecPdfVal / (rayPdfVal * (4 * abs(vDotn) * abs(nDotL) )
    // pdf_weighted_f_specular = f_specular / rayPdfVal
    vec3 pdf_weighted_f_specular = frsnel * simplifiedGVal * 4 * dot(halfVec, -prd.rayDirection);
    vec3 material_f = pdf_weighted_f_specular + pdf_weighted_f_diffuse;

    float rayLength = length(prd.rayOrigin - world_position);

    prd.rayOrigin    = rayOrigin;
    prd.rayDirection = rayDirection;
    prd.weight       = material_f * cos_theta * exp(-pcRay.airExtinctCoff * rayLength);

    return;

}
