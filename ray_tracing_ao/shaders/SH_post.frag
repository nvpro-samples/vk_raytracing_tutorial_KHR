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

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#include "raycommon.glsl"
#include "SH_hash_tools.glsl"

layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D noisyTxt;
layout(set = 0, binding = 1) uniform sampler2D aoTxt;

layout(scalar, set = 0, binding = 2) readonly buffer HashMap {
    HashCell hashMap[HASH_MAP_SIZE];
};

layout(set = 0, binding = 3) uniform UniformBuffer {
    ConfigurationValues config;
};


layout(set = 0, binding = 4, rgba32f) uniform image2D _gBuffer;


layout(push_constant) uniform shaderInformation
{
  float aspectRatio;
}
pushc;


void main()
{
  vec2  uv    = outUV;
  float gamma = 1. / 2.2;
  //vec4  color = texture(noisyTxt, uv);
  float ao    = 0.0; //texture(aoTxt, uv).x;
  vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

  // Retrieving position and normal
  vec4 gBuffer = imageLoad(_gBuffer, ivec2(gl_FragCoord.xy)); //ivec2(uv.x * config.res.x, uv.y * config.res.y));

  // Shooting rays only if a fragment was rendered
  if(gBuffer != vec4(0))
  {
    vec3 position = gBuffer.xyz;
    vec3 normal = DecompressUnitVec(floatBitsToUint(gBuffer.w));
    
    float s_wd_real = s_wd_calc(config, position);

    const int min_nr_samples = 100;
    uint current_samples = 0;

    
    //color = hash_to_color(H7D_SWD(config, position, normal, s_wd_real)); //* vec4(position / 10, 1);

    
    for(int j = 0; j < 5; j++){
        
        float s_wd = (S_MIN * j) + s_wd_real;

        uint hash = H7D_SWD(config, position, normal, s_wd) % HASH_MAP_SIZE;
        uint checksum = H7D_SWD_checksum(config, position, normal, s_wd);

        for(int i = 0; i < LINEAR_SEARCH_LENGTH; i++){
            if(hashMap[hash].checksum == checksum){

                current_samples += hashMap[hash].contribution_counter;
                ao += hashMap[hash].ao_value;
                break;
            }
        }

        if(current_samples >= min_nr_samples){
            break;
        }
    }

    if(current_samples != 0){
        ao /= current_samples;
    }else {
        ao = 1.0;
        color = vec4(1,0,0,1);
    }
    

  }
  else{
    ao = 0.5;
    color = vec4(0,0,1,1);
  }

  //ao    = texture(aoTxt, uv).x;

 // ao = texture(aoTxt, uv).x;
  fragColor = pow(color * ao, vec4(gamma));
}
