
# Environment Setup


## Repositories

Besides the current repository, you will also need to clone or download the following repositories:

* [shared_sources](https://github.com/nvpro-samples/shared_sources): The primary framework that all samples depend on.
* [shared_external](https://github.com/nvpro-samples/shared_external): Third party libraries that are provided pre-compiled, mostly for Windows x64 / MSVC.

Cloning all repositories 

~~~~~
git clone https://github.com/nvpro-samples/shared_sources.git
git clone https://github.com/nvpro-samples/shared_external.git
git clone https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR.git
~~~~~

The directory structure should be looking like this:

~~~~
   C:\Vulkan\nvpro-samples
   | 
   +---shared_external
   +---shared_sources
   +---vk_raytracing_tutorial_KHR
   |   +---ray_tracing__simple
   |   +---ray_tracing_...
   |   \---...   
~~~~

See also [build_all](https://github.com/nvpro-samples/build_all) from nvpro-samples.

## Latest Vulkan SDK

This repository tries to always be up to date with the latest Vulkan SDK, therefore we suggest to download and install it.

**Vulkan SDK**: https://vulkan.lunarg.com/sdk/home


## Beta Installation

KHR ray tracing is still in Beta, therefore you will need the latest
Vulkan driver.

**Latest driver**: https://developer.nvidia.com/vulkan-driver


## CMake

The CMakefile will use other makefiles from `shared_sources` and look for Vulkan environment variables for the installation of the SDK. Therefore, it is important to have all the above installed before running Cmake in the 
`vk_raytracing_tutorial_KHR` directory.

**Note:** If you are using your own Vulkan header files, it is possible to overide the default search path.
  Modify `VULKAN > VULKAN_HEADERS_OVERRIDE_INCLUDE_DIR` to the path to beta vulkan headers.

## Starting From Extra Tutorial

All _extra_ tutorials are starting from the end result of the _first tutorial_. The directory of the _extra_ tutorials is the end result of doing it. 

To start the tutorial from the begining.

* Make a copy of the ray_tutorial__simple (backup)
* Follow the tutorial by modifying ray_tutorial__simple