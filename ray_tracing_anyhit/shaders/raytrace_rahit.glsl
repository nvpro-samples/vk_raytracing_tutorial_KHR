//#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
//#extension GL_GOOGLE_include_directive : enable

#include "random.glsl"
#include "raycommon.glsl"
#include "wavefront.glsl"

// clang-format off
#ifdef PAYLOAD_0
layout(location = 0) rayPayloadInEXT hitPayload prd;
#elif defined(PAYLOAD_1)
layout(location = 1) rayPayloadInEXT shadowPayload prd;
#endif

layout(binding = 2, set = 1, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 4, set = 1)  buffer MatIndexColorBuffer { int i[]; } matIndex[];
layout(binding = 5, set = 1, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 6, set = 1) buffer Indices { uint i[]; } indices[];
layout(binding = 1, set = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
// clang-format on

void main()
{
  // Object of this instance
  uint objId = scnDesc.i[gl_InstanceID].objId;
  // Indices of the triangle
  uint ind = indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 0];
  // Vertex of the triangle
  Vertex v0 = vertices[nonuniformEXT(objId)].v[ind.x];

  // Material of the object
  int               matIdx = matIndex[nonuniformEXT(objId)].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[nonuniformEXT(objId)].m[matIdx];

  if(mat.illum != 4)
    return;

  if(mat.dissolve == 0.0)
    ignoreIntersectionEXT;
  else if(rnd(prd.seed) > mat.dissolve)
    ignoreIntersectionEXT;
}
