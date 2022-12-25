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

vec2 fragCoordToUV(vec2 fragCoord){
    return vec2(fragCoord.xy) / vec2(config.res.xy);
}


void main()
{
  vec2  uv    = outUV;
  float gamma = 1. / 2.2;
  //vec4  color = texture(noisyTxt, uv);
  float ao_mid    = texture(aoTxt, uv).x;
  vec4 color = vec4(1.0, 1.0, 1.0, 1.0);
  float final_ao = 0.0;
  // Retrieving position and normal
  vec4 gBuffer = imageLoad(_gBuffer, ivec2(gl_FragCoord.xy)); //ivec2(uv.x * config.res.x, uv.y * config.res.y));

  // Shooting rays only if a fragment was rendered
  if(gBuffer != vec4(0))
  {
    vec3 position_mid = gBuffer.xyz;
    vec3 normal_mid = DecompressUnitVec(floatBitsToUint(gBuffer.w));

    //try blurr
    
    float weight_acc = 0;
    float ao_acc = 0;


    //TODO: ampling kernel should be of size s_wd
    //then only combine with the positions sampled in the image, but use 3d gaussian and stuff

    float s_wd = s_wd_calc(config, position_mid);
    float step_size = s_wd; /// pow2[5];

    
    //give hash-cells colors
    //color = hash_to_color(H7D_SWD(config, position_mid, normal_mid, s_wd));

    for(int it = 0; it < 3; ++it){ //iterations of À-trous wavelet filtering
        
        for(int i = -2; i <= 2; ++i){
            for(int j = -2; j <= 2; ++j){
                    
                //circle

                vec2 sample_step = vec2(i, j) * step_size * pow2[it]; // in pixel-space
                vec2 jittering = vec2(random_in_range(0, -step_size, step_size), random_in_range(0, -step_size, step_size));
                vec2 sample_fragCoord = gl_FragCoord.xy + sample_step + jittering;
                vec2 sample_uv = fragCoordToUV(sample_fragCoord); //for ao texture
                float sample_ao = texture(aoTxt, sample_uv).x;

                vec3 sample_position = imageLoad(_gBuffer, ivec2(sample_fragCoord)).xyz;

                //calculate weights for bilateral blur
                float weight_dis = gauss3(position_mid, sample_position, step_size * pow2[5]);
                float weight_ao_diff = gauss1(ao_mid, sample_ao, 0.5);

                float weight = weight_dis * weight_ao_diff;
                
                weight_acc += weight;
                ao_acc += weight * sample_ao;
            }
        }

        
    }
    final_ao = ao_acc / weight_acc;

  }
  else{
    final_ao = 0.5;
    color = vec4(0,0,1,1);
  }

  //final_ao = texture(aoTxt, uv).x;
  fragColor = pow(color * final_ao, vec4(gamma));
}
