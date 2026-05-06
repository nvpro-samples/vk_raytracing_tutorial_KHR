![logo](http://nvidianews.nvidia.com/_ir/219/20157/NV_Designworks_logo_horizontal_greenblack.png)

# NVIDIA Vulkan Ray Tracing Tutorials (v2.0)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4%2B-AC162C?logo=vulkan&logoColor=white)](https://www.vulkan.org/)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
[![CMake](https://img.shields.io/badge/CMake-3.18%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![Shaders](https://img.shields.io/badge/Shaders-Slang-7B42BC)](https://shader-slang.com/)
[![Documentation](https://img.shields.io/badge/docs-online-brightgreen)](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/)


![resultRaytraceShadowMedieval](docs/images/tuto.png)

**Convert a modern Vulkan 1.4 rasterization application into a fully functional ray tracer through 8 progressive, compilable phases**, then explore 18+ focused samples covering reflections, motion blur, ray queries, callable shaders, opacity micro-maps, and more. Built on the [`VK_KHR_acceleration_structure`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_acceleration_structure), [`VK_KHR_ray_tracing_pipeline`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_tracing_pipeline), and [`VK_KHR_ray_query`](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VK_KHR_ray_query) extensions.

---

<div align="center">

### [**Start the Progressive Tutorial**](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/tutorial/) &nbsp;·&nbsp; [Browse Samples](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/samples/) &nbsp;·&nbsp; [Documentation](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/)

| Before (rasterization) | After (ray tracing) |
| :---: | :---: |
| <img src="docs/images/01.png" alt="Phase 0 raster" width="380"/> | <img src="docs/images/02.png" alt="Phase 8 ray traced" width="380"/> |

</div>

---

## Quick Start

**Prerequisites**

- [nvpro_core2](https://github.com/nvpro-samples/nvpro_core2) (Vulkan helpers)
- [Vulkan 1.4+ SDK](https://vulkan.lunarg.com/sdk/home) — select **Volk headers** during installation
- [CMake](https://cmake.org/download/) 3.18+
- A GPU and driver supporting Vulkan ray tracing

**Build**

From an empty parent directory that will hold both repos side-by-side:

```bash
git clone https://github.com/nvpro-samples/nvpro_core2.git
git clone https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR.git

cd vk_raytracing_tutorial_KHR
cmake -B build -S .
cmake --build build -j 8
```

Compiled binaries are placed in the `_bin` directory.

## Looking for the Original?

The legacy pre-v2.0 tutorial is on the [`master` branch](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/tree/master).

## Related Projects

- [**vk_gltf_renderer**](https://github.com/nvpro-samples/vk_gltf_renderer) — Production-ready Vulkan ray/path tracer with full glTF 2.0 support, IBL, denoising, and post-processing.
- [**vk_mini_samples**](https://github.com/nvpro-samples/vk_mini_samples) — Comprehensive collection of focused Vulkan samples beyond ray tracing.
