![logo](http://nvidianews.nvidia.com/_ir/219/20157/NV_Designworks_logo_horizontal_greenblack.png)

# NVIDIA Vulkan Ray Tracing Tutorials (v2.0)

![resultRaytraceShadowMedieval](/docs/images/tuto.png)

This repository provides a comprehensive learning resource for Vulkan ray tracing, featuring a **progressive step-by-step tutorial** that transforms a rasterization application into a fully functional ray tracing implementation. The tutorial demonstrates practical integration of [`ray tracing`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#ray-tracing) and [`ray traversal`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#ray-traversal) using the [`VK_KHR_acceleration_structure`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_acceleration_structure), [`VK_KHR_ray_tracing_pipeline`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_tracing_pipeline) and [`VK_KHR_ray_query`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_query) extensions.

Looking for the original version?  
Check out the [legacy Vulkan ray tracing tutorial (pre-v2.0)](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/tree/master).

## Quick Start

### Prerequisites

- [nvpro_core2](https://github.com/nvpro-samples/nvpro_core2): Vulkan helper classes and utilities  
  - See **Clone** instructions below
- [Vulkan 1.4+](https://vulkan.lunarg.com/sdk/home): Compatible GPU and drivers  
  - Select **Volk headers** during installation for better compatibility
- [CMake](https://cmake.org/download/) 3.18+

### Build Instructions

```bash
# Clone repositories
git clone https://github.com/nvpro-samples/nvpro_core2.git
git clone https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR.git

# Build
cmake -B build -S .
cmake --build build -j 8
```

Compiled files will be under the `_bin` directory.

## Progressive Ray Tracing Tutorial

This repository provides a comprehensive, step-by-step tutorial that demonstrates how to convert a modern Vulkan rasterization application into a fully functional ray tracing implementation. The tutorial takes a hands-on approach, ensuring developers understand both the theoretical concepts and practical implementation details.

### Tutorial Overview

- **Progressive Conversion**: Transform `01_foundation` (raster-based) into `02_basic` (ray tracing) through 8 carefully designed phases
- **Compilable Checkpoints**: Each phase results in a working, testable application
- **Industry Implementation**: Learn standard practices for acceleration structures, shader binding tables, and ray tracing pipelines
- **Modern Vulkan 1.4**: Utilize current features including shader objects, push descriptors, and ray tracing extensions

---
<div align="center">

# 🚀 [**BEGIN THE TUTORIAL**](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/) 🚀

</div>

*This tutorial is designed for developers with basic Vulkan knowledge who want to add ray tracing to their skillset. Each phase builds upon the previous one, ensuring a solid foundation for advanced ray tracing development.*

## Essential Ray Tracing Documentation

This repository provides comprehensive documentation covering the core concepts and implementation details of Vulkan ray tracing. These documents serve as essential reference material for understanding the theoretical foundations and advanced techniques used throughout the tutorial series.

### Core Documentation

- **[Rendering Concepts](/docs/rendering_concepts.md)**  
  Plain-language guide to rendering acronyms (BRDF, BSDF, PDF, NEE, MIS). New to these concepts? Start here before diving into the technical documentation.

- **[Acceleration Structures](/docs/acceleration_structures.md)**  
  Comprehensive guide to building and managing acceleration structures (BLAS and TLAS) in Vulkan. Covers construction algorithms, memory management, update strategies, and performance optimization techniques. Includes detailed coverage of utilities like `nvvk::AccelerationStructureHelper` and `nvvk::AccelerationStructureBuilder`.

- **[Shader Binding Table (SBT)](/docs/shader_binding_table.md)**  
  Detailed explanation of the Shader Binding Table structure, alignment requirements, and management techniques. Covers the `nvvk::SBTGenerator` utility for efficient SBT creation and advanced patterns for custom data and multiple shader groups.


> **Note**: The Acceleration Structures and SBT documentation should be read alongside the tutorial for complete understanding of the implementation details.

## Complete Tutorial Series

This comprehensive collection of focused tutorials builds upon the foundation established in the progressive tutorial, exploring specific ray tracing concepts and advanced techniques:


| Tutorial | Features |
|----------|----------|
| [01_foundation](raytrace_tutorial/01_foundation)<br/>![](/docs/images/01_th.jpg)|**Rasterization-Only Foundation**<br/>- Sets up a modern Vulkan 1.4 rasterization pipeline (no ray tracing yet)<br/> - Loads and displays glTF objects using vertex and fragment shaders<br/>- Demonstrates use of Shader Objects and Push Descriptors for flexible resource binding<br/>- Establishes the application and rendering framework that all ray tracing samples will build upon<br/>- Provides a clean, minimal starting point for transitioning to ray tracing in later steps |
| [02_basic](raytrace_tutorial/02_basic)<br/>![](/docs/images/02_th.jpg)|**Basic Ray Tracing (Direct Vulkan)**<br/> - Transitions from rasterization to a minimal ray tracing pipeline using Vulkan 1.4.<br/>- Sets up acceleration structures (BLAS/TLAS) for the loaded glTF scene.<br/>- Implements a ray generation shader, closest hit shader, and miss shader.<br/>- Renders the scene using ray tracing: primary rays are cast from the camera, and the closest intersection is shaded.<br/>- Introduces a simple material system for basic surface color and lighting.<br/>- Demonstrates how to bind acceleration structures and shader tables.<br/>- **Uses direct Vulkan calls** for educational purposes - shows complete implementation without helper libraries.<br/>- Provides a clear, minimal foundation for all subsequent ray tracing features.|
| [02_basic_nvvk](raytrace_tutorial/02_basic_nvvk)<br/>![](/docs/images/02_th.jpg)|**Basic Ray Tracing (Helper Libraries)**<br/> - **Same functionality as 02_basic** but uses nvpro-core2 helper libraries for simplified development.<br/>- Uses `nvvk::AccelerationStructureHelper` for automatic scratch buffer management and batch building.<br/>- Uses `nvvk::SBTGenerator` for automatic shader binding table creation and alignment handling.<br/>- **Production-ready approach** with reduced code complexity and better performance.<br/>- **Used as foundation** for all subsequent tutorials in the series (03_any_hit, 04_jitter_camera, etc.).<br/>- Demonstrates the recommended approach for real-world ray tracing applications.|
| [03_any_hit](raytrace_tutorial/03_any_hit)<br/>![](/docs/images/03_th.jpg) | **Any-Hit Shaders and Transparency**<br/>- Introduces the **any-hit shader** stage to the ray tracing pipeline.<br/>- Demonstrates how any-hit shaders can be used for:<br/>  - **Transparency**: Ignoring intersections to create cutouts or alpha-tested materials.<br/>  - **Alpha Testing**: Discarding hits based on texture alpha values.<br/>  - **Custom Intersection Logic**: Procedural cutouts and complex intersection behavior.<br/>- Implements a circular cutout in a plane using the any-hit shader and a user-adjustable radius.<br/>- Adds a UI slider to control the cutout radius in real time.<br/>- Shows how to attach the any-hit shader to the hit group in the pipeline.<br/>- Explains the use of `IgnoreHit()` in HLSL to skip intersections. |
| [04_jitter_camera](raytrace_tutorial/04_jitter_camera)<br/>![](/docs/images/04_th.jpg)|**Camera Jitter and Anti-Aliasing**<br/> - Introduces **camera jitter** to implement temporal anti-aliasing (TAA) in ray tracing.<br/>- Offsets the camera's projection matrix each frame using a sub-pixel jitter pattern (e.g., Halton sequence).<br/>- Demonstrates how jittered camera rays reduce aliasing and improve image quality over multiple frames.<br/>- Accumulates frames in a history buffer to blend results and achieve smooth anti-aliased output.<br/>- Adds a UI toggle to enable/disable jitter and control the accumulation reset.<br/>- Explains how to update the camera and accumulation logic in both the application and shaders. |
| [05_shadow_miss](raytrace_tutorial/05_shadow_miss)<br/> ![](/docs/images/05_th.jpg) | **Shadow Miss Shader and Efficient Shadow Rays**<br/> - Introduces a **dedicated miss shader** for shadow rays, separate from the main miss shader.<br/>- Demonstrates why using the same miss shader for both primary and shadow rays is inefficient:<br/> - Shadow rays only need to know if they hit something (occlusion), not full color/lighting.<br/>  - The main miss shader may perform expensive sky/lighting calculations unnecessary for shadows.<br/>- Implements a **minimal payload** (`ShadowPayload`) for shadow rays, reducing memory and register usage.<br/>- Shows how to set up the ray tracing pipeline with multiple miss shader groups:<br/>  - **Group 0**: Main miss shader for primary rays.<br/>  - **Group 1**: Lightweight miss shader for shadow rays.<br/>- Updates the shadow tracing code to use the new miss shader group and minimal payload.<br/>- Results in improved performance, especially in scenes with many shadow rays. |
| [06_reflection](raytrace_tutorial/06_reflection) <br/> ![](/docs/images/06_th.jpg) | **Reflections with Ray Tracing**<br/> - Introduces **reflection rays** to simulate mirror-like surfaces using ray tracing.<br/>- Demonstrates how to trace secondary rays from hit points to compute reflections.<br/>- Adds a new closest hit shader that spawns reflection rays and blends their results with the surface color.<br/>- Shows how to limit reflection recursion depth for performance and to avoid infinite loops.<br/>- Implements a simple material system to control reflectivity per object.<br/>- Updates the shader binding table and descriptor sets to support reflection rays.<br/>- Explains how to accumulate and denoise reflections for improved visual quality. |
| [07_multi_closest_hit](raytrace_tutorial/07_multi_closest_hit)<br/>![](/docs/images/07_th.jpg)| **Multiple Closest Hit Shaders** <br/> - Introduces **multiple closest hit shaders** to the ray tracing pipeline.<br/>- Demonstrates how different objects can use different closest hit shaders:<br/>  - **Default shader**: Standard PBR lighting for the plane.<br/>  - **Second shader**: First wuson instance with shader record data for color.<br/>  - **Third shader**: Second wuson instance with shader record data for color.<br/>- Implements **shader record data** to pass instance-specific information through the Shader Binding Table (SBT).<br/>- Shows how to set up multiple hit groups in the ray tracing pipeline.<br/>- Adds a UI to dynamically change the colors of the wuson instances in real-time.<br/>- Explains the benefits of using multiple closest hit shaders for performance and flexibility. |
| [08_intersection](raytrace_tutorial/08_intersection)<br/>![](/docs/images/08_th.jpg) |**Intersection Shaders for Implicit Primitives**<br/> - Demonstrates the use of **intersection shaders** to render implicit primitives (spheres and cubes) alongside traditional triangle geometry.<br/>- Shows how to define custom geometry (spheres/cubes) in the shader and intersect rays with them at runtime.<br/>- Implements a buffer of 2,000,000 implicit objects, alternating between spheres and cubes, each with different materials.<br/>- Explains how to:<br/>  - Define implicit object structures and types in the shader interface.<br/>  - Build and upload buffers for implicit objects and their AABBs.<br/>  - Register custom intersection shaders in the ray tracing pipeline.<br/>  - Use a single closest hit shader for all implicit objects, with logic to distinguish between spheres and cubes.<br/>  - Alternate materials and colors procedurally for visual variety.<br/>- Illustrates how to combine triangle and procedural geometry in the same acceleration structure and render pass. |
| [09_motion_blur](raytrace_tutorial/09_motion_blur) <br/> ![](/docs/images/09_th.jpg) |**Advanced Motion Blur with Ray Tracing** <br/> - Demonstrates **three types of motion blur** using the `VK_NV_ray_tracing_motion_blur` extension:<br/>  - **Matrix Motion**: Object transformation interpolation (translation/rotation) using motion matrices.<br/>  - **SRT Motion**: Scale-Rotate-Translate transformation with separate motion data structures.<br/>  - **Vertex Motion**: Morphing geometry where vertex positions interpolate between T0 and T1 states.<br/>- Implements **temporal ray tracing** with varying ray time parameters for smooth motion blur effects.<br/>- Features **multi-sample accumulation** with user-controllable sample count for motion blur quality.<br/>- Uses **low-level acceleration structure building** with motion-enabled BLAS and TLAS construction.<br/>- Includes **random time generation** in shaders for consistent temporal sampling across primary and shadow rays.<br/>- Demonstrates **advanced pipeline setup** with motion blur flags and specialized instance data structures. |
| [10_position_fetch](raytrace_tutorial/10_position_fetch) ![](/docs/images/10_th.jpg) | **Position fetch extension** <br/> Demonstrates the use of `VK_KHR_ray_tracing_position_fetch` extension to retrieve vertex positions directly from the acceleration structure during ray traversal. This reduces memory usage by eliminating the need for additional vertex buffers during rendering.<br/>**Key Features:**<br/>- Direct position access using `HitTriangleVertexPosition()`<br/>- Memory-efficient rendering without separate vertex buffers  <br/>- Geometric normal calculation from fetched positions<br/>- BLAS configuration with `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR`<br/>**Extension Requirements:**<br/>- `VK_KHR_ray_tracing_position_fetch`<br/>- Hardware support (RTX 20 series and newer)|
| [11_shader_execution_reorder](raytrace_tutorial/11_shader_execution_reorder) <br/> ![](/docs/images/11_th.jpg) | **Shader Execution Reorder (SER) for Performance Optimization**<br/>- Introduces **Shader Execution Reorder (SER)** using the `VK_NV_ray_tracing_invocation_reorder` extension to reduce execution divergence in ray tracing shaders.<br/>- Implements **path tracing** (first in the series) with multiple bounces, Russian roulette, and physically-based lighting.<br/>- Uses **specialization constants** (first in the series) for runtime toggling of SER without pipeline recreation.<br/>- Features a **hollow box of spheres** scene with **real-time heatmap visualization** showing execution divergence.<br/>- Demonstrates SER benefits through artificial divergence and performance comparison controls. |
| [12_infinite_plane](raytrace_tutorial/12_infinite_plane) <br/> ![](/docs/images/12_th.jpg) | **Infinite Ground Plane with Interactive Toggle**<br/>- Shows how to add an infinite plane to your ray tracing scene using a custom intersection function.<br/>- Implements a UI toggle to enable/disable the infinite plane in real time.<br/>- Demonstrates proper hit state management and how to distinguish plane hits from scene geometry.<br/>- Explains the importance of checking the plane intersection *after* `TraceRay()` for correct compositing.<br/>- Includes adjustable plane color, height, and material parameters via the UI.<br/>- Serves as a foundation for procedural environment effects and further extensions (multiple planes, procedural textures, etc.). |
| [13_callable_shader](raytrace_tutorial/13_callable_shader) <br/> ![](/docs/images/13_th.jpg) | **Callable Shaders for Modular Material Systems**<br/>- Introduces **callable shaders** to create modular, reusable shader functions that can be invoked from other ray tracing shaders.<br/>- Demonstrates a **material system** with four different materials (diffuse, plastic, glass, constant) implemented as separate callable shaders.<br/>- Implements **procedural textures** (noise, checker, voronoi) as callable shaders with different payload structures.<br/>- Shows how to **avoid expensive branching** by using `instanceCustomIndex` in the TLAS for material selection.<br/>- Features **dynamic material assignment** with UI controls that trigger TLAS recreation when materials change.<br/>- Explains the **performance trade-offs**: material changes require TLAS rebuild, texture changes use push constants.<br/>- Demonstrates **payload flexibility** with different structures for materials vs. textures.<br/>- Includes **animation support** for procedural textures with time-based effects. |
| [14_animation](raytrace_tutorial/14_animation)<br/> ![](/docs/images/14_ath.gif) | **Real-Time Animation in Ray Tracing**<br/>- Demonstrates **two types of animation** in ray tracing environments:<br/>  - **Instance Animation**: Multiple Wuson models rotating in a circle using transformation matrix updates<br/>  - **Geometry Animation**: Sphere deformation using compute shaders with radial wave effects<br/>- Implements **acceleration structure updates** with `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR`:<br/>  - **TLAS updates** for instance animation (transformation changes) <br/>  - **BLAS updates** for geometry animation (vertex modifications)<br/>- Features **compute shader vertex animation** that modifies vertex positions and normals in-place on the GPU<br/>- Uses **sine wave mathematics** for realistic wave deformation with proper normal recalculation<br/>- Includes **real-time UI controls** for toggling animations and adjusting speed<br/>- Demonstrates **proper synchronization** between compute operations and ray tracing with memory barriers<br/>- Shows **efficient update strategies** using `cmdUpdateAccelerationStructure` instead of full rebuilds |
| [15_micro_maps_opacity](raytrace_tutorial/15_micro_maps_opacity) <br/> ![](/docs/images/15_th.jpg) | **Opacity Micro-Maps in Ray Tracing**<br/>- Demonstrates **Opacity Micro-Maps** implementation in Vulkan for efficient ray tracing<br/>- Implements **selective AnyHit shader invocation** based on opacity state<br/>- Features **radius-based opacity testing** with real-time UI controls<br/>- Uses **VkMicromapEXT** structures for triangle subdivision and opacity encoding<br/>- Supports **2-state and 4-state micro-map formats** with dynamic switching<br/>- Includes **6x6 triangle plane** geometry for micro-maps demonstration<br/>- Provides **real-time radius adjustment** without acceleration structure rebuilds<br/>- Shows **hardware-accelerated opacity testing** with dedicated micro-map support |
| [16_ray_query](raytrace_tutorial/16_ray_query)<br/>![](/docs/images/16_th.jpg) | **Ray Queries - Inline Ray Tracing in Compute Shaders**<br/>- Demonstrates Vulkan's `VK_KHR_ray_query` extension for inline ray tracing directly in compute shaders<br/>- Replaces the traditional ray tracing pipeline with a compute shader using `RayQuery<>` objects<br/>- All ray intersection and shading logic is handled procedurally in a single shader<br/>- Supports **Monte Carlo path tracing**, temporal accumulation, and physically-based materials|
| [17_ray_query_screenspace](raytrace_tutorial/17_ray_query_screenspace)<br/>![](/docs/images/17_th.jpg) | **Screen-Space Ray Queries**<br/>- Uses compute shader ray queries for effects like **ambient occlusion**<br/>- Integrates ray tracing with rasterization via G-buffer<br/>- Real-time, single-ray effects with adjustable **AO**<br/>- Efficient: processes only visible pixels |
| [18_swept_spheres](raytrace_tutorial/18_swept_spheres)<br/>![](/docs/images/18_th.jpg) | **Linear Swept Spheres and Sphere Primitives (NVIDIA ONLY)**<br/>- Demonstrates the `VK_NV_ray_tracing_linear_swept_spheres` extension for efficient strand-based geometry<br/>- Introduces **two new geometric primitives**: Linear Swept Spheres (LSS) and Spheres<br/>- Implements **three rendering modes**:<br/>  - **Grass field**: Dense grass using LIST indexing mode (independent strands)<br/>  - **Standalone spheres**: Decorative spheres with varying radii<br/>  - **Multi-segment chains**: Connected chains using SUCCESSIVE indexing mode<br/>- Perfect for rendering grass, hair, fur, and other thin cylindrical objects |
| xx_ray_indirect  | comming soon  |
| xx_wireframe  | comming soon  |
| xx_partition_tlas  | comming soon  |
| xx_clusters  | comming soon  |
| xx_particles_large_accel | comming soon |
| xx_volumetric_rendering | comming soon |

---


### Key Features

- **Progressive Learning Path**: Each tutorial builds naturally on the previous one with minimal, focused changes
- **Industry-Standard Practices**: Learn the same techniques used in professional ray tracing applications
- **Modern Vulkan 1.4**: Utilize current features including shader objects, push descriptors, and advanced ray tracing extensions
- **Real-World Content**: glTF scene support enables integration with existing 3D content pipelines
- **Performance-Focused**: Every implementation prioritizes efficiency and real-time performance
- **Cross-Platform Compatibility**: Slang shaders ensure compatibility across different platforms and hardware

---


## Production Ray Tracing Implementation

For developers ready to explore a complete, production-ready Vulkan ray/path tracer implementation:

### [vk_gltf_renderer](https://github.com/nvpro-samples/vk_gltf_renderer)

A comprehensive, full-featured Vulkan renderer showcasing advanced ray tracing, path tracing, and physically based rendering with robust glTF 2.0 support:

- **Production-Quality Features:**  
  - Physically Based Rendering (PBR) with image-based lighting
  - Real-time and offline path tracing modes
  - Ray-traced reflections, shadows, and global illumination
  - Complete glTF 2.0 support including animations and cameras
  - Advanced post-processing: denoising, tone mapping, and effects
  - Interactive UI for scene and renderer configuration
  - Extensive Vulkan 1.3+ and ray tracing extension usage

- **Reference Implementation:**  
  Well-documented and modular architecture provides an excellent reference for studying complete Vulkan ray/path tracer implementations and building production renderers.


## Additional Vulkan Learning Resources

This tutorial series is inspired by the [vk_raytracing_tutorial_KHR](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR) but focuses on modern Vulkan 1.4 implementation rather than educational content. 

### [vk_mini_samples](https://github.com/nvpro-samples/vk_mini_samples)

A comprehensive collection covering all aspects of Vulkan development beyond ray tracing:

- **Comprehensive Coverage:**  
  - Basic rendering, compute shaders, and graphics pipelines
  - Advanced features like mesh shading, ray queries, and shader objects
  - Performance analysis, debugging, and optimization techniques
  - Real-time rendering, post-processing, and modern Vulkan features

- **Learning-Focused Design:**  
  - Extensive collection of focused samples with clear documentation
  - Progressive complexity from basic to advanced concepts
  - Consistent architecture using the nvpro_core2 framework
  - Complements the ray tracing tutorials
