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
layout(binding = 7, set = 1, scalar) buffer allImplicits_ {Implicit i[];} allImplicits;

// clang-format on

layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  vec3  lightDirection;
  float lightSpotCutoff;
  float lightSpotOuterCutoff;
  int   lightType;
}
pushC;

layout(location = 3) callableDataEXT rayLight cLight;


void main()
{
  vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

  Implicit impl = allImplicits.i[gl_PrimitiveID];

  // Computing the normal at hit position
  vec3 normal;
  if(gl_HitKindEXT == KIND_SPHERE)
  {
    vec3 center = (impl.maximum + impl.minimum) * 0.5;
    normal      = normalize(worldPos - center);
  }
  else if(gl_HitKindEXT == KIND_CUBE)
  {
    const float epsilon = 0.00001;
    if(abs(impl.maximum.x - worldPos.x) < epsilon)
      normal = vec3(1, 0, 0);
    else if(abs(impl.maximum.y - worldPos.y) < epsilon)
      normal = vec3(0, 1, 0);
    else if(abs(impl.maximum.z - worldPos.z) < epsilon)
      normal = vec3(0, 0, 1);
    else if(abs(impl.minimum.x - worldPos.x) < epsilon)
      normal = vec3(-1, 0, 0);
    else if(abs(impl.minimum.y - worldPos.y) < epsilon)
      normal = vec3(0, -1, 0);
    else if(abs(impl.minimum.z - worldPos.z) < epsilon)
      normal = vec3(0, 0, -1);
  }

  cLight.inHitPosition = worldPos;
  executeCallableEXT(pushC.lightType, 3);

  // Material of the object
  WaveFrontMaterial mat = materials[nonuniformEXT(gl_InstanceCustomIndexEXT)].m[impl.matId];


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, cLight.outLightDir, normal);

  vec3  specular    = vec3(0);
  float attenuation = 1;

  // Tracing shadow ray only if the light is visible from the surface
  if(dot(normal, cLight.outLightDir) > 0)
  {
    float tMin   = 0.001;
    float tMax   = cLight.outLightDistance;
    vec3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3  rayDir = cLight.outLightDir;
    uint  flags  = gl_RayFlagsSkipClosestHitShaderEXT;
    isShadowed   = true;
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
      // Specular
      specular = computeSpecular(mat, gl_WorldRayDirectionEXT, cLight.outLightDir, normal);
    }
  }

  // Reflection
  if(mat.illum == 3)
  {
    vec3 origin = worldPos;
    vec3 rayDir = reflect(gl_WorldRayDirectionEXT, normal);
    prd.attenuation *= mat.specular;
    prd.done      = 0;
    prd.rayOrigin = origin;
    prd.rayDir    = rayDir;
  }


  prd.hitValue = vec3(cLight.outIntensity * attenuation * (diffuse + specular));
}
