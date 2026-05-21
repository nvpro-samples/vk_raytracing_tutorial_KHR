# NVIDIA Vulkan Ray Tracing Tutorials (v2.0)

![Vulkan Ray Tracing Tutorial](images/tuto.png)

**Convert a modern Vulkan 1.4 rasterization application into a fully functional ray tracer through 8 progressive, compilable phases.** Learn acceleration structures, the shader binding table, ray tracing pipelines, and physically-based shading by following along step by step.

<div class="hero-cta" markdown>

[Start the Progressive Tutorial →](tutorial/index.md){ .md-button .md-button--primary }
[Browse the 20+ samples](samples/index.md){ .md-button }

</div>

| Before (rasterization) | After (ray tracing) |
| ---------------------- | ------------------- |
| ![Phase 0](images/01.png) | ![Phase 8](images/02.png) |

Built on the [`VK_KHR_acceleration_structure`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_acceleration_structure), [`VK_KHR_ray_tracing_pipeline`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_tracing_pipeline), and [`VK_KHR_ray_query`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_query) extensions.

!!! tip "Looking for the original version?"
    Check out the [legacy Vulkan ray tracing tutorial (pre-v2.0)](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/tree/master).

## Other Resources

<div class="grid cards" markdown>

-   __Setup__

    ---

    Build prerequisites, repository layout, and how to create a working copy.

    [Setup guide →](getting-started/setup.md)

-   __Concepts__

    ---

    Plain-language explanations of acceleration structures, the SBT, and rendering acronyms (BRDF, BSDF, NEE, MIS).

    [Read the concepts →](concepts/rendering.md)

-   __Samples__

    ---

    20+ focused samples covering reflections, motion blur, ray queries, callable shaders, and more.

    [Browse samples →](samples/index.md)

-   __Reference Implementation__

    ---

    A production-quality Vulkan path tracer with full glTF 2.0 support to study after the tutorial.

    [vk_gltf_renderer →](https://github.com/nvpro-samples/vk_gltf_renderer)

</div>

## Quick Start

### Prerequisites

- [nvpro_core2](https://github.com/nvpro-samples/nvpro_core2): Vulkan helper classes and utilities
- [Vulkan 1.4+ SDK](https://vulkan.lunarg.com/sdk/home) with **Volk headers** selected during installation
- A GPU and driver supporting Vulkan ray tracing
- [CMake](https://cmake.org/download/) 3.18+

### Build

```bash
git clone https://github.com/nvpro-samples/nvpro_core2.git
git clone https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR.git

cmake -B build -S vk_raytracing_tutorial_KHR
cmake --build build -j 8
```

Compiled binaries land in the `_bin` directory.

## What You Will Learn

- **Progressive Conversion**: Transform `01_foundation` (raster-based) into `02_basic` (ray tracing) through 8 carefully designed phases.
- **Compilable Checkpoints**: Each phase results in a working, testable application.
- **Industry Implementation**: Acceleration structures, shader binding tables, and ray tracing pipelines as used in real engines.
- **Modern Vulkan 1.4**: Shader objects, push descriptors, dynamic rendering, and the latest ray tracing extensions.
- **Cross-Platform Shaders**: Slang shaders compiled across hardware and platforms.

## Related Projects

- **[vk_gltf_renderer](https://github.com/nvpro-samples/vk_gltf_renderer)** — A complete, production-ready Vulkan ray/path tracer with full glTF 2.0 support, IBL, denoising, and post-processing. Excellent reference for studying a finished implementation.
- **[vk_mini_samples](https://github.com/nvpro-samples/vk_mini_samples)** — A wide collection of focused Vulkan samples beyond ray tracing: compute, mesh shading, debugging, and modern Vulkan features.

## Repository

Source: [github.com/nvpro-samples/vk_raytracing_tutorial_KHR](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR)

Author: Martin-Karl Lefrançois, NVIDIA
