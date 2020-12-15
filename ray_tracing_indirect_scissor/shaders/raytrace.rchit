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
layout(location = 2) rayPayloadEXT int hitLanternInstance;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0) buffer LanternArray { LanternIndirectEntry lanterns[]; } lanterns;

layout(binding = 1, set = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, set = 1, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3, set = 1) uniform sampler2D textureSamplers[];
layout(binding = 4, set = 1)  buffer MatIndexColorBuffer { int i[]; } matIndex[];
layout(binding = 5, set = 1, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 6, set = 1) buffer Indices { uint i[]; } indices[];

// clang-format on

void main()
{
  // Object of this instance
  uint objId = scnDesc.i[gl_InstanceCustomIndexEXT].objId;

  // Indices of the triangle
  ivec3 ind = ivec3(indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 0],   //
                    indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 1],   //
                    indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 2]);  //
  // Vertex of the triangle
  Vertex v0 = vertices[nonuniformEXT(objId)].v[ind.x];
  Vertex v1 = vertices[nonuniformEXT(objId)].v[ind.y];
  Vertex v2 = vertices[nonuniformEXT(objId)].v[ind.z];

  const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Computing the normal at hit position
  vec3 normal = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
  // Transforming the normal to world space
  normal = normalize(vec3(scnDesc.i[gl_InstanceCustomIndexEXT].transfoIT * vec4(normal, 0.0)));


  // Computing the coordinates of the hit position
  vec3 worldPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
  // Transforming the position to world space
  worldPos = vec3(scnDesc.i[gl_InstanceCustomIndexEXT].transfo * vec4(worldPos, 1.0));

  // Vector toward the light
  vec3  L;
  vec3 colorIntensity = vec3(pushC.lightIntensity);
  float lightDistance = 100000.0;

  // ray direction is towards lantern, if in lantern pass.
  if (pushC.lanternPassNumber >= 0)
  {
    LanternIndirectEntry lantern = lanterns.lanterns[pushC.lanternPassNumber];
    vec3 lDir       = vec3(lantern.x, lantern.y, lantern.z) - worldPos;
    lightDistance   = length(lDir);
    vec3 color      = vec3(lantern.red, lantern.green, lantern.blue);
    // Lantern light decreases linearly. Not physically accurate, but looks good
    // and avoids a hard "edge" at the radius limit. Use a constant value
    // if lantern debug is enabled to clearly see the covered screen rectangle.
    float distanceFade =
      pushC.lanternDebug != 0
        ? 0.3
        : max(0, (lantern.radius - lightDistance) / lantern.radius);
    colorIntensity  = color * lantern.brightness * distanceFade;
    L               = normalize(lDir);
  }
  // Non-lantern pass may have point light...
  else if(pushC.lightType == 0)
  {
    vec3 lDir      = pushC.lightPosition - worldPos;
    lightDistance  = length(lDir);
    colorIntensity = vec3(pushC.lightIntensity / (lightDistance * lightDistance));
    L              = normalize(lDir);
  }
  else  // or directional light.
  {
    L = normalize(pushC.lightPosition - vec3(0));
  }

  // Material of the object
  int               matIdx = matIndex[nonuniformEXT(objId)].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[nonuniformEXT(objId)].m[matIdx];


  // Diffuse
  vec3 diffuse = computeDiffuse(mat, L, normal);
  if(mat.textureId >= 0)
  {
    uint txtId = mat.textureId + scnDesc.i[gl_InstanceCustomIndexEXT].txtOffset;
    vec2 texCoord =
        v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
    diffuse *= texture(textureSamplers[nonuniformEXT(txtId)], texCoord).xyz;
  }

  vec3  specular    = vec3(0);
  float attenuation = 1;

  // Tracing shadow ray only if the light is visible from the surface
  if(dot(normal, L) > 0)
  {
    float tMin   = 0.001;
    float tMax   = lightDistance;
    vec3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3  rayDir = L;

    // Ordinary shadow from the simple tutorial.
    if (pushC.lanternPassNumber < 0) {
      isShadowed = true;
      uint  flags  = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT
                      | gl_RayFlagsSkipClosestHitShaderEXT;
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
    }
    // Lantern shadow ray. Cast a ray towards the lantern whose lighting is being
    // added this pass. Only the closest hit shader for lanterns will set
    // hitLanternInstance (payload 2) to non-negative value.
    else {
      // Skip ray if no light would be added anyway.
      if (colorIntensity == vec3(0)) {
        isShadowed = true;
      }
      else {
        uint flags = gl_RayFlagsOpaqueEXT;
        hitLanternInstance = -1;
        traceRayEXT(topLevelAS, // acceleration structure
                    flags,      // rayFlags
                    0xFF,       // cullMask
                    2,          // sbtRecordOffset : lantern shadow hit groups start at index 2.
                    0,          // sbtRecordStride
                    2,          // missIndex       : lantern shadow miss shader is number 2.
                    origin,     // ray origin
                    tMin,       // ray min range
                    rayDir,     // ray direction
                    tMax,       // ray max range
                    2           // payload (location = 2)
        );
        // Did we hit the lantern we expected?
        isShadowed = (hitLanternInstance != pushC.lanternPassNumber);
      }
    }

    if(isShadowed)
    {
      attenuation = 0.1;
    }
    else
    {
      // Specular
      specular = computeSpecular(mat, gl_WorldRayDirectionEXT, L, normal);
    }
  }

  prd.hitValue = colorIntensity * (attenuation * (diffuse + specular));
  prd.additiveBlending = true;
}
