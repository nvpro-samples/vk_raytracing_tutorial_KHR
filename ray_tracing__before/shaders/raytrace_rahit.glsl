#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require


#include "random.glsl"
#include "raycommon.glsl"
#include "wavefront.glsl"

#ifdef PAYLOAD_0
layout(location = 0) rayPayloadInEXT hitPayload prd;
#elif defined(PAYLOAD_1)
layout(location = 1) rayPayloadInEXT shadowPayload prd;
#endif

// clang-format off
layout(buffer_reference, scalar) buffer Vertices   {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices    {ivec3 i[]; };  // Triangle indices

layout(buffer_reference, scalar) buffer Materials {WaveFrontMaterial m[]; }; // Array of all materials on an object
layout(buffer_reference, scalar) buffer MatIndices {int i[]; }; // Material ID for each triangle
layout(set = 1, binding = eObjDescs, scalar) buffer ObjDesc_ { ObjDesc i[]; } objDesc;

hitAttributeEXT vec3 attribs; // Collision data
// clang-format on

void main()
{
    // Object data
    ObjDesc    objResource = objDesc.i[gl_InstanceCustomIndexEXT];
    MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
    Materials  materials   = Materials(objResource.materialAddress);
    Indices    indices     = Indices(objResource.indexAddress);
    Vertices   vertices    = Vertices(objResource.vertexAddress);

    // Material of the object
    int               matIdx = matIndices.i[gl_PrimitiveID];
    WaveFrontMaterial mat    = materials.m[matIdx];

    // If the surface is refractant
    if(mat.illum == 5)
    {
      // Indices of the triangle
      ivec3 ind = indices.i[gl_PrimitiveID];  // gl_PrimitiveID allows us to find the vertices of the triangle hit by the ray
      // Vertex of the triangle
      Vertex v0 = vertices.v[ind.x];
      Vertex v1 = vertices.v[ind.y];
      Vertex v2 = vertices.v[ind.z];

      // Computing the coordinates of the hit position
      const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

      const vec3 pos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
      const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));  // Transforming the position to world space

      // Computing the normal at hit position
      const vec3 nrm      = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
      const vec3 worldNrm = normalize(vec3(nrm * gl_WorldToObjectEXT));  // Transforming the normal to world space

      if(prd.isRefracted)  // and the ray is alrealdy a refracted ray
      {
        // If hasn't gone out of the water body == Particles are close by OR Ray hasn't been refracted max times
        if(length(vec3(worldPos - prd.lastParticleCollision)) < 1.0f || prd.particleCollisionCount < 100)
        {
          prd.lastParticleCollision = worldPos;  // We set the last collision to be this one in order to check the distance with next particle
          prd.particleCollisionCount += 1;  // Add 1 to the particles intersected count
          prd.lastPartCollNormal = worldNrm;
          ignoreIntersectionEXT;
        }
        // Ray has intersected max particles allowed
        else if(prd.particleCollisionCount == 100)
        {
          prd.lastParticleCollision = worldPos;
          prd.lastPartCollNormal    = worldNrm;
          return;
        }
        // Ray is yet inside the water body
        else
        {
          return;
        }
      }
      return;
    }

    // If the surface is not reflectant it accepts the collision by the way
    if (mat.illum != 4)
        return;

    

    if (mat.dissolve == 0.0)
        ignoreIntersectionEXT;
    else if(rnd(prd.seed) > mat.dissolve)
        ignoreIntersectionEXT;
}