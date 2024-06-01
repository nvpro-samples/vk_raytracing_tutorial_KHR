#version 460
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_ray_tracing : require

#include "raycommon.glsl"
#include "wavefront.glsl"

layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadEXT shadowPayload prdShadow;

layout(push_constant) uniform _PushConstantRay { PushConstantRay pcRay; };
hitAttributeEXT vec3 attribs;

layout(set = 0, binding = eTlas) uniform accelerationStructureEXT topLevelAS; // When chit cast rays, it need to know the TLAS

layout(buffer_reference, scalar)     buffer Vertices   {Vertex v[]; };              // Positions of an object
layout(buffer_reference, scalar)     buffer Indices    {ivec3 i[]; };               // Triangle indices
layout(buffer_reference, scalar)     buffer Materials  {WaveFrontMaterial m[]; };   // Array of all materials on an object
layout(buffer_reference, scalar)     buffer MatIndices {int i[]; };                 // Material ID for each triangle
layout(set = 1, binding = eObjDescs, scalar) buffer ObjDesc_ { ObjDesc i[]; } objDesc;

layout(set = 1, binding = eTextures) uniform sampler2D textureSamplers[];           // Buffer containing the materials

vec3 GetRefractedRayDir(vec3 enterRayDir, vec3 collisionNormal)
{
    float n1, n2;
    float enterAngle = 0.0f, exitAngle  = 0.0f;
    vec3 exitRayDir  = vec3(1);

    if(!prd.isRefracted) // If the ray is not a refracted one: Surface ==> Water
    {
        n1 = 1;
        n2 = 1.333;
    }
    else  // Else the ray is getting out of refracting surface: Water ==> surface
    {
        n1 = 1.333;
        n2 = 1;
    }

    enterAngle = acos(dot(enterRayDir, -collisionNormal));
    exitAngle  = enterAngle/n2;                             //  n1*enterAngle = n2*exitAngle

    exitRayDir = normalize(cos(exitAngle)/(-collisionNormal));

    return exitRayDir;
}

void main()
{
    // Object data
    ObjDesc    objResource = objDesc.i[gl_InstanceCustomIndexEXT]; // gl_InstanceCustomIndexEXT tells which object was hit
    MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
    Materials  materials   = Materials(objResource.materialAddress);
    Indices    indices     = Indices(objResource.indexAddress);
    Vertices   vertices    = Vertices(objResource.vertexAddress);
  
    // Indices of the triangle
    ivec3 ind = indices.i[gl_PrimitiveID]; // gl_PrimitiveID allows us to find the vertices of the triangle hit by the ray
  
    // Vertex of the triangle
    Vertex v0 = vertices.v[ind.x];
    Vertex v1 = vertices.v[ind.y];
    Vertex v2 = vertices.v[ind.z];

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    // Computing the coordinates of the hit position
    const vec3 pos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));  // Transforming the position to world space

    // Computing the normal at hit position
    const vec3 nrm      = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
    const vec3 worldNrm = normalize(vec3(nrm * gl_WorldToObjectEXT));  // Transforming the normal to world space


    // Vector toward the light
    vec3  L;
    float lightIntensity = pcRay.lightIntensity;
    float lightDistance  = 100000.0;

    // Point light
    if(pcRay.lightType == 0)
    {
        vec3 lDir      = pcRay.lightPosition - worldPos;
        lightDistance  = length(lDir);
        lightIntensity = pcRay.lightIntensity / (lightDistance * lightDistance);
        L              = normalize(lDir);
    }
    else  // Directional light
    {
        L = normalize(pcRay.lightPosition);
    }


    // Material of the object
    int               matIdx = matIndices.i[gl_PrimitiveID];
    WaveFrontMaterial mat    = materials.m[matIdx];

    // Diffuse
    vec3 diffuse = computeDiffuse(mat, L, worldNrm);
    if(mat.textureID >= 0)
    {
        uint txtId    = mat.textureID + objDesc.i[gl_InstanceCustomIndexEXT].txtOffset;
        vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
        diffuse *= texture(textureSamplers[nonuniformEXT(txtId)], texCoord).xyz;
    }
  
    // Specular
    vec3  specular    = vec3(0);
    float attenuation = 1;

    // Tracing shadow ray only if the light is visible from the surface and if it has not collided with a refractant surface
    if(dot(worldNrm, L) > 0 && mat.illum != 5)
    {
        float tMin   = 0.001;
        float tMax   = lightDistance;
        vec3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
        vec3  rayDir = L;
        uint  flags = gl_RayFlagsSkipClosestHitShaderEXT;
            //gl_RayFlagsSkipClosestHitShaderKHR: Will not invoke the hit shader, only the miss shader
            //gl_RayFlagsOpaqueKHR : Will not call the any hit shader, so all objects will be opaque
            //gl_RayFlagsTerminateOnFirstHitKHR : The first hit is always good.
            //gl_RayFlagsSkipClosestHitShaderEXT : This will enable transparent shadows.

        prdShadow.isHit                  = true;
        prdShadow.seed                   = prd.seed;
        prdShadow.isRefracted            = false;
        prdShadow.lastParticleCollision  = origin; // We set the last particle collision as this point
        prdShadow.particleCollisionCount = 0;

        traceRayEXT(topLevelAS,  // acceleration structure
                flags,       // rayFlags
                0xFF,        // cullMask
                1,           // sbtRecordOffset
                0,           // sbtRecordStride
                1,           // missIndex
                origin,      // ray origin
                tMin,        // ray min range
                rayDir,      // ray direction
                tMax,        // ray max range
                1            // payload (location = 1)
        );
        prd.seed = prdShadow.seed; 
    }

    if(prdShadow.isHit)
    {
      attenuation = 0.3;
    }
    else
    {
      // Specular
      specular = computeSpecular(mat, gl_WorldRayDirectionEXT, L, worldNrm);
    }

    // Reflection
    if(mat.illum == 3)
    {
        vec3 origin = worldPos;
        vec3 rayDir = reflect(gl_WorldRayDirectionEXT, worldNrm);
        prd.attenuation *= mat.specular;
        prd.done      = 0;
        prd.rayOrigin = origin;
        prd.rayDir    = rayDir;
    }

    // Refraction
    if(mat.illum == 5)
    {
        vec3 origin;
        vec3 collisionNormal;
        float eta;
        vec3 rayDir;
        if(!prd.isRefracted)
        {
            origin = worldPos;
            collisionNormal = worldNrm;
            eta = 1/1.33;
            rayDir      = refract(prd.rayDir, collisionNormal, eta);//GetRefractedRayDir(prd.rayDir, collisionNormal);
        }  
        else
        {
            origin = prd.lastParticleCollision;
            collisionNormal = prd.lastPartCollNormal;
            eta = 1.33/1;
            rayDir      = refract(prd.rayDir, collisionNormal, eta);//GetRefractedRayDir(prd.rayDir, collisionNormal);
        }
        
        //rayDir      = refract(prd.rayDir, collisionNormal, eta);//GetRefractedRayDir(prd.rayDir, collisionNormal);
        prd.attenuation *= mat.specular;
        prd.done         = 0;
        prd.rayOrigin    = origin;
        prd.rayDir       = rayDir;

        prd.isRefracted            = !prd.isRefracted;
        prd.lastParticleCollision  = origin;
        prd.particleCollisionCount = 0;
    }

    prd.hitValue = vec3(attenuation * lightIntensity * (diffuse + specular));
}