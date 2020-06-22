#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

// clang-format off
layout( set = 0, binding = B_MATRICES) readonly buffer _Matrix { mat4 matrices[]; };
// clang-format on

layout(binding = 0) uniform UniformBufferObject
{
  mat4 view;
  mat4 proj;
  mat4 viewI;
}
ubo;

layout(push_constant) uniform shaderInformation
{
  vec3  lightPosition;
  uint  instanceId;
  float lightIntensity;
  int   lightType;
  int materialId;
}
pushC;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;


//layout(location = 0) flat out int matIndex;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 viewDir;
layout(location = 4) out vec3 worldPos;

out gl_PerVertex
{
  vec4 gl_Position;
};


void main()
{
  mat4 objMatrix   = matrices[pushC.instanceId];
  mat4 objMatrixIT = transpose(inverse(objMatrix));

  vec3 origin = vec3(ubo.viewI * vec4(0, 0, 0, 1));

  worldPos     = vec3(objMatrix * vec4(inPosition, 1.0));
  viewDir      = vec3(worldPos - origin);
  fragTexCoord = inTexCoord;
  fragNormal   = vec3(objMatrixIT * vec4(inNormal, 0.0));
  //  matIndex     = inMatID;

  gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
