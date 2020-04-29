#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"
#include "wavefront.glsl"

hitAttributeEXT vec2 attribs;

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

layout(binding = 2, set = 1, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 5, set = 1, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 6, set = 1) buffer Indices { uint i[]; } indices[];

layout(binding = 1, set = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 3, set = 1) uniform sampler2D textureSamplers[];
layout(binding = 4, set = 1)  buffer MatIndexColorBuffer { int i[]; } matIndex[];
layout(binding = 7, set = 1, scalar) buffer allSpheres_ {Sphere i[];} allSpheres;

// clang-format on

layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;
}
pushC;


void main()
{
  vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

  Sphere instance = allSpheres.i[gl_PrimitiveID];

  // Computing the normal at hit position
  vec3 normal = normalize(worldPos - instance.center);

  // Computing the normal for a cube
  if(gl_HitKindEXT == KIND_CUBE)  // Aabb
  {
    vec3  absN = abs(normal);
    float maxC = max(max(absN.x, absN.y), absN.z);
    normal     = (maxC == absN.x) ?
                 vec3(sign(normal.x), 0, 0) :
                 (maxC == absN.y) ? vec3(0, sign(normal.y), 0) : vec3(0, 0, sign(normal.z));
  }

  // Vector toward the light
  vec3  L;
  float lightIntensity = pushC.lightIntensity;
  float lightDistance  = 100000.0;
  // Point light
  if(pushC.lightType == 0)
  {
    vec3 lDir      = pushC.lightPosition - worldPos;
    lightDistance  = length(lDir);
    lightIntensity = pushC.lightIntensity / (lightDistance * lightDistance);
    L              = normalize(lDir);
  }
  else  // Directional light
  {
    L = normalize(pushC.lightPosition - vec3(0));
  }

  // Material of the object
  int               matIdx = matIndex[gl_InstanceID].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[gl_InstanceID].m[matIdx];

  // Diffuse
  vec3  diffuse     = computeDiffuse(mat, L, normal);
  vec3  specular    = vec3(0);
  float attenuation = 0.3;

  // Tracing shadow ray only if the light is visible from the surface
  if(dot(normal, L) > 0)
  {
    float tMin   = 0.001;
    float tMax   = lightDistance;
    vec3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3  rayDir = L;
    uint  flags  = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT
                 | gl_RayFlagsSkipClosestHitShaderEXT;
    isShadowed = true;
    traceRayEXT(topLevelAS,  // acceleration structure
                flags,       // rayFlags
                0xFF,        // cullMask
                0,           // sbtRecordOffset
                0,           // sbtRecordStride
                1,           // missIndex
                origin,      // ray origin
                tMin,        // ray min range
                rayDir,      // ray direction
                tMax,        // ray max range
                1            // payload (location = 1)
    );

    if(isShadowed)
    {
      attenuation = 0.3;
    }
    else
    {
      attenuation = 1;
      // Specular
      specular = computeSpecular(mat, gl_WorldRayDirectionEXT, L, normal);
    }
  }

  prd.hitValue = vec3(lightIntensity * attenuation * (diffuse + specular));
}
