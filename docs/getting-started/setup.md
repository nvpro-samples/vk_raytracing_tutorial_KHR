# Setup Guide

This guide walks you from a clean machine to a working build of the Vulkan Ray Tracing Tutorial, then prepares the working copy you will edit while following the [Progressive Tutorial](../tutorial/index.md).

## Prerequisites

- A GPU and driver that support Vulkan ray tracing (RTX 20-series or newer, recent AMD RDNA2+ also supported for the core extensions).
- [Vulkan 1.4+ SDK](https://vulkan.lunarg.com/sdk/home). During installation, **select the "Volk headers" optional component** for better cross-platform compatibility.
- [CMake](https://cmake.org/download/) 3.18 or newer.
- A C++17-capable compiler:
    - **Windows**: Visual Studio 2022 (Desktop development with C++).
    - **Linux**: GCC 11+ or Clang 14+.
- Git.

## Clone the Repositories

The tutorial depends on [nvpro_core2](https://github.com/nvpro-samples/nvpro_core2). Clone it next to the tutorial repository:

```bash
git clone https://github.com/nvpro-samples/nvpro_core2.git
git clone https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR.git
```

Your folder layout should look like:

```text
parent_folder/
├── nvpro_core2/
└── vk_raytracing_tutorial_KHR/
```

## Initial Build

Configure and build from the tutorial root. CMake automatically discovers the sibling `nvpro_core2` checkout.

```bash
cd vk_raytracing_tutorial_KHR
cmake -B build -S .
cmake --build build -j 8
```

Compiled binaries land in the `_bin` directory. Run any sample, for example:

```bash
# From the project root
./_bin/01_foundation        # Linux/macOS
.\_bin\01_foundation.exe    # Windows
```

If `01_foundation` opens a window and renders a glTF scene, your toolchain is good to go.

!!! tip "Switching branches"
    The `v2` branch hosts this Vulkan 1.4 / nvpro_core2 tutorial. The legacy pre-v2.0 tutorial lives on the [`master` branch](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/tree/master) and uses a different framework.

## Important: Create a Working Copy

Before starting this tutorial, **create a copy of the `01_foundation` directory** to work with:

```bash
# Navigate to the raytrace_tutorial directory
cd raytrace_tutorial
``` 

Create a working copy of the foundation sample

```bash
# Linux
cp -r 01_foundation 01_foundation_copy
# Windows
xcopy 01_foundation 01_foundation_copy /S /E
``` 

## Why Create a Copy?

- **Preserves Original**: Keeps `01_foundation` intact for reference
- **Easy Comparison**: Compare your progress with the original implementation
- **Clean Starting Point**: Fresh copy for each phase
- **Easy Rollback**: Can restart from clean state if needed
- **Reference Material**: Original serves as a working example


## CMakeLists.txt Modification

You need to add the new project to the main CMakeLists.txt. Insert this line after the original foundation project:

```cmake
add_subdirectory(raytrace_tutorial/01_foundation)
add_subdirectory(raytrace_tutorial/01_foundation_copy)
add_subdirectory(raytrace_tutorial/02_basic)
```

## Working Directory Structure

After creating the copy, your directory should look like this:

```
raytrace_tutorial/
├── 01_foundation/                    # Original (don't modify)
│   ├── 01_foundation.cpp
│   ├── shaders/
│   │   ├── foundation.slang
│   │   └── shaderio.h
│   ├── CMakeLists.txt
│   └── README.md
├── 01_foundation_copy/               # Your working copy
│   ├── 01_foundation.cpp             # Main file to modify
│   ├── shaders/                      # Will add rtbasic.slang
│   │   ├── foundation.slang         
│   │   └── shaderio.h
│   ├── CMakeLists.txt
│   └── README.md
├── 02_basic/                         # Reference implementation
│   ├── 02_basic.cpp
│   ├── shaders/
│   │   ├── rtbasic.slang
│   │   └── shaderio.h
│   └── CMakeLists.txt
└── docs/
    └── tutorial/
        └── index.md   # Main progressive tutorial
```

## Build Verification

Since this is a unified build system, you only need to rebuild from the root directory:

```bash
# From the project root directory
cmake -B build -S .
cmake --build build -j 8
```

This will build all projects including both:

- `01_foundation` (original)
- `01_foundation_copy` (your working copy)

Both should produce identical results initially.

## Next

You're ready to start converting `01_foundation_copy` from rasterization to ray tracing.

[Start the Progressive Tutorial →](../tutorial/index.md){ .md-button .md-button--primary }
