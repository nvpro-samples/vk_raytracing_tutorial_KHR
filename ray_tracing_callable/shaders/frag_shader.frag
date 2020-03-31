#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#include "wavefront.glsl"


layout(push_constant) uniform shaderInformation
{
  vec3  lightPosition;
  float lightIntensity;
  vec3  lightDirection;
  float lightSpotCutoff;
  float lightSpotOuterCutoff;
  uint  instanceId;
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
// Buffers
layout(binding = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3) uniform sampler2D[] textureSamplers;
layout(binding = 4, scalar) buffer MatIndex { int i[]; } matIdx[];

// clang-format on


void main()
{
  // Object of this instance
  int objId = scnDesc.i[pushC.instanceId].objId;

  // Material of the object
  int               matIndex = matIdx[objId].i[gl_PrimitiveID];
  WaveFrontMaterial mat      = materials[objId].m[matIndex];

  vec3 N = normalize(fragNormal);

  // Vector toward light
  vec3  LightDir;
  float lightIntensity;
  ;

  // Point light
  if(pushC.lightType == 0)
  {
    vec3  lDir          = pushC.lightPosition - worldPos;
    float lightDistance = length(lDir);
    lightIntensity      = pushC.lightIntensity / (lightDistance * lightDistance);
    LightDir            = normalize(lDir);
  }
  else if(pushC.lightType == 1)
  {
    vec3  lDir          = pushC.lightPosition - worldPos;
    float lightDistance = length(lDir);
    lightIntensity      = pushC.lightIntensity / (lightDistance * lightDistance);
    LightDir            = normalize(lDir);
    float theta         = dot(LightDir, normalize(-pushC.lightDirection));
    float epsilon       = pushC.lightSpotCutoff - pushC.lightSpotOuterCutoff;
    float spotIntensity = clamp((theta - pushC.lightSpotOuterCutoff) / epsilon, 0.0, 1.0);
    lightIntensity *= spotIntensity;
  }
  else  // Directional light
  {
    LightDir       = normalize(-pushC.lightDirection);
    lightIntensity = 1.0;
  }


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, LightDir, N);
  if(mat.textureId >= 0)
  {
    int  txtOffset  = scnDesc.i[pushC.instanceId].txtOffset;
    uint txtId      = txtOffset + mat.textureId;
    vec3 diffuseTxt = texture(textureSamplers[txtId], fragTexCoord).xyz;
    diffuse *= diffuseTxt;
  }

  // Specular
  vec3 specular = computeSpecular(mat, viewDir, LightDir, N);

  // Result
  outColor = vec4(lightIntensity * (diffuse + specular), 1);
}
