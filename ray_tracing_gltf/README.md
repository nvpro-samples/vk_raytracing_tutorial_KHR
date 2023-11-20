# NVIDIA Vulkan Ray Tracing Tutorial - glTF Scene

![img](images/vk_ray_tracing_gltf_KHR.png)


This example is the result of the modification of the [simple ray tracing](../ray_tracing__simple) tutorial.
Instead of loading separated OBJ objects, the example was modified to load glTF scene files containing multiple objects.

This example is not about shading, but using more complex data than OBJ.

For a more complete version, see

* https://github.com/nvpro-samples/vk_raytrace
* https://github.com/nvpro-samples/vk_shaded_gltfscene



## Scene Data

The OBJ models were loaded and stored in four buffers: 

* vertices: array of structure of position, normal, texcoord, color
* indices: index of the vertex, every three makes a triangle
* materials: the wavefront material structure
* material index: material index per triangle.

Since we could have multiple OBJ, we would have arrays of those buffers.

With glTF scene, the data will be organized differently a choice we have made for convenience. Instead of having structure of vertices,  
positions, normals and other attributes will be in separate buffers. There will be one single position buffer,
for all geometries of the scene, same for indices and other attributes. But for each geometry there is the information
of the number of elements and offsets. 

From the source tutorial, we will not need the following and therefore remove it:

~~~~C
  std::vector<ObjModel>    m_objModel;   // Model on host
  std::vector<ObjDesc>     m_objDesc;    // Model description for device access
  std::vector<ObjInstance> m_instances;  // Scene model instances
~~~~

In `host_device.h` we will add new host/device structures: PrimMeshInfo, SceneDesc and GltfShadeMaterial.

~~~~C
// Structure used for retrieving the primitive information in the closest hit
struct PrimMeshInfo
{
  uint indexOffset;
  uint vertexOffset;
  int  materialIndex;
};

// Scene buffer addresses
struct SceneDesc
{
  uint64_t vertexAddress;    // Address of the Vertex buffer
  uint64_t normalAddress;    // Address of the Normal buffer
  uint64_t uvAddress;        // Address of the texture coordinates buffer
  uint64_t indexAddress;     // Address of the triangle indices buffer
  uint64_t materialAddress;  // Address of the Materials buffer (GltfShadeMaterial)
  uint64_t primInfoAddress;  // Address of the mesh primitives buffer (PrimMeshInfo)
};
~~~~

