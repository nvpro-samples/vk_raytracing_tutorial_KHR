# 10 Position Fetch - Tutorial

![10 Position fetch extension result](/docs/images/10.png)

This tutorial demonstrates the `VK_KHR_ray_tracing_position_fetch` extension, which allows retrieving vertex positions directly from the acceleration structure during ray traversal. This eliminates the need for separate vertex buffers during rendering, reducing memory usage and simplifying data management. The main learning objective is to understand how to use position fetch for memory-efficient ray tracing with proper feature detection and graceful hardware compatibility handling.

## Key Changes from 02_basic.cpp

### 1. Extension Setup and Feature Detection
**Modified: `10_position_fetch.cpp`**
- Added `VK_KHR_ray_tracing_position_fetch` extension support with proper feature detection
- Implemented runtime hardware capability checking to gracefully handle unsupported devices
- Added UI status indicators showing "SUPPORTED" or "ERROR" based on hardware capabilities

```cpp
// Feature detection for position fetch support
VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR m_rtPosFetch{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR
};

// Runtime validation
if(m_rtPosFetch.rayTracingPositionFetch == VK_FALSE)
{
  // Handle unsupported hardware gracefully
  return;
}
```

### 2. Acceleration Structure Configuration
**Modified: `createBottomLevelAS()` function**
- Added `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR` flag to enable position data access
- This flag allows the acceleration structure to retain vertex data for position fetch operations

```cpp
// Enable data access for position fetch
m_asBuilder.blasSubmitBuildAndWait(geoInfos, 
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR);
```

### 3. Shader Changes
**Modified: `10_position_fetch.slang`**
- Replaced vertex buffer access with `HitTriangleVertexPosition()` built-in function
- Implemented geometric normal calculation from fetched vertex positions
- Added position fetch visualization with subtle purple tint

```glsl
// Core position fetch functionality
const float3 pos0 = HitTriangleVertexPosition(0);  // First vertex
const float3 pos1 = HitTriangleVertexPosition(1);  // Second vertex  
const float3 pos2 = HitTriangleVertexPosition(2);  // Third vertex

// Calculate geometric normal from fetched positions
const float3 edge1 = pos1 - pos0;
const float3 edge2 = pos2 - pos0;
const float3 geoNormal = normalize(cross(edge1, edge2));
```

### 4. UI Enhancements
**Modified: User interface system**
- Added real-time material parameter controls (metallic/roughness sliders)
- Implemented feature status display with color-coded support indicators
- Added reset functionality to restore default material values

## How It Works

Position fetch works by enabling direct access to vertex data stored within the acceleration structure itself. Instead of maintaining separate vertex buffers for shading calculations, the `HitTriangleVertexPosition()` function retrieves vertex positions directly from the BLAS during ray traversal.

The key insight is that the acceleration structure already contains the vertex data needed for intersection testing. By building the BLAS with the `ALLOW_DATA_ACCESS` flag, this data becomes accessible to shaders, eliminating the need for duplicate vertex storage.

## Benefits

- **Memory Efficiency**: Eliminates redundant vertex buffer storage, reducing memory footprint
- **Simplified Data Management**: Single source of truth for geometry data in the acceleration structure
- **Cache Efficiency**: Only fetches vertex data when actually needed during ray traversal
- **Bandwidth Reduction**: Reduces memory bandwidth requirements for vertex data access

## Technical Details

### Hardware Requirements
- Vulkan 1.2+ with ray tracing support
- GPU supporting `VK_KHR_ray_tracing_position_fetch` extension
- Recent NVIDIA RTX GPUs (RTX 20 series and newer)

### Performance Considerations
- **Advantages**: Reduced memory usage, simplified management, improved cache efficiency
- **Trade-offs**: Requires hardware extension support, limited to position data only, slight BLAS build time increase
- **Best Practice**: Always include runtime feature detection for graceful degradation

### Limitations
- Only vertex positions are directly accessible (normals, UVs require separate storage or calculation)
- Requires modern hardware with extension support
- BLAS build time may increase slightly due to data retention

## Usage Instructions

1. **Hardware Check**: The tutorial automatically detects position fetch support and displays status
2. **Material Controls**: Use the metallic and roughness sliders to adjust material properties in real-time
3. **Visual Feedback**: Look for the subtle purple tint on rendered objects indicating position fetch is active
4. **Error Handling**: If hardware doesn't support the extension, clear error messages are displayed

## Advanced Applications

Position fetch enables several advanced rendering techniques:
- Memory-constrained ray tracing scenarios
- Displacement mapping without vertex buffer expansion
- Adaptive level-of-detail based on ray distance
- Procedural geometry with minimal memory overhead
- Combined with compressed attribute storage for maximum efficiency

For more information, see:
- [Vulkan Ray Tracing Position Fetch Extension](https://www.khronos.org/blog/introducing-vulkan-ray-tracing-position-fetch-extension)
- [Vulkan Specification - VK_KHR_ray_tracing_position_fetch](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_ray_tracing_position_fetch.html)
