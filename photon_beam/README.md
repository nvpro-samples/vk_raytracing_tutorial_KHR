# Photon Beam

<p>
<img src="images/default_image.png" width="270">
<img src="images/default_image2.png" width="270">
<img src="images/default_image3.png" width="270">
</p>


#### Link to video
[![Link to video](https://img.youtube.com/vi/Tia85zQ_XLM/0.jpg)](https://youtu.be/Tia85zQ_XLM)

This is actually a video of DirectX 12 implementation explained in bellow section.
However, this video shows newly added light motion, and light variation features. 

Also, DirectX version now works similary with the Vulkan version.
There are some small differences in defautl parameter settings and GUI.

#### Link to old version video
[![Link to old version video](https://img.youtube.com/vi/CNZyZBfqwkY/0.jpg)](https://www.youtube.com/watch?v=CNZyZBfqwkY)

This is an obsolete video which was taken when there were no light motion nor light variation features.


## Background
This example aims to describe volumetric radiance from participating media.

This example used bellow version nvpro-core codes.

https://github.com/nvpro-samples/nvpro_core/commit/1d82623cf8fc0e3881150b5d0f0aef920d9af627


The example is result of modification of [glTF Scene](../ray_tracing__gltf) tutorial.
It has been modified a lot from [glTF Scene](../ray_tracing__gltf). You may want to check difference at bellow page.

[Compare with glTF Scene](https://github.com/donguklim/vk_raytracing_tutorial_KHR/compare/photon-beam-copied-from-gltf...donguklim:vk_raytracing_tutorial_KHR:master?expand=1)

While the original work omitted some features in shading material, this example loaded all features required for implementing 
[BRDF in glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation) for surface reflection.

For rendering, this example uses 

- Photon beam method for volumetric radiance estimation, with 
- Photon mapping method for surface reflection
- Simple raytracing for specular reflection.

For more detailed background of the techinques used, you may check the references.


## DirectX 12 Implementation

https://github.com/donguklim/DirectX12PhotonBeam

There is DirectX 12 version of the implementation, which is made later than this version.

DirectX 12 version is more upgraded than this version. It has new features light motion and light variation.

Unlike Vulkan version, which uses a fixed light simulation for every frame, DX12 implementation performs light simulation for each frame.


Vulkan version may also implement the upgraded light motion and light variagion feature in the future, when I have some time.


## References

 - ### Photon Mapping
    - Jensen, Henrik. (2001). A Practical Guide to Global Illumination using Photon Maps.
 - ### Photon Beam
    - Derek Nowrouzezahrai, Jared Johnson, Andrew Selle, Dylan Lacewell, Michael Kaschalk, Wojciech Jarosz. A programmable system for artistic volumetric lighting. ACM Transactions on Graphics (Proceedings of SIGGRAPH), 30(4):29:1–29:8, August 2011.
    - Wojciech Jarosz, Derek Nowrouzezahrai, Iman Sadeghi, Henrik Wann Jensen. A comprehensive theory of volumetric radiance estimation using photon points and beams. ACM Transactions on Graphics (Presented at SIGGRAPH), 30(1):5:1–5:19, January 2011.
 - ### BRDF Sampling
    - https://www.astro.umd.edu/~jph/HG_note.pdf    &nbsp; [[pdf file backup]](reference_backup/HG_note.pdf)
    - https://agraphicsguynotes.com/posts/sample_microfacet_brdf/   &nbsp; [[webpage backup as pdf file]](reference_backup/Importance_Sampling_techniques_for_GGX.pdf)
    - https://schuttejoe.github.io/post/ggximportancesamplingpart1/     &nbsp; [[webpage backup as pdf file]](reference_backup/sampling_with_microfacet_brdf.pdf)


## General Process

1. Build Model Accelerated Structure
    - Load the scene and save the 3D model in the accelerated structure.
2. Light Generation 
    - Use ray tracing method to simulate light. 
    - Save light interactions as photons and beams.
3. Build Light Accelerated Structure 
    - Build Accelerated structure that contains the beams and photons. 
4. Ray tracing. 
    - Accumulate beam and photon radiance to draw the image.


## Surface Accelerated Strucrue Generation 
This process is almost the same as the [glTF Scene](../ray_tracing__gltf).

In addition, roughness and metallic factors are also loaded for calculating BRDF equation provided by glTF specification.

#### **`shaders/host_device.h`**
~~~~C
  struct GltfShadeMaterial
{
  vec4 pbrBaseColorFactor;
  vec3 emissiveFactor;
  int  pbrBaseColorTexture;
  float metallic;
  float roughness;
  uint   padding[2];
};
~~~~
`uint   padding[2]` is for data alilghment in Vulkan buffer. When the struct data is copied to buffer, it must be multiple of size of the largest field, which is  `vec4`.
#### **`hello_vulkan.cpp`**
~~~~CPP
// Copying all materials, only the elements we need
  std::vector<GltfShadeMaterial> shadeMaterials;
  for(const auto& m : m_gltfScene.m_materials)
  {
    shadeMaterials.emplace_back(
        GltfShadeMaterial{
            m.baseColorFactor, 
            m.emissiveFactor, 
            m.baseColorTexture, 
            m.metallicFactor, 
            m.roughnessFactor
        }
    );
  }
  m_materialBuffer = m_alloc.createBuffer(
      cmdBuf, 
      shadeMaterials,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
  );
~~~~

## Light Generation

Lights are simulated using Vulkan raytracing.

Light generation and image drawing process will have separate descriptor sets, pipelines, shader bounding tables, shader scripts for ray tracing.

Bellow are the shader files used by light simulation.

- [Ray Generation Shader - photonbeam.rget](shaders/photonbeam.rgen)
- [Close Hit Shader - photonbeam.rchit](shaders/photonbeam.rchit)
- [Miss Shader - photonbeam.rmiss](shaders/photonbeam.rmiss)

In this process the accelerated structure that contains surface objects is used to simulate lights and surface interaction.
Bellow structs is used to save the light data.

### Accelerated structure and Light data

#### **`shaders/host_device.h`**
~~~~C
struct PhotonBeam
{
  vec3  startPos;
  uint	mediaIndex;
  vec3  endPos;
  float radius;
  vec3  lightColor;
  int   hitInstanceIndex;
};
~~~~

Currently, `radius` and `mediaIndex` are not used. 

You may want to use them if you want to constrcut light with different width radius and more than one type of volumetric media.

In this example, all lights have the same constant width, and the air is the only one participating media, so the two fields are only used as paddings.

The data for accelerate structure that will contain lights are saved as following struct.

#### **`shaders/host_device.h`**
~~~~C
struct ShaderVkAccelerationStructureInstanceKHR
{
  float                      matrix[3][4];
  uint                   instanceCustomIndexAndmask;
  uint                   instanceShaderBindingTableRecordOffsetAndflags;
  uint64_t                   accelerationStructureReference;
};
~~~~
This struct is used as `VkAccelerationStructureInstanceKHR`. It is made only for saving `VkAccelerationStructureInstanceKHR` in shader script. 

The buffer for saving light data and accelerated structure instance info data are referenced in ray generation shader as shown bellow.

#### **`shaders/photonbeam.rgen`**
~~~~C
layout(std430, set = 0, binding = 2) restrict buffer PhotonBeams{

    uint subBeamCount;
    uint beamCount;
    uint _padding_beams[2];
	PhotonBeam beams[];
};

layout(std430, set = 0, binding = 3) restrict buffer PhotonBeamsAs{
	ShaderVkAccelerationStructureInstanceKHR subBeams[];
};
~~~~
`subBeamCount` and `beamCount` are counters of the data.
The light generation process will stop if any one of the counter reaches to the maximum and filled all allocated space in the buffer.
`atomicAdd` function is used to increase count.

#### **`shaders/photonbeam.rgen`**
~~~~C
    beamIndex = atomicAdd(beamCount, 1);
    
    ...

    subBeamIndex = atomicAdd(subBeamCount, num_split + numSurfacePhoton);
~~~~

#### Surface Photon
Only one accelerated structure instance is saved for surface photon interaction.

The AS will be saved as a ball with the sampling radius.

One `PhotonBeam` is referenced by one photon AS.

Photons are located on the surface.

#### Beam
Beams in participating media will be sub-divided by its sampling radius.

The AS will be saved as a cylinder with the sampling radius.

One `PhotonBeam` is referenced by one or more beam AS.

### Light Path

A single invokation of the ray tracing process represents a path traveled by the light. 
Light may scatter or reflect on a surface until it gets absorbed.
A new `PhotonBeam` instance is generated if light scatters of reflects.

#### Media Scattering
A Light beam may randomly scatter or get absorbed in the middle of the air before it reaches a sold surface.

The probability of scattering, absortion, and the scattering length is dependent to the scattering cofficient and extinct cofficient of the volumetric media the light is passing through.

In the case of scattering a new light instance is generated with direction sampled from Henrey-Greenstein Phase function.


#### Surface Reflection
Light beam reaching a surface is reflected or absorbed.

The reflection direction is sampled by microfacet distribution.If the direction is bellow the surface, it is considered to be absorbed.
The refleccted light's power is weighted by `BRDF value / p` where p is the PDF value of the microfacet distribution.


## Light Acceleration Structure

The AS that will contain the light is built with following step.

Copy the counter of `ShaderVkAccelerationStructureInstanceKHR` instances generated, to a host visible memory.

Use fence to wait until the copy is finished.

#### **`hello_vulkan.cpp`**
~~~~C
    VkBufferCopy cpy;
    cpy.size      = sizeof(uint32_t);
    cpy.srcOffset = 0;
    cpy.dstOffset = 0;

    vkCmdCopyBuffer(
        m_pbBuildCommandBuffer, 
        m_beamBuffer.buffer, 
        m_beamAsCountReadBuffer.buffer, 
        1, 
        &cpy
    );

    vkResetFences(m_device, 1, &m_beamCounterReadFence);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
    submitInfo.pWaitDstStageMask = waitStages;  
    submitInfo.pWaitSemaphores = nullptr;  
    submitInfo.waitSemaphoreCount   = 0;  
    submitInfo.pSignalSemaphores  = nullptr; 
    submitInfo.signalSemaphoreCount = 0;              
    submitInfo.pCommandBuffers    = &m_pbBuildCommandBuffer;  
    submitInfo.commandBufferCount = 1;                           
    submitInfo.pNext              = nullptr;

    // Submit to the graphics queue passing a wait fence
    vkEndCommandBuffer(m_pbBuildCommandBuffer);
    vkQueueSubmit(m_queue, 1, &submitInfo, m_beamCounterReadFence);
~~~~

Read the host visible memory and get the counter value in CPU side.

#### **`hello_vulkan.cpp`**
~~~~C
    vkWaitForFences(m_device, 1, &m_beamCounterReadFence, VK_TRUE, UINT64_MAX);

    void*    numBeamAsdata = m_alloc.map(m_beamAsCountReadBuffer);
    uint32_t numBeamAs     = *(reinterpret_cast<uint32_t*>(numBeamAsdata));
    m_alloc.unmap(m_beamAsCountReadBuffer);
    numBeamAs = numBeamAs > m_maxNumSubBeams ? m_maxNumBeams : numBeamAs;
~~~~

Run the AS build command and wait on CPU side until it is finished.
#### **`hello_vulkan.cpp`**
~~~~C
    vkResetCommandBuffer(m_pbBuildCommandBuffer, 0);
    vkBeginCommandBuffer(m_pbBuildCommandBuffer, &beginInfo);
    m_debug.beginLabel(m_pbBuildCommandBuffer, "Beam AS build");

    VkBuildAccelerationStructureFlagsKHR flags  
        = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bool                                 update = false;
    bool                                 motion = false;

    VkBufferDeviceAddressInfo bufferInfo{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, 
        nullptr, 
        m_beamAsInfoBuffer.buffer
    };
    VkDeviceAddress instBufferAddr = vkGetBufferDeviceAddress(m_device, &bufferInfo);

    // Creating the TLAS
    nvvk::Buffer scratchBuffer;
    m_pbBuilder.cmdCreateTlas(
        m_pbBuildCommandBuffer, 
        numBeamAs, 
        instBufferAddr, 
        scratchBuffer, 
        flags, 
        update, 
        motion
    );
    vkEndCommandBuffer(m_pbBuildCommandBuffer);
~~~~

Now all requird ASs are built, and image can be drawn.


## Ray Tracing

Bellow are the shader files used for ray tracing.

- [Ray Generation Shader - raytrace.rchit](shaders/raytrace.rgen)
- [Beam Intersection Shader - raytrace.rint](shaders/raytrace.rint)
- [Beam Any Hit Shader - raytrace.raint](shaders/raytrace.rahit)
- [Photon Intersection Shader - raytrace_surface.rint](shaders/raytrace_surface.rint)
- [Photon Any Hit Shader - raytrace_surface.rahit](shaders/raytrace_surface.rahit)

There are close hit shader and miss shader loaded to the shader bounding table for ray tracing process,
but those scripts actually do nothing. Those scripts are only left for debugging purpose.

There are three types of radiance in the process.
1. Volumetric Radiance
2. Surface Radiance
3. Specular Radiance.

1 uses Beam Any hit shader and Beam intersection shader.
2 and 3 uses photon intersection shader and photon any hit shader.


Two accelerated structures will be used.
- Solid Surface AS
- Light AS

Solid surface AS is only used for getting ray length, and the data of the material hit by ray.
Rather than using separate shader scripts for solid surface AS, ray query extension is used to get the data.

#### **`main.cpp`**
~~~~C
VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    contextInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayQueryFeatures);
~~~~

#### **`raytrace.rgen`**
~~~~C
    rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery,              // Ray query
                                surfaceAS,                  // Top-level acceleration structure
                                gl_RayFlagsOpaqueEXT,  // Ray flags, here saying "treat all geometry as opaque"
                                0xFF,                  // 8-bit instance mask, here saying "trace against all instances"
                                prd.rayOrigin,             // Ray origin
                                0.0,                   // Minimum t-value
                                prd.rayDirection,          // Ray direction
                                tMax);              // Maximum t-value

        // Start traversal, and loop over all ray-scene intersections. When this finishes,
        // rayQuery stores a "committed" intersection, the closest intersection (if any).
        rayQueryProceedEXT(rayQuery);
        tMax = rayQueryGetIntersectionTEXT(rayQuery, true);

        ....

        prd.instanceIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
        const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);

        PrimMeshInfo pinfo = primInfo[prd.instanceIndex];
        uint indexOffset  = (pinfo.indexOffset / 3) + rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        
        ...

        vec3 barycentrics = vec3(0.0, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));

~~~~

### Volumetric Radiance

Eye rays hit the beam and accumulates the beam radiance.

The intersection shader checks if the eye ray is hitting the sampling cylinder volume of the beam.

Any hit shader accumulates the beam radiance hit by eye ray.


Bellow are 3 sets of images with different Henry-Greenstein assymetric factor g.
Positive g yields more front scattering. Negative g yields more back scattering. Zero g yields uniform scateering in all spherical direction.

Left images are generated by using both photon mapping and photon beam methods. Right images are generated with only photon beam methods.

#### g = 0
<p>
<img src="images/zero_g.png" width="400">
<img src="images/photon_beam_only_zero_g.png" width="400">
</p>

#### g = 0.7
<p>
<img src="images/0dot7_g.png" width="400">
<img src="images/photon_beam_only_0dot7g.png" width="400">

</p>

#### g = 0.-7
<p>
<img src="images/negative_0dot7_g.png" width="400">
<img src="images/photon_beam_only_negative_0dot7g.png" width="400">
</p>

Bellow additional 3 images are generated with more light intensity and air albedo.

Air scattering and extinct cofficients were also adjusted.

#### Left g=0.7,  Middle g=0,  Right g=-0.7
<p>
<img src="images/photon_beam_only_0dot7g2.png" width="260">
<img src="images/photon_beam_only_zero_g2.png" width="260">
<img src="images/photon_beam_only_negative_0dot7g2.png" width="260">
</p>
<p>




Bellow images are result of photon mapping with only 64 sample lights with shorter sampling radius.
Left shows the actual beams, and right is the solid color of the beam.

#### g = 0
<p>
<img src="images/photon_beam_only_zero_g3.png" width="400">
<img src="images/photon_beam_only_zero_g3_solid_color.png" width="400">
</p>

#### g = 0.7
<p>
<img src="images/photon_beam_only_0dot7g3.png" width="400">
<img src="images/photon_beam_only_0dot7g3_solid_color.png" width="400">
</p>

#### g = 0.-7
<p>
<img src="images/photon_beam_only_negative_0dot7g3.png" width="400">
<img src="images/photon_beam_only_negative_0dot7g3_solid_color.png" width="400">
</p>


### Surface Radiance

As you see in Above images, photon mapping method alone is not enough to describe the lighting.
Omitting the radiance from surface makes the scene brightly foggy or mythycaly dark.
So photon mapping method is added to reflect the surface light.


#### Photon mapping examples (16384 light samples)
Result without photon beam

<img src="images/only_photon_mapping.png" width="400"></p>

The actual photons hit on the surface.

<img src="images/only_photon_mapping_points.png" width="400">

The result when you add back the beam.

<img src="images/beam_photon_mapping.png" width="400">



#### Photon mapping examples (16384 light samples, increased light intensity)
Bellow is the result after increased light intensity. Still the quality of the result seems to low.

<img src="images/only_photon_mapping2.png" width="400">
<img src="images/only_photon_mapping2_points.png" width="400">
<img src="images/beam_photon_mapping2.png" width="400">

#### Photon mapping examples (65536 light samples, increased light intensity)
Bellow is result after increased the number of sample lights to 65536 from above.

<img src="images/only_photon_mapping3.png" width="400">
<img src="images/only_photon_mapping3_points.png" width="400">
<img src="images/beam_photon_mapping3.png" width="400">

### Specular Reflection

In above images, you may have noticed the two black balls with some small spotted high lilghts.
Those balls have zero roguhness. They only yield specular reflection.

Both photon mapping and photon beam methods are not good for specular reflection.
This is because specular reflections only account photons or beams with exactly or almost exactly matching the reflection angle of the eye ray on the surface.
Even if there is a photon exactly matching or almost matching the target reflection angle, 
calculating this kind of radiance with Monte Carlo method often yields division by zero, or an infinite randiance.

In the end, almost always no specular reflection at all by the sample phtons or beams, or very rarely infinite or too large radiance from one or two sample phtons or beams.

So forget drawing specular reflection with photon beam or photon mapping method, and just use ray tracing.

#### The method used

1. Ray generation shader shoot ray and accumulate radiance until the ray hits the first solid surface
2. If the hit material has roughness larger than 0.01, end ray tracing and return the radiance
3. Else, sample the new direction from microfacet distribution. (It gives a constant direction if roughness is 0.0).
4. Shoot ray again to the sampled direction to get specular radiance.

#### **`raytrace.rgen`**
~~~~C
    uint num_iteration = 2;
    for(int i=0; i < num_iteration; i ++)
    {
        // get the t value to surface
        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery,              // Ray query
                                surfaceAS,                  // Top-level acceleration structure
                                gl_RayFlagsOpaqueEXT,  // Ray flags, here saying "treat all geometry as opaque"
                                0xFF,                  // 8-bit instance mask, here saying "trace against all instances"
                                prd.rayOrigin,             // Ray origin
                                0.0,                   // Minimum t-value
                                prd.rayDirection,          // Ray direction
                                tMax);              // Maximum t-value

        ....

        vec3 viewingDirection = -prd.rayDirection;
        if (mat.roughness > 0.01)
            break;

        ....

        prd.rayOrigin = prd.rayOrigin - viewingDirection * tMax;
        prd.rayOrigin += prd.rayDirection;
        prd.weight *= exp(-pcRay.airExtinctCoff * tMax) * pdfWeightedGltfBrdf(prd.rayDirection, viewingDirection, world_normal, albedo, mat.roughness, mat.metallic);
        tMax = tMaxDefault;

        ...
~~~~

<img src="images/with_specular_lights.png" width="400">

### Control by Colors

In the result program, you do not set the scattering cofficent, extinct cofficient, and the power of light source directly.
Instead you can set air albedo value, which is scattering coffcient / extinct cofficient, and near beam color and a unit distant color.

This control is based on the article  A programmable system for artistic volumetric lighting(D Nowrouzezahrai).

Bean near color is the color of the beam you expect to view when the beam is at the light source, and eye position is at the default distance(which is set to 15 units).

Distant color is the color of the beam you expect to view when the beam is 1 unit away from the light source in the direction orthogonal to the line segment between the eye and the light source.

When only one beam is sampled, and the direction is orthogonal to the viewer, viewer can obviously see how beam changes from near color to distant color.

<p>
<img src="images/control_colors.png" width="400">
<img src="images/control_colors_result.png" width="400">
</p>

<p>
<img src="images/control_colors2.png" width="400">
<img src="images/control_colors_result2.png" width="400">
</p>

### Further Improvements

This is of course just my toy project for learning Vulkan.
There can be many ways for improvements.

#### Fix Beam cylinder popping out back face

<img src="images/light_cylinder_popping_out.png" width="600">

This is currently the most obvious problem.
The cylinder that contains the beam segment will pop out at back face of meshes.
A beam line segment itself does not pass through a surface mesh, but its cylinder edges may.
However, this would not be a problem in most cases since back face is not visible to viewer.

#### Adaptive Beam width

Some people may feel unatural that all beam lights have constant thickness along their paths. 

You may implement the adaptive beam width mentioned  Wojciech Jarosz et. al(2011) in above reference.

#### Multiple participating media

I applied a constant homogeneous scattering and estinct cofficients to the whole air rendered in the scene.

However, you may only want to apply different values in different volumes in restricted region of the space.

Making clouds, or smokes for example, with heterogeneous scatteinrg and extinction.


