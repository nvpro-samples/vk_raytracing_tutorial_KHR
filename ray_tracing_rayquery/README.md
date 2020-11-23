# Ray Query - Tutorial


![](images/rayquery.png)

## Tutorial ([Setup](../docs/setup.md))

This is an extension of the Vulkan ray tracing [tutorial](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR).


This extension is allowing to execute ray intersection queries in any shader stages. In this example, we will add 
ray queries [(GLSL_EXT_ray_query)](https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GLSL_EXT_ray_query.txt) to the fragment shader to cast shadow rays.

In the contrary to all other examples, with this one, we are removing code. There are no need to have a SBT and a raytracing pipeline, the only thing that 
will matter, is the creation of the acceleration structure.

Starting from the end of the tutorial, [ray_tracing__simple](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/tree/master/ray_tracing__simple) we will remove 
all functions that were dedicated to ray tracing and keep only the construction of the BLAS and TLAS.

## Cleanup 

First, let's remove all extra code

### hello_vulkan (header)

Remove most functions and members to keep only what is need to create the acceleration structure:

~~~~ C++
// #VKRay
void                             initRayTracing();
nvvk::RaytracingBuilderKHR::Blas objectToVkGeometryKHR(const ObjModel& model);
void                             createBottomLevelAS();
void                             createTopLevelAS();

vk::PhysicalDeviceRayTracingPropertiesKHR m_rtProperties;
nvvk::RaytracingBuilderKHR                m_rtBuilder;
~~~~ 

### hello_vulkan (source)

From the source code, remove the code for all functions that was previously removed.

### Shaders

You can safely remove all raytrace.* shaders 


## Support for Fragment shader

In `HelloVulkan::createDescriptorSetLayout`, add the acceleration structure to the description layout to have access to the acceleration structure directly in the fragment shader.

~~~~ C++
// The top level acceleration structure
m_descSetLayoutBind.emplace_back(  //
    vkDS(7, vkDT::eAccelerationStructureKHR, 1, vkSS::eFragment));
~~~~ 

In `HelloVulkan::updateDescriptorSet`, write the value to the descriptor set.

~~~~ C++
  vk::AccelerationStructureKHR                   tlas = m_rtBuilder.getAccelerationStructure();
  vk::WriteDescriptorSetAccelerationStructureKHR descASInfo;
  descASInfo.setAccelerationStructureCount(1);
  descASInfo.setPAccelerationStructures(&tlas);
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 7, descASInfo));
~~~~ 


### Shader 

The last modification is in the fragment shader, where we will add the ray intersection query to trace shadow rays.

First, the version has bumpped to 460

~~~~ C++
#version 460
~~~~ 

Then we need to add new extensions

~~~~ C++
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
~~~~

We have to add the layout to access the top level acceleration structure.

~~~~ C++
layout(binding = 7, set = 0) uniform accelerationStructureEXT topLevelAS;
~~~~


Ad the end of the shader, add the following code to initiate the ray query. As we are only interested to know if the ray 
has hit something, we can keep the minimal.

~~~~ C++
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
~~~~

