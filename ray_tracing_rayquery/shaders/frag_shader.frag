#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

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
// Buffers
layout(binding = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3) uniform sampler2D[] textureSamplers;
layout(binding = 4, scalar) buffer MatIndex { int i[]; } matIdx[];
layout(binding = 7, set = 0) uniform accelerationStructureEXT topLevelAS;

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
  if(mat.textureId >= 0)
  {
    int  txtOffset  = scnDesc.i[pushC.instanceId].txtOffset;
    uint txtId      = txtOffset + mat.textureId;
    vec3 diffuseTxt = texture(textureSamplers[nonuniformEXT(txtId)], fragTexCoord).xyz;
    diffuse *= diffuseTxt;
  }

  // Specular
  vec3 specular = computeSpecular(mat, viewDir, L, N);

  // Result
  outColor = vec4(lightIntensity * (diffuse + specular), 1);


  // Ray Query for shadow
  vec3  origin    = worldPos;
  vec3  direction = L;  // vector to light
  float tMin      = 0.01f;
  float tMax      = lightDistance;

  // Initializes a ray query object but does not start traversal
  rayQueryEXT rayQuery;
  rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, tMin,
                        direction, tMax);

  // Start traversal: return false if traversal is complete
  while(rayQueryProceedEXT(rayQuery))
  {
  }

  // Returns type of committed (true) intersection
  if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
  {
    // Got an intersection == Shadow
    outColor *= 0.1;
  }
}
