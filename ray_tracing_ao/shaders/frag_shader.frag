#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

#include "raycommon.glsl"
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
//layout(location = 0) flat in int matIndex;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 viewDir;
layout(location = 4) in vec3 worldPos;
// Outgoing
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outGbuffer;
// Buffers
layout(binding = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3) uniform sampler2D[] textureSamplers;
layout(binding = 4, scalar) buffer MatIndex { int i[]; } matIdx[];
//layout(binding = 7, set = 0) uniform accelerationStructureEXT topLevelAS;

// clang-format on


void main()
{
  // Object of this instance
  int objId = scnDesc.i[pushC.instanceId].objId;

  // Material of the object
  int               matIndex = matIdx[nonuniformEXT(objId)].i[gl_PrimitiveID];
  WaveFrontMaterial mat      = materials[nonuniformEXT(objId)].m[matIndex];

  vec3 N = normalize(fragNormal);

  // Vector toward light
  vec3  L;
  float lightDistance;
  float lightIntensity = pushC.lightIntensity;
  if(pushC.lightType == 0)
  {
    vec3  lDir     = pushC.lightPosition - worldPos;
    float d        = length(lDir);
    lightIntensity = pushC.lightIntensity / (d * d);
    L              = normalize(lDir);
    lightDistance  = d;
  }
  else
  {
    L             = normalize(pushC.lightPosition - vec3(0));
    lightDistance = 10000;
  }


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, L, N);
  diffuse      = vec3(1);
  //  if(mat.textureId >= 0)
  //  {
  //    int  txtOffset  = scnDesc.i[pushC.instanceId].txtOffset;
  //    uint txtId      = txtOffset + mat.textureId;
  //    vec3 diffuseTxt = texture(textureSamplers[nonuniformEXT(txtId)], fragTexCoord).xyz;
  //    diffuse *= diffuseTxt;
  //  }
  //lightIntensity = 1;
  // Specular
  vec3 specular  = vec3(0);  //computeSpecular(mat, viewDir, L, N);
  lightIntensity = 1;
  // Result
  outColor = vec4(lightIntensity * (diffuse + specular), 1);


  outGbuffer.rgba = vec4(worldPos, uintBitsToFloat(CompressUnitVec(N)));
}
