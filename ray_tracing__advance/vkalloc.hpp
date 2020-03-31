
//#define ALLOC_DEDICATED
#define ALLOC_DMA
//#define ALLOC_VMA

#if defined(ALLOC_DEDICATED)
#include "nvvkpp/allocator_dedicated_vkpp.hpp"
using nvvkBuffer    = nvvkpp::BufferDedicated;
using nvvkTexture   = nvvkpp::TextureDedicated;
using nvvkAllocator = nvvkpp::AllocatorDedicated;
#elif defined(ALLOC_DMA)
#include "nvvkpp/allocator_dma_vkpp.hpp"
using nvvkBuffer       = nvvkpp::BufferDma;
using nvvkTexture      = nvvkpp::TextureDma;
using nvvkAllocator    = nvvkpp::AllocatorDma;
using nvvkMemAllocator = nvvk::DeviceMemoryAllocator;

#elif defined(ALLOC_VMA)
#include "nvvkpp/allocator_vma_vkpp.hpp"
using nvvkBuffer       = nvvkpp::BufferVma;
using nvvkTexture      = nvvkpp::TextureVma;
using nvvkAllocator    = nvvkpp::AllocatorVma;
using nvvkMemAllocator = VmaAllocator;
#endif
