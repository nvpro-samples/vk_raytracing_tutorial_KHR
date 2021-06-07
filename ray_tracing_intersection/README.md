# Intersection Shader - Tutorial

![](images/intersection.png)
<small>Author: [Martin-Karl Lefrançois](https://devblogs.nvidia.com/author/mlefrancois/)</small>


## Tutorial ([Setup](../docs/setup.md))

This is an extension of the Vulkan ray tracing [tutorial](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR).

This tutorial chapter shows how to use intersection shader and render different primitives with different materials.


## High Level Implementation

On a high level view, we will

* Add 2.000.000 axis aligned bounding boxes in a BLAS
* 2 materials will be added
* Every second intersected object will be a sphere or a cube and will use one of the two material.

To do this, we will need to:

* Add an intersection shader (.rint)
* Add a new closest hit shader (.chit)
* Create `VkAccelerationStructureGeometryKHR` from `VkAccelerationStructureGeometryAabbsDataKHR`

## Creating all spheres

In the HelloVulkan class, we will add the structures we will need. First the structure that defines a sphere.

~~~~ C++
  struct Sphere
  {
    nvmath::vec3f center;
    float         radius;
  };
~~~~

Then we need the Aabb structure holding all the spheres, but also used for the creation of the BLAS (`VK_GEOMETRY_TYPE_AABBS_KHR`). 

~~~~ C++
  struct Aabb
  {
    nvmath::vec3f minimum;
    nvmath::vec3f maximum;
  };
~~~~

All the information will need to be hold in buffers, which will be available to the shaders.

~~~~ C++
  std::vector<Sphere> m_spheres;                // All spheres
  nvvkBuffer          m_spheresBuffer;          // Buffer holding the spheres
  nvvkBuffer          m_spheresAabbBuffer;      // Buffer of all Aabb
  nvvkBuffer          m_spheresMatColorBuffer;  // Multiple materials
  nvvkBuffer          m_spheresMatIndexBuffer;  // Define which sphere uses which material
~~~~

Finally, there are two functions, one to create the spheres, and one that will create the intermediate structure for the BLAS.

~~~~ C++
  void                              createSpheres();
  nvvk::RaytracingBuilderKHR::Blas  sphereToVkGeometryKHR();
~~~~

The following implementation will create 2.000.000 spheres at random positions and radius. It will create the Aabb from the sphere definition, two materials which will be assigned alternatively to each object. All the created information will be moved to Vulkan buffers to be accessed by the intersection and closest shaders.

~~~~ C++

//--------------------------------------------------------------------------------------------------
// Creating all spheres
//
void HelloVulkan::createSpheres(uint32_t nbSpheres)
{
  std::random_device                    rd{};
  std::mt19937                          gen{rd()};
  std::normal_distribution<float>       xzd{0.f, 5.f};
  std::normal_distribution<float>       yd{6.f, 3.f};
  std::uniform_real_distribution<float> radd{.05f, .2f};

  // All spheres
  m_spheres.resize(nbSpheres);
  for(uint32_t i = 0; i < nbSpheres; i++)
  {
    Sphere s;
    s.center     = nvmath::vec3f(xzd(gen), yd(gen), xzd(gen));
    s.radius     = radd(gen);
    m_spheres[i] = std::move(s);
  }

  // Axis aligned bounding box of each sphere
  std::vector<Aabb> aabbs;
  aabbs.reserve(nbSpheres);
  for(const auto& s : m_spheres)
  {
    Aabb aabb;
    aabb.minimum = s.center - nvmath::vec3f(s.radius);
    aabb.maximum = s.center + nvmath::vec3f(s.radius);
    aabbs.emplace_back(aabb);
  }

  // Creating two materials
  MaterialObj mat;
  mat.diffuse = nvmath::vec3f(0, 1, 1);
  std::vector<MaterialObj> materials;
  std::vector<int>         matIdx(nbSpheres);
  materials.emplace_back(mat);
  mat.diffuse = nvmath::vec3f(1, 1, 0);
  materials.emplace_back(mat);

  // Assign a material to each sphere
  for(size_t i = 0; i < m_spheres.size(); i++)
  {
    matIdx[i] = i % 2;
  }

  // Creating all buffers
  using vkBU = VkBufferUsageFlagBits;
  nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
  auto              cmdBuf = genCmdBuf.createCommandBuffer();
  m_spheresBuffer          = m_alloc.createBuffer(cmdBuf, m_spheres, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_spheresAabbBuffer      = m_alloc.createBuffer(cmdBuf, aabbs, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  m_spheresMatIndexBuffer  = m_alloc.createBuffer(cmdBuf, matIdx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_spheresMatColorBuffer  = m_alloc.createBuffer(cmdBuf, materials, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  genCmdBuf.submitAndWait(cmdBuf);

  // Debug information
  m_debug.setObjectName(m_spheresBuffer.buffer, "spheres");
  m_debug.setObjectName(m_spheresAabbBuffer.buffer, "spheresAabb");
  m_debug.setObjectName(m_spheresMatColorBuffer.buffer, "spheresMat");
  m_debug.setObjectName(m_spheresMatIndexBuffer.buffer, "spheresMatIdx");
}
~~~~

Do not forget to destroy the buffers in `destroyResources()`

~~~~ C++
  m_alloc.destroy(m_spheresBuffer);
  m_alloc.destroy(m_spheresAabbBuffer);
  m_alloc.destroy(m_spheresMatColorBuffer);
  m_alloc.destroy(m_spheresMatIndexBuffer);
~~~~

We need a new bottom level acceleration structure (BLAS) to hold the implicit primitives. For efficiency and since all those primitives are static, they will all be added in a single BLAS.

What is changing compare to triangle primitive is the Aabb data (see Aabb structure) and the geometry type (`VK_GEOMETRY_TYPE_AABBS_KHR`).

~~~~ C++
//--------------------------------------------------------------------------------------------------
// Returning the ray tracing geometry used for the BLAS, containing all spheres
//
nvvk::RaytracingBuilderKHR::BlasInput HelloVulkan::sphereToVkGeometryKHR()
{
  VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  info.buffer                 = m_spheresAabbBuffer.buffer;
  VkDeviceAddress dataAddress = vkGetBufferDeviceAddress(m_device, &info);

  VkAccelerationStructureGeometryAabbsDataKHR aabbs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR};
  aabbs.data.deviceAddress = dataAddress;
  aabbs.stride             = sizeof(Aabb);

  // Setting up the build info of the acceleration (C version, c++ gives wrong type)
  VkAccelerationStructureGeometryKHR asGeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  asGeom.geometryType   = VK_GEOMETRY_TYPE_AABBS_KHR;
  asGeom.flags          = VK_GEOMETRY_OPAQUE_BIT_KHR;
  asGeom.geometry.aabbs = aabbs;

  VkAccelerationStructureBuildRangeInfoKHR offset{};
  offset.firstVertex     = 0;
  offset.primitiveCount  = (uint32_t)m_spheres.size();  // Nb aabb
  offset.primitiveOffset = 0;
  offset.transformOffset = 0;

  nvvk::RaytracingBuilderKHR::BlasInput input;
  input.asGeometry.emplace_back(asGeom);
  input.asBuildOffsetInfo.emplace_back(offset);
  return input;
}
~~~~

## Setting Up the Scene

In `main.cpp`, where we are loading the OBJ model, we can replace it with

~~~~ C++
  // Creation of the example
  helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths, true));
  helloVk.createSpheres();
~~~~

 **Note:** it is possible to have more OBJ models, but the spheres will need to be added after all of them.

The scene will be large, better to move the camera out

~~~~ C++
  CameraManip.setLookat(nvmath::vec3f(20, 20, 20), nvmath::vec3f(0, 1, 0), nvmath::vec3f(0, 1, 0));
~~~~

## Acceleration Structures

### BLAS

The function `createBottomLevelAS()` is creating a BLAS per OBJ, the following modification will add a new BLAS containing the Aabb's of all spheres.

~~~~ C++
void HelloVulkan::createBottomLevelAS()
{
  // BLAS - Storing each primitive in a geometry
  std::vector<nvvk::RaytracingBuilderKHR::BlasInput> allBlas;
  allBlas.reserve(m_objModel.size());
  for(const auto& obj : m_objModel)
  {
    auto blas = objectToVkGeometryKHR(obj);

    // We could add more geometry in each BLAS, but we add only one for now
    allBlas.emplace_back(blas);
  }

  // Spheres
  {
    auto blas = sphereToVkGeometryKHR();
    allBlas.emplace_back(blas);
  }

  m_rtBuilder.buildBlas(allBlas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
}
~~~~

### TLAS

Similarly in `createTopLevelAS()`, the top level acceleration structure will need to add a reference to the BLAS of the spheres. We are setting the instanceCustomId and blasId to the last element, which is why the sphere BLAS must be added after everything else.

The hitGroupId will be set to 1 instead of 0. We need to add a new hit group for the implicit primitives, since we will need to compute attributes like the  normal, since they are not provide like with triangle primitives.

Just before building the TLAS, we need to add the following

~~~~ C++
  // Add the blas containing all spheres
  {
    nvvk::RaytracingBuilder::Instance rayInst;
    rayInst.transform        = m_objInstance[0].transform;          // Position of the instance
    rayInst.instanceCustomId = static_cast<uint32_t>(tlas.size());  // gl_InstanceCustomIndexEXT
    rayInst.blasId           = static_cast<uint32_t>(m_objModel.size());
    rayInst.hitGroupId       = 1;  // We will use the same hit group for all objects
    rayInst.flags            = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    tlas.emplace_back(rayInst);
  }
~~~~

## Descriptors

To access the newly created buffers holding all the spheres and materials, some changes are required to the descriptors.

In function `createDescriptorSetLayout()`, the addition of the material and material index need to be instructed.

~~~~ C++
  // Materials (binding = 1)
  m_descSetLayoutBind.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nbObj+1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
  // Materials Index (binding = 4)
  m_descSetLayoutBind.addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nbObj +1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
~~~~

And the new buffer holding the spheres

~~~~ C++
  // Storing spheres (binding = 7)
  m_descSetLayoutBind.addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                 VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
~~~~

The function `updateDescriptorSet()` which is writing the values of the buffer need also to be modified.

At the end of the loop on all models, lets add the new material and material index.

~~~~ C++
  for(auto& m : m_objModel)
  {
    dbiMat.push_back({m.matColorBuffer.buffer, 0, VK_WHOLE_SIZE});
    dbiMatIdx.push_back({m.matIndexBuffer.buffer, 0, VK_WHOLE_SIZE});
    dbiVert.push_back({m.vertexBuffer.buffer, 0, VK_WHOLE_SIZE});
    dbiIdx.push_back({m.indexBuffer.buffer, 0, VK_WHOLE_SIZE});
  }
  dbiMat.push_back({m_spheresMatColorBuffer.buffer, 0, VK_WHOLE_SIZE});
  dbiMatIdx.push_back({m_spheresMatIndexBuffer.buffer, 0, VK_WHOLE_SIZE});
~~~~

Then write the buffer for the spheres

~~~~ C++
  VkDescriptorBufferInfo dbiSpheres{m_spheresBuffer.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 7, &dbiSpheres));
~~~~

## Intersection Shader

The intersection shader is added to the Hit Group `VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR`. In our example, we already have a Hit Group for triangle and a closest hit associated. We will add a new one, which will become the Hit Group ID (1), see the TLAS section.

Here is how the two hit group looks like:

~~~~ C++
  enum StageIndices
  {
    eRaygen,
    eMiss,
    eMiss2,
    eClosestHit,
    eClosestHit2,
    eIntersection,
    eShaderGroupCount
  };

  // Closest hit
  stage.module = nvvk::createShaderModule(m_device, nvh::loadFile("spv/raytrace2.rchit.spv", true, defaultSearchPaths, true));
  stage.stage          = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
  stages[eClosestHit2] = stage;
  // Intersection
  stage.module = nvvk::createShaderModule(m_device, nvh::loadFile("spv/raytrace.rint.spv", true, defaultSearchPaths, true));
  stage.stage           = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
  stages[eIntersection] = stage;
~~~~

~~~~ C++
  // closest hit shader + Intersection (Hit group 2)
  group.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
  group.closestHitShader   = eClosestHit2;
  group.intersectionShader = eIntersection;
  m_rtShaderGroups.push_back(group);
~~~~

### raycommon.glsl

To share the structure of the data across the shaders, we can add the following to `raycommon.glsl`

~~~~ C++
struct Sphere
{
  vec3  center;
  float radius;
};

struct Aabb
{
  vec3 minimum;
  vec3 maximum;
};

#define KIND_SPHERE 0
#define KIND_CUBE 1
~~~~

### raytrace.rint

The intersection shader `raytrace.rint` need to be added to the shader directory and CMake to be rerun such that it is added to the project. The shader will be called every time a ray will hit one of the Aabb of the scene. Note that there are no Aabb information that can be retrieved in the intersection shader. It is also not possible to have the value of the hit point that the ray tracer might have calculated on the GPU.

The only information we have is that one of the Aabb was hit and using the `gl_PrimitiveID`, it is possible to know which one it was. Then, with the information stored in the buffer, we can retrive the geometry information of the sphere.

We first declare the extensions and include common files.

~~~~ C++
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"
#include "wavefront.glsl"
~~~~


The following is the topology of all spheres, which we will be able to retrieve using `gl_PrimitiveID`.

~~~~ C++
layout(binding = 7, set = 1, scalar) buffer allSpheres_
{
  Sphere i[];
}
allSpheres;
~~~~

We will implement two intersetion method against the incoming ray.

~~~~ C++
struct Ray
{
  vec3 origin;
  vec3 direction;
};
~~~~

The sphere intersection

~~~~ C++
// Ray-Sphere intersection
// http://viclw17.github.io/2018/07/16/raytracing-ray-sphere-intersection/
float hitSphere(const Sphere s, const Ray r)
{
  vec3  oc           = r.origin - s.center;
  float a            = dot(r.direction, r.direction);
  float b            = 2.0 * dot(oc, r.direction);
  float c            = dot(oc, oc) - s.radius * s.radius;
  float discriminant = b * b - 4 * a * c;
  if(discriminant < 0)
  {
    return -1.0;
  }
  else
  {
    return (-b - sqrt(discriminant)) / (2.0 * a);
  }
}
~~~~

And the axis aligned bounding box intersection

~~~~ C++
// Ray-AABB intersection
float hitAabb(const Aabb aabb, const Ray r)
{
  vec3  invDir = 1.0 / r.direction;
  vec3  tbot   = invDir * (aabb.minimum - r.origin);
  vec3  ttop   = invDir * (aabb.maximum - r.origin);
  vec3  tmin   = min(ttop, tbot);
  vec3  tmax   = max(ttop, tbot);
  float t0     = max(tmin.x, max(tmin.y, tmin.z));
  float t1     = min(tmax.x, min(tmax.y, tmax.z));
  return t1 > max(t0, 0.0) ? t0 : -1.0;
}
~~~~

Both are returning -1 if there is no hit, otherwise, it returns the distance from to origin of the ray.

Retrieving the ray is straight forward

~~~~ C++
void main()
{
  Ray ray;
  ray.origin    = gl_WorldRayOriginEXT;
  ray.direction = gl_WorldRayDirectionEXT;
~~~~

And getting the information about the geometry enclosed in the Aabb can be done like this.

~~~~ C++
  // Sphere data
  Sphere sphere = allSpheres.i[gl_PrimitiveID];
~~~~

Now we just need to know if we will hit a sphere or a cube.

~~~~ C++
  float tHit    = -1;
  int   hitKind = gl_PrimitiveID % 2 == 0 ? KIND_SPHERE : KIND_CUBE;
  if(hitKind == KIND_SPHERE)
  {
    // Sphere intersection
    tHit = hitSphere(sphere, ray);
  }
  else
  {
    // AABB intersection
    Aabb aabb;
    aabb.minimum = sphere.center - vec3(sphere.radius);
    aabb.maximum = sphere.center + vec3(sphere.radius);
    tHit         = hitAabb(aabb, ray);
  }
  ~~~~

Intersection information is reported using `reportIntersectionEXT`, with a distance from the origin and a second argument (hitKind) that can be used to differentiate the primitive type.

~~~~ C++

  // Report hit point
  if(tHit > 0)
    reportIntersectionEXT(tHit, hitKind);
}
~~~~

The shader can be found [here](shaders/raytrace.rint)

### raytrace2.rchit

The new closest hit can be found [here](shaders/raytrace2.rchit)

This shader is almost identical to original `raytrace.rchit`, but since the primitive is implicit, we will only need to compute the normal for the primitive that was hit.

We retrieve the world position from the ray and the `gl_HitTEXT` which was set in the intersection shader.

~~~~ C++
  vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
~~~~

The sphere information is retrieved the same way as in the `raytrace.rint` shader.

~~~~ C++
  Sphere instance = allSpheres.i[gl_PrimitiveID];
~~~~

Then we compute the normal, as for a sphere.

~~~~ C++
  // Computing the normal at hit position
  vec3 normal = normalize(worldPos - instance.center);
~~~~

To know if we have intersect a cube rather than a sphere, we are using  `gl_HitKindEXT`, which was set in the second argument of `reportIntersectionEXT`.

So when this is a cube, we set the normal to the major axis.

~~~~ C++
  // Computing the normal for a cube if the hit intersection was reported as 1
  if(gl_HitKindEXT == KIND_CUBE)  // Aabb
  {
    vec3  absN = abs(normal);
    float maxC = max(max(absN.x, absN.y), absN.z);
    normal     = (maxC == absN.x) ?
                 vec3(sign(normal.x), 0, 0) :
                 (maxC == absN.y) ? vec3(0, sign(normal.y), 0) : vec3(0, 0, sign(normal.z));
  }
~~~~
