#pragma once
#include "obj_loader.h"

// The OBJ model
struct ObjModel
{
  uint32_t   nbIndices{0};
  uint32_t   nbVertices{0};
  nvvkBuffer vertexBuffer;    // Device buffer of all 'Vertex'
  nvvkBuffer indexBuffer;     // Device buffer of the indices forming triangles
  nvvkBuffer matColorBuffer;  // Device buffer of array of 'Wavefront material'
  nvvkBuffer matIndexBuffer;  // Device buffer of array of 'Wavefront material'
};

// Instance of the OBJ
struct ObjInstance
{
  uint32_t      objIndex{0};     // Reference to the `m_objModel`
  uint32_t      txtOffset{0};    // Offset in `m_textures`
  nvmath::mat4f transform{1};    // Position of the instance
  nvmath::mat4f transformIT{1};  // Inverse transpose
};

// Information pushed at each draw call
struct ObjPushConstants
{
  nvmath::vec3f lightPosition{10.f, 15.f, 8.f};
  float         lightIntensity{100.f};
  nvmath::vec3f lightDirection{-1, -1, -1};
  float         lightSpotCutoff{cos(deg2rad(12.5f))};
  float         lightSpotOuterCutoff{cos(deg2rad(17.5f))};
  int           instanceId{0};  // To retrieve the transformation matrix
  int           lightType{0};   // 0: point, 1: infinite
  int           frame{0};
};

enum EObjType
{
  eSphere = 0,
  eCube
};

// One single implicit object
struct ObjImplicit
{
  nvmath::vec3f minimum{0, 0, 0};  // Aabb
  nvmath::vec3f maximum{0, 0, 0};  // Aabb
  int           objType{0};        // 0: Sphere, 1: Cube
  int           matId{0};
};

// All implicit objects
struct ImplInst
{
  std::vector<ObjImplicit> objImpl;     // All objects
  std::vector<MaterialObj> implMat;     // All materials used by implicit obj
  nvvkBuffer               implBuf;     // Buffer of objects
  nvvkBuffer               implMatBuf;  // Buffer of material
  int                      blasId;
  nvmath::mat4f            transform{1};
};