And also, our glTF material representation for the shading. This is a stripped down version of the glTF PBR. If you are interested in the 
correct PBR implementation, check out [vk_raytrace](https://github.com/nvpro-samples/vk_raytrace).

~~~~ C
struct GltfShadeMaterial
{
  vec4 pbrBaseColorFactor;
  vec3 emissiveFactor;
  int  pbrBaseColorTexture;
};
~~~~


 And for holding the all the buffers allocated for representing the scene, we will store them in the following.

 ~~~~C
  nvh::GltfScene m_gltfScene;
  nvvk::Buffer   m_vertexBuffer;
  nvvk::Buffer   m_normalBuffer;
  nvvk::Buffer   m_uvBuffer;
  nvvk::Buffer   m_indexBuffer;
  nvvk::Buffer   m_materialBuffer;
  nvvk::Buffer   m_primInfo;
  nvvk::Buffer   m_sceneDesc;
~~~~

## Loading glTF scene

To load the scene, we will be using [TinyGLTF](https://github.com/syoyo/tinygltf) from Syoyo Fujita, then to avoid traversing 
the scene graph, the information will be flatten using the helper [gltfScene](https://github.com/nvpro-samples/nvpro_core/tree/master/nvh#gltfscenehpp).

### Loading Scene

Instead of loading a model, we will be loading a scene, so we are replacing `loadModel()` by `loadScene()`.

In the source file, loading the scene `loadScene()` will have first the glTF import with TinyGLTF.

~~~~C
  tinygltf::Model    tmodel;
  tinygltf::TinyGLTF tcontext;
  std::string        warn, error;

  if(!tcontext.LoadASCIIFromFile(&tmodel, &error, &warn, filename))
    assert(!"Error while loading scene");
~~~~

Then we will flatten the scene graph and grab the information we will need using the gltfScene helper.

~~~~C
  m_gltfScene.importMaterials(tmodel);
  m_gltfScene.importDrawableNodes(tmodel,
                                  nvh::GltfAttributes::Normal | nvh::GltfAttributes::Texcoord_0);
~~~~

The next part is to allocate the buffers to hold the information, such as the positions, normals, texture coordinates, etc.

~~~~C
  // Create the buffers on Device and copy vertices, indices and materials
  nvvk::CommandPool cmdBufGet(m_device, m_graphicsQueueIndex);
  VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

  m_vertexBuffer = m_alloc.createBuffer(cmdBuf, m_gltfScene.m_positions,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
  m_indexBuffer  = m_alloc.createBuffer(cmdBuf, m_gltfScene.m_indices,
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                           | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
  m_normalBuffer = m_alloc.createBuffer(cmdBuf, m_gltfScene.m_normals,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  m_uvBuffer     = m_alloc.createBuffer(cmdBuf, m_gltfScene.m_texcoords0,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
~~~~

We are making a simple material, extracting only a few members from the glTF material.

~~~~ C++
  // Copying all materials, only the elements we need
  std::vector<GltfShadeMaterial> shadeMaterials;
  for(auto& m : m_gltfScene.m_materials)
  {
    shadeMaterials.emplace_back(GltfShadeMaterial{m.baseColorFactor, m.emissiveFactor, m.baseColorTexture});
  }
  m_materialBuffer = m_alloc.createBuffer(cmdBuf, shadeMaterials,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
~~~~


To find the positions of the triangle hit in the closest hit shader, as well as the other 
attributes, we will store the offsets information of that geometry.

~~~~C
  // The following is used to find the primitive mesh information in the CHIT
  std::vector<PrimMeshInfo> primLookup;
  for(auto& primMesh : m_gltfScene.m_primMeshes)
  {
    primLookup.push_back({primMesh.firstIndex, primMesh.vertexOffset, primMesh.materialIndex});
  }
  m_rtPrimLookup =
      m_alloc.createBuffer(cmdBuf, primLookup, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
~~~~

Finally, we are creating a buffer holding the address of all buffers

~~~~ C++
  SceneDesc sceneDesc;
  sceneDesc.vertexAddress   = nvvk::getBufferDeviceAddress(m_device, m_vertexBuffer.buffer);
  sceneDesc.indexAddress    = nvvk::getBufferDeviceAddress(m_device, m_indexBuffer.buffer);
  sceneDesc.normalAddress   = nvvk::getBufferDeviceAddress(m_device, m_normalBuffer.buffer);
  sceneDesc.uvAddress       = nvvk::getBufferDeviceAddress(m_device, m_uvBuffer.buffer);
  sceneDesc.materialAddress = nvvk::getBufferDeviceAddress(m_device, m_materialBuffer.buffer);
  sceneDesc.primInfoAddress = nvvk::getBufferDeviceAddress(m_device, m_primInfo.buffer);
  m_sceneDesc               = m_alloc.createBuffer(cmdBuf, sizeof(SceneDesc), &sceneDesc,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
~~~~

Before closing the function, we will create textures (none in default scene) and submitting the command buffer.
The finalize and releasing staging is waiting for the copy of all data to the GPU.

~~~~ C
  // Creates all textures found
  createTextureImages(cmdBuf, tmodel);
  cmdBufGet.submitAndWait(cmdBuf);
  m_alloc.finalizeAndReleaseStaging();


  NAME_VK(m_vertexBuffer.buffer);
  NAME_VK(m_indexBuffer.buffer);
  NAME_VK(m_normalBuffer.buffer);
  NAME_VK(m_uvBuffer.buffer);
  NAME_VK(m_materialBuffer.buffer);
  NAME_VK(m_primInfo.buffer);
  NAME_VK(m_sceneDesc.buffer);
}
~~~~ 

**:warning: NOTE**: the macro `NAME_VK` is a convenience to name Vulkan object to easily identify them in Nsight Graphics and to know where it was created. 

## Converting geometry to BLAS

Instead of `objectToVkGeometryKHR()`, we will be using `primitiveToVkGeometry(const nvh::GltfPrimMesh& prim)`.
The function is similar, only the input is different, except for `VkAccelerationStructureBuildRangeInfoKHR` where 
we also include the offsets.

~~~~C
//--------------------------------------------------------------------------------------------------
// Converting a GLTF primitive in the Raytracing Geometry used for the BLAS
//
auto HelloVulkan::primitiveToGeometry(const nvh::GltfPrimMesh& prim)
{
  // BLAS builder requires raw device addresses.
  VkDeviceAddress vertexAddress = nvvk::getBufferDeviceAddress(m_device, m_vertexBuffer.buffer);
  VkDeviceAddress indexAddress  = nvvk::getBufferDeviceAddress(m_device, m_indexBuffer.buffer);

  uint32_t maxPrimitiveCount = prim.indexCount / 3;

  // Describe buffer as array of VertexObj.
  VkAccelerationStructureGeometryTrianglesDataKHR triangles{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
  triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 vertex position data.
  triangles.vertexData.deviceAddress = vertexAddress;
  triangles.vertexStride             = sizeof(glm::vec3);
  // Describe index data (32-bit unsigned int)
  triangles.indexType               = VK_INDEX_TYPE_UINT32;
  triangles.indexData.deviceAddress = indexAddress;
  // Indicate identity transform by setting transformData to null device pointer.
  //triangles.transformData = {};
  triangles.maxVertex = prim.vertexCount - 1;

  // Identify the above data as containing opaque triangles.
  VkAccelerationStructureGeometryKHR asGeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  asGeom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  asGeom.flags              = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;  // For AnyHit
  asGeom.geometry.triangles = triangles;

  VkAccelerationStructureBuildRangeInfoKHR offset;
  offset.firstVertex     = prim.vertexOffset;
  offset.primitiveCount  = prim.indexCount / 3;
  offset.primitiveOffset = prim.firstIndex * sizeof(uint32_t);
  offset.transformOffset = 0;

  // Our blas is made from only one geometry, but could be made of many geometries
  nvvk::RaytracingBuilderKHR::BlasInput input;
  input.asGeometry.emplace_back(asGeom);
  input.asBuildOffsetInfo.emplace_back(offset);

  return input;
}
~~~~

## Top Level creation

There are almost no differences, besides the fact that the index of the geometry is stored in `primMesh`.

~~~~C
  for(auto& node : m_gltfScene.m_nodes)
  {
    VkAccelerationStructureInstanceKHR rayInst;
    rayInst.transform                      = nvvk::toTransformMatrixKHR(node.worldMatrix);
    rayInst.instanceCustomIndex            = node.primMesh;  // gl_InstanceCustomIndexEXT: to find which primitive
    rayInst.accelerationStructureReference = m_rtBuilder.getBlasDeviceAddress(node.primMesh);
    rayInst.flags                          = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    rayInst.mask                           = 0xFF;
    rayInst.instanceShaderBindingTableRecordOffset = 0;  // We will use the same hit group for all objects
    tlas.emplace_back(rayInst);
  }
~~~~


## Raster Rendering

Raster rendering is simple. The shader was changed to use vertex, normal and texture coordinates. For 
each node, we will be pushing the material Id this primitive is using. Since we have flatten the scene graph,
we can loop over all drawable nodes.

~~~~C
  std::vector<VkBuffer> vertexBuffers = {m_vertexBuffer.buffer, m_normalBuffer.buffer, m_uvBuffer.buffer};
  vkCmdBindVertexBuffers(cmdBuf, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(), offsets.data());
  vkCmdBindIndexBuffer(cmdBuf, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

  uint32_t idxNode = 0;
  for(auto& node : m_gltfScene.m_nodes)
  {
    auto& primitive = m_gltfScene.m_primMeshes[node.primMesh];

    m_pcRaster.modelMatrix = node.worldMatrix;
    m_pcRaster.objIndex    = node.primMesh;
    m_pcRaster.materialId  = primitive.materialIndex;
    vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstantRaster), &m_pcRaster);
    vkCmdDrawIndexed(cmdBuf, primitive.indexCount, 1, primitive.firstIndex, primitive.vertexOffset, 0);
  }
~~~~


## Ray tracing change

In `createRtDescriptorSet()`, the only change we will add is the primitive info buffer to retrieve 
the data when hitting a triangle. 

~~~~C
  m_rtDescSetLayoutBind.addBinding(ePrimLookup, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);  // Primitive info
// ...
  VkDescriptorBufferInfo primitiveInfoDesc{m_rtPrimLookup.buffer, 0, VK_WHOLE_SIZE};
// ...
  writes.emplace_back(m_rtDescSetLayoutBind.makeWrite(m_rtDescSet, ePrimLookup, &primitiveInfoDesc));
~~~~


## Descriptors and Pipeline Changes

Since we are using different buffers and the vertex is no longer a struct but is using 
3 different buffers for the position, normal and texture coord. 
The methods `createDescriptorSetLayout()`, `updateDescriptorSet()` and `createGraphicsPipeline()`
will be changed accordingly.

See [hello_vulkan](hello_vulkan.cpp)


## Shaders

The shading is the same and is not reflecting the glTF PBR shading model, but the shaders were nevertheless 
changed to fit the new incoming format. 

* Raster : [vertex](shaders/vert_shader.vert), [fragment](shaders/frag_shader.frag)
* Ray Trace: [RayGen](shaders/raytrace.rgen), [ClosestHit](shaders/raytrace.rchit)


## Other changes

Small other changes were done, a different scene, different camera and light position.

Camera position
~~~~C
  CameraManip.setLookat(glm::vec3(0, 0, 15), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
~~~~

Scene
~~~~C
  helloVk.loadScene(nvh::findFile("media/scenes/cornellBox.gltf", defaultSearchPaths, true));
~~~~

Light Position
~~~~C
    glm::vec3 lightPosition{0.f, 4.5f, 0.f};
~~~~

# Simple Path Tracing

To convert this example to a simple path tracer (see Wikipedia [Path Tracing](https://en.wikipedia.org/wiki/Path_tracing)), we need to change the `RayGen` and the `ClosestHit` shaders.
Before doing this, we will modify the application to send the current rendering frame, allowing to accumulate
samples.


![img](images/vk_ray_tracing_gltf_KHR_2.png)

Add the following two functions in `hello_vulkan.cpp`:

~~~~C
//--------------------------------------------------------------------------------------------------
// If the camera matrix has changed, resets the frame.
// otherwise, increments frame.
//
void HelloVulkan::updateFrame()
{
  static glm::mat4 refCamMatrix;
  static float         refFov{CameraManip.getFov()};

  const auto& m   = CameraManip.getMatrix();
  const auto  fov = CameraManip.getFov();

  if(refCamMatrix != m || refFov != fov)
  {
    resetFrame();
    refCamMatrix = m;
    refFov       = fov;
  }
  m_pcRay.frame++;
}

void HelloVulkan::resetFrame()
{
  m_pcRay.frame = -1;
}
~~~~

And call `updateFrame()` in the begining of the `raytrace()` function.

In `hello_vulkan.cpp`, add the function declarations

~~~~C
  void updateFrame();
  void resetFrame();
~~~~

And add a new `frame` member at the end of `RtPushConstant` structure.

## Ray Generation

There are a few modifications to be done in the ray generation. First, it will use the clock for its random seed number.

This is done by adding the [`GL_ARB_shader_clock`](https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_shader_clock.txt) extension.

~~~~C
#extension GL_ARB_shader_clock : enable
~~~~

The random number generator is in `sampling.glsl`, `#include` this file.

In `main()`, we will initialize the random number like this: (see tutorial on jitter camera)

~~~~C
  // Initialize the random number
  uint seed = tea(gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x + gl_LaunchIDEXT.x, int(clockARB()));
~~~~

To accumulate the samples, instead of only write to the image, we will also use the previous frame.

~~~~C
  // Do accumulation over time
  if(pcRay.frame > 0)
  {
    float a         = 1.0f / float(pcRay.frame + 1);
    vec3  old_color = imageLoad(image, ivec2(gl_LaunchIDEXT.xy)).xyz;
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(mix(old_color, hitValue, a), 1.f));
  }
  else
  {
    // First frame, replace the value in the buffer
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.f));
  }
~~~~

Extra information will be needed in the ray payload `hitPayload`, the `seed` and the `depth`.

The modification in `raycommon.glsl`
~~~~C
struct hitPayload
{
  vec3 hitValue;
  uint seed;
  uint depth;
};
~~~~

## Closest Hit Shader

This modification will recursively trace until the `depth`hits 10 (hardcoded) or hit an emissive element (light).

The only information that we will keep from the shader, is the calculation of the hit state: the position and normal. So
all code from `// Vector toward the light` to the end can be removed and be replaced by the following.

~~~~C
  // https://en.wikipedia.org/wiki/Path_tracing
  // Material of the object
  GltfMaterial mat       = materials[nonuniformEXT(matIndex)];
  vec3         emittance = mat.emissiveFactor;

  // Pick a random direction from here and keep going.
  vec3 tangent, bitangent;
  createCoordinateSystem(world_normal, tangent, bitangent);
  vec3 rayOrigin    = world_position;
  vec3 rayDirection = samplingHemisphere(prd.seed, tangent, bitangent, world_normal);

  const float cos_theta = dot(rayDirection, world_normal);
  // Probability density function of samplingHemisphere choosing this rayDirection
  const float p = cos_theta / M_PI;

  // Compute the BRDF for this ray (assuming Lambertian reflection)
  vec3 albedo = mat.pbrBaseColorFactor.xyz;
  if(mat.pbrBaseColorTexture > -1)
  {
    uint txtId = mat.pbrBaseColorTexture;
    albedo *= texture(texturesMap[nonuniformEXT(txtId)], texcoord0).xyz;
  }
  vec3 BRDF = albedo / M_PI;

  // Recursively trace reflected light sources.
  if(prd.depth < 10)
  {
    prd.depth++;
    float tMin  = 0.001;
    float tMax  = 100000000.0;
    uint  flags = gl_RayFlagsOpaqueEXT;
    traceRayEXT(topLevelAS,    // acceleration structure
                flags,         // rayFlags
                0xFF,          // cullMask
                0,             // sbtRecordOffset
                0,             // sbtRecordStride
                0,             // missIndex
                rayOrigin,     // ray origin
                tMin,          // ray min range
                rayDirection,  // ray direction
                tMax,          // ray max range
                0              // payload (location = 0)
    );
  }
  vec3 incoming = prd.hitValue;

  // Apply the Rendering Equation here.
  prd.hitValue = emittance + (BRDF * incoming * cos_theta / p);
~~~~

:warning: **Note:** We do not implement the point light as in the Rasterizer. Therefore, only the emitting geometry will emit the energy to illuminate the scene.

## Miss Shader

To avoid contribution from the environment.

~~~~C
void main()
{
  if(prd.depth == 0)
    prd.hitValue = clearColor.xyz * 0.8;
  else
    prd.hitValue = vec3(0.01);  // Tiny contribution from environment
  prd.depth = 100;              // Ending trace
}
~~~~

# Faster Path Tracer

The implementation above is recursive and this is really not optimal. As described in the [reflection](../ray_tracing_reflections) 
tutorial, the best is to break the recursivity and do most of the work in the `RayGen`.

The following change can give up to **3 time faster** rendering.

To be able to do this, we need to extend the ray `payload` to bring data from the `Closest Hit` to the `RayGen`, which is the 
ray origin and direction and the BRDF weight. 

~~~~C
struct hitPayload
{
  vec3 hitValue;
  uint seed;
  uint depth;
  vec3 rayOrigin;
  vec3 rayDirection;
  vec3 weight;
};
~~~~

## Closest Hit

We don't need to trace anymore, so before tracing a new ray, we can store the information in 
the `payload` and return before the recursion code.

~~~~C
  prd.rayOrigin    = rayOrigin;
  prd.rayDirection = rayDirection;
  prd.hitValue     = emittance;
  prd.weight       = BRDF * cos_theta / p;
  return;
~~~~

## Ray Generation

The ray generation is the one that will do the trace loop. 

First initialize the `payload` and variable to compute the accumulation.

~~~~C
  prd.rayOrigin    = origin.xyz;
  prd.rayDirection = direction.xyz;
  prd.weight       = vec3(0);

  vec3 curWeight = vec3(1);
  vec3 hitValue  = vec3(0);
~~~~

Now the loop over the trace function, will be like the following.

 :warning: **Note:** the depth is hardcode, but could be a parameter to the `push constant`.

~~~~C
  for(; prd.depth < 10; prd.depth++)
  {
    traceRayEXT(topLevelAS,        // acceleration structure
                rayFlags,          // rayFlags
                0xFF,              // cullMask
                0,                 // sbtRecordOffset
                0,                 // sbtRecordStride
                0,                 // missIndex
                prd.rayOrigin,     // ray origin
                tMin,              // ray min range
                prd.rayDirection,  // ray direction
                tMax,              // ray max range
                0                  // payload (location = 0)
    );

    hitValue += prd.hitValue * curWeight;
    curWeight *= prd.weight;
  }
~~~~

:warning: **Note:** do not forget to use `hitValue` in the `imageStore`.
