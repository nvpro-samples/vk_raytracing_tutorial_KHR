# Reflections - Tutorial

![](images/reflections.png)

## Tutorial ([Setup](../docs/setup.md))

This is an extension of the Vulkan ray tracing [tutorial](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR).

## Setting Up the scene

First, we will create a scene with two reflective planes and a multicolored cube in the center. Change the `helloVk.loadModel` calls in `main()` to

~~~~ C++
  // Creation of the example
  helloVk.loadModel(nvh::findFile("media/scenes/cube.obj", defaultSearchPaths, true),
                    nvmath::translation_mat4(nvmath::vec3f(-2, 0, 0))
                        * nvmath::scale_mat4(nvmath::vec3f(.1f, 5.f, 5.f)));
  helloVk.loadModel(nvh::findFile("media/scenes/cube.obj", defaultSearchPaths, true),
                    nvmath::translation_mat4(nvmath::vec3f(2, 0, 0))
                        * nvmath::scale_mat4(nvmath::vec3f(.1f, 5.f, 5.f)));
  helloVk.loadModel(nvh::findFile("media/scenes/cube_multi.obj", defaultSearchPaths, true));
  helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths, true),
                    nvmath::translation_mat4(nvmath::vec3f(0, -1, 0)));
~~~~

Then find `cube.mtl` in `media/scenes` and modify the material to be 95% reflective, without any diffuse
contribution:

~~~~ C++
newmtl  cube_instance_material
illum 3
d 1  
Ns 32
Ni 0
Ka 0 0 0
Kd 0 0 0
Ks 0.95 0.95 0.95
~~~~

## Recursive Reflections

Vulkan ray tracing allows recursive calls to traceRayEXT, up to a limit defined by `VkPhysicalDeviceRayTracingPropertiesKHR`.

In `createRtPipeline()` in `hello_vulkan.cpp`, bring the maximum recursion depth up to 10, making sure not to exceed the physical device's maximum recursion limit:

~~~~ C++
  rayPipelineInfo.setMaxPipelineRayRecursionDepth(
      std::max(10u, m_rtProperties.maxRecursionDepth));  // Ray depth
~~~~

### `raycommon.glsl`

We will need to track the depth and the attenuation of the ray.
In the `hitPayload` struct in `raycommon.glsl`, add the following:

~~~~ C++
  int  depth;
  vec3 attenuation;
~~~~

### `raytrace.rgen`

In the ray generation shader, we will initialize all payload values before calling `traceRayEXT`.

~~~~ C++
  prd.depth       = 0;
  prd.hitValue    = vec3(0);
  prd.attenuation = vec3(1.f, 1.f, 1.f);
~~~~

### `raytrace.rchit`

At the end of the closest hit shader, before setting `prd.hitValue`, we need to shoot a ray if the material is reflective.

~~~~ C++
  // Reflection
  if(mat.illum == 3 && prd.depth < 10)
  {
    vec3 origin   = worldPos;
    vec3 rayDir   = reflect(gl_WorldRayDirectionEXT, normal);
    prd.attenuation *= mat.specular;

    prd.depth++;
    traceRayEXT(topLevelAS,         // acceleration structure
            gl_RayFlagsNoneEXT,  // rayFlags
            0xFF,               // cullMask
            0,                  // sbtRecordOffset
            0,                  // sbtRecordStride
            0,                  // missIndex
            origin,             // ray origin
            0.1,                // ray min range
            rayDir,             // ray direction
            100000.0,           // ray max range
            0                   // payload (location = 0)
    );
    prd.depth--;
  }
~~~~

The calculated `hitValue` needs to be accumulated, since the payload is global for the 
entire execution from raygen, so change the last line of `main()` to

~~~~ C++
prd.hitValue += vec3(attenuation * lightIntensity * (diffuse + specular)) * prd.attenuation;
~~~~

### `raytrace.rmiss`

Finally, the miss shader also needs to attenuate its contribution:

~~~~ C++
  prd.hitValue = clearColor.xyz * 0.8 * prd.attenuation;
~~~~

### Working, but limited

This is working, but it is limited to the number of recursions the GPU can do, and could also impact performance. Trying to go over the limit of recursions would eventually generate a device lost error.

## Iterative Reflections

Instead of dispatching new rays from the closest hit shader, we will return the information in the payload to shoot new rays if needed.

### 'raycommon.glsl'

Enhance the structure to add information to start new rays if wanted.

~~~~ C++
  int  done;
  vec3 rayOrigin;
  vec3 rayDir;
~~~~

### `raytrace.rgen`

Initialize the new members of the payload:

~~~~ C++
  prd.done        = 1;
  prd.rayOrigin   = origin.xyz;
  prd.rayDir      = direction.xyz;
~~~~

Instead of calling traceRayEXT only once, we will call it in a loop until we are done.

Wrap the trace call in `raytrace.rgen` like this:

~~~~ C++
  vec3 hitValue = vec3(0);
  for(;;)
  {
    traceRayEXT( /*.. */);

    hitValue += prd.hitValue * prd.attenuation;

    prd.depth++;
    if(prd.done == 1 || prd.depth >= 10)
      break;

    origin.xyz    = prd.rayOrigin;
    direction.xyz = prd.rayDir;
    prd.done      = 1; // Will stop if a reflective material isn't hit
  }
~~~~

And make sure to write the correct value

~~~~ C++
imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
~~~~

### `raytrace.rchit`

We no longer need to shoot rays from the closest hit shader, so we can replace the block at the end with

~~~~ C++
  if(mat.illum == 3)
  {
    vec3 origin = worldPos;
    vec3 rayDir = reflect(gl_WorldRayDirectionEXT, normal);
    prd.attenuation *= mat.specular;
    prd.done      = 0;
    prd.rayOrigin = origin;
    prd.rayDir    = rayDir;
  }
~~~~

The calculation of the hitValue also no longer needs to be additive, or take attenuation into account:

~~~~ C++
  prd.hitValue = vec3(attenuation * lightIntensity * (diffuse + specular));
~~~~

### `raytrace.rmiss`

Since the ray generation shader now handles attenuation, we no longer need to attenuate the value returned in the miss shader:

~~~~ C++
  prd.hitValue = clearColor.xyz * 0.8;
~~~~

### Max Recursion

Finally, we no longer need to have a deep recursion setting in `createRtPipeline` -- just a depth of 2, one for the initial ray generation segment and another for shadow rays.

~~~~ C++
  rayPipelineInfo.setMaxPipelineRayRecursionDepth(2);  // Ray depth
~~~~

In `raytrace.rgen`, we can now make the maximum ray depth significantly larger -- such as 100, for instance -- without causing a device lost error.

## Controlling Depth

As an extra, we can also add UI to control the maximum depth.

In the `RtPushConstant` structure, we can add a new `maxDepth` member to pass to the shader.

~~~~ C++
  struct RtPushConstant
  {
    nvmath::vec4f clearColor;
    nvmath::vec3f lightPosition;
    float         lightIntensity;
    int           lightType;
    int           maxDepth{10};
  } m_rtPushConstants;
~~~~

In the `raytrace.rgen` shader, we will collect the push constant data

~~~~ C++
layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;
  int   maxDepth;
}
pushC;
~~~~ 

Then test for the value for when to stop

~~~~ C++
    if(prd.done == 1 || prd.depth >= pushC.maxDepth)
      break;
~~~~

Finally, in `main.cpp` after the `renderUI()` function call, we will add a slider to control the depth value.

~~~~ C++
  ImGui::SliderInt("Max Depth", &helloVk.m_rtPushConstants.maxDepth, 1, 50);
~~~~

