# 06 Reflection - Tutorial
![06 Reflection ray tracing result](/docs/images/06.png)

This tutorial demonstrates how to implement realistic reflections in ray tracing by tracing secondary rays when light bounces off surfaces. It introduces two different approaches: recursive reflection (using hardware recursion) and iterative reflection (using explicit loops), showing the trade-offs between simplicity and scalability.

**Key Takeaway:** This tutorial demonstrates both recursive and iterative reflection approaches. The iterative approach is recommended for production use as it requires only `maxPipelineRayRecursionDepth = 2` regardless of actual reflection depth, uses fewer GPU registers per ray, and is not limited by hardware recursion depth constraints.

## Key Changes from 02_basic.cpp

### 1. Shader Structure and Reflection Modes

**Modified: `shaders/rtreflection.slang`**
The shader supports two reflection implementations controlled by a preprocessor define:
```hlsl
#define USE_RECURSIVE_REFLECTION 0  // 0 = Iterative (recommended), 1 = Recursive
```

### 2. Extended Payload Structure

**Modified: `shaders/rtreflection.slang`**
The hit payload is extended to track reflection state and accumulate results:
```hlsl
struct HitPayload
{
    float3 color;      // Final color for this ray
    float  weight;     // Reflection weight (decreases with each bounce)
    int    depth;      // Current reflection depth
    
    // For iterative reflection
    float3 rayOrigin;    // Origin for next ray iteration
    float3 rayDirection; // Direction for next ray iteration
};
```

### 3. Push Constants

**Modified: `shaders/shaderio.h`**
Added depth control for reflection quality:
```cpp
struct TutoPushConstant
{
    // ... existing members ...
    int depthMax = 3;  // Maximum reflection depth (default: 3)
};
```

### 4. Ray Generation Shader - Iterative Approach

**Modified: `shaders/rtreflection.slang`**
The iterative approach uses a loop in the ray generation shader to handle multiple bounces:
```hlsl
// Iterative reflection loop
while(payload.depth < pushConst.depthMax && payload.weight > 0.01)
{
    float prevWeight = payload.weight;
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
    
    accumulatedColor += payload.color * prevWeight;
    
    // Update ray for next iteration
    ray.Origin = payload.rayOrigin;
    ray.Direction = payload.rayDirection;
}
```

### 5. Closest Hit Shader - Reflection Logic

**Modified: `shaders/rtreflection.slang`**
Both approaches calculate reflection direction and handle material properties:
```hlsl
// Calculate reflection direction
float3 reflectionDir = reflect(-V, N);

// For recursive: create new ray and trace recursively
// For iterative: store ray info in payload for next iteration
payload.rayOrigin = worldPos;
payload.rayDirection = reflectionDir;
payload.weight *= metallic;  // Decrease weight based on material
```

### 6. C++ Application Changes

**Modified: `06_reflection.cpp`**

#### Pipeline Configuration - Critical Optimization
The pipeline recursion depth requirement differs dramatically between the two modes:

```cpp
// Reflection mode (must match shader define)
#define USE_RECURSIVE_REFLECTION 0  // 0 = Iterative (recommended), 1 = Recursive
#define MAX_DEPTH 10U  // Maximum reflections

#if USE_RECURSIVE_REFLECTION
    const uint32_t pipelineDepth = MAX_DEPTH + 2;  // Recursive needs depth = 12
#else
    const uint32_t pipelineDepth = 2;  // Iterative only needs depth = 2
#endif
VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = 
    createRayTracingPipelineCreateInfo(stages, shaderGroups, pipelineDepth);
```

**Explanation:**
- **Iterative (depth=2)**: Loop runs in ray generation shader. Only 2 hardware recursion levels needed (primary → shadow). Reflections handled by loop iterations.
- **Recursive (depth=MAX_DEPTH+2)**: Needs extra depth because each reflection also traces a shadow ray. For 10 reflections: 1 (primary) + 10 (reflections) + 1 (final shadow) = 12 total depth required.
- **Register Impact**: GPU allocates registers for worst-case recursion depth. Lower depth reduces register usage per ray, allowing more concurrent rays.

**Why +2?** Each closest hit shader traces both a shadow ray and potentially a reflection ray. At reflection depth N, you need pipeline depth of N+2 to account for the primary ray and shadow rays at each level.

#### UI Controls
Added reflection depth slider for quality control:
```cpp
PE::SliderInt("Reflection Depth", &m_pushValues.depthMax, 1, MAX_DEPTH, "%d", 
              ImGuiSliderFlags_AlwaysClamp, "Maximum reflection depth");
```

### 7. Scene Setup

**Modified: `createScene()`**
- Uses `wuson.glb` model with metallic materials for better reflection demonstration
- Configures mirror surfaces and sky environment for visible reflections
- Sets up directional lighting to create shadows and highlights

## How It Works

### Recursive Approach (Educational Only)
The recursive method uses hardware ray recursion where each hit shader can spawn new rays:
1. **Primary Ray**: Hits surface and calculates direct lighting
2. **Reflection Check**: If material is metallic, calculates reflection direction
3. **Recursive Call**: `TraceRay()` is called recursively from within the closest hit shader
4. **Depth Control**: Stops when depth limit is reached or reflection weight becomes negligible
5. **Hardware Limitation**: **Strictly limited** by GPU's `maxRayRecursionDepth` (typically 4-31 levels)

Each nested `TraceRay()` call adds a hardware recursion level. The shader requests MAX_DEPTH+2 recursion (e.g., 12 for MAX_DEPTH=10: primary + reflections + shadow), but `RtBase::createRayTracingPipelineCreateInfo` clamps this using `std::min(requestedDepth, m_rtProperties.maxRayRecursionDepth)`, so pipeline creation always succeeds. However, if runtime recursion depth exceeds the device's `maxRayRecursionDepth`, ray tracing validation will report a `TRACE_DEPTH_EXCEEDED` error.

### Iterative Approach (Recommended)
The iterative method uses explicit loops in the ray generation shader:
1. **Primary Ray**: Hits surface and calculates direct lighting
2. **Payload Update**: Reflection information is stored in payload for next iteration
3. **Loop Control**: Ray generation shader continues until depth or weight limits
4. **Accumulation**: Each iteration's color is accumulated with previous results
5. **No Hardware Limits**: Can handle arbitrarily deep reflection chains - 10, 50, 100+ bounces!

**Key Insight**: Each iteration calls `TraceRay()` which only goes 2 levels deep (primary + shadow). The GPU allocates registers for depth=2, but the shader can loop as many times as needed for the desired reflection depth.

## Benefits

### Comparison: Iterative vs Recursive

#### Register Efficiency
- **Iterative**: `maxPipelineRayRecursionDepth = 2` (primary + shadow rays only)
- **Recursive**: `maxPipelineRayRecursionDepth = MAX_DEPTH` (full reflection depth required)
- **Impact**: GPU allocates registers based on worst-case recursion depth. Lower depth allows more concurrent rays and better GPU occupancy.
- **Performance**: Register savings improve performance, with exact gains varying by GPU architecture and scene complexity.

#### No Hardware Recursion Limits
- **Iterative**: Can handle arbitrarily deep reflection chains (50, 100+ bounces)
  - Limited only by performance and shader loop limits, not hardware recursion depth
  - Pipeline depth remains at 2 regardless of reflection count
  - Suitable for path tracing, caustics, or effects requiring many bounces
- **Recursive**: Limited by GPU's `maxRayRecursionDepth` property (typically 4-31 levels)
  - Pipeline creation succeeds (depth is clamped to device limit), but runtime recursion exceeding device limit triggers `TRACE_DEPTH_EXCEEDED` validation error
  - Requires pipeline recreation to change maximum depth
  - Runtime depth controlled by hardware capabilities

#### Better Memory Management
- **Iterative**: Uses constant stack depth regardless of reflection count
- **Recursive**: Stack depth grows with each reflection level
- **Predictability**: Avoids stack overflow on complex scenes

#### Additional Benefits
- **Predictable Performance**: Linear performance scaling with reflection depth
- **Easier Debugging**: Linear execution flow is easier to trace and profile
- **Cross-Vendor**: Works identically across all GPU vendors without hardware-specific limitations

### General Benefits
- **Realistic Lighting**: Simulates light bounces for photorealistic rendering
- **Material-Based**: Different materials reflect differently based on metallic/roughness properties
- **Progressive Quality**: More reflection bounces create more realistic lighting
- **Performance Control**: Can limit depth to balance quality vs performance

## Technical Details

### Reflection Quality Control
- **Depth Limit**: `depthMax` parameter controls maximum bounces (1-10, default: 3)
- **Weight Decay**: Reflection contribution decreases with each bounce based on material metallic value
- **Early Termination**: Stops when weight becomes too small (< 0.01) to avoid unnecessary computation
- **Material Threshold**: Only materials with metallic > 0.01 generate reflections

### Performance Considerations

- **Pipeline Depth**: GPU allocates registers based on `maxPipelineRayRecursionDepth`, not actual usage. Lower depth allows more concurrent rays.
- **Computation Cost**: Each reflection level increases computation linearly.
- **Payload Size**: Iterative mode payload is slightly larger (adds rayOrigin, rayDirection) but negligible compared to register savings.
- **Early Termination**: Both modes can terminate early based on material properties (weight < 0.01).
- **Hardware Limits**: Recursive depth varies by GPU vendor (4-31). Iterative has no such limit.

### Material Properties
- **Metallic**: Controls reflection strength (0 = dielectric, 1 = metallic)
- **Roughness**: Controls reflection scatter (0 = mirror, 1 = diffuse)
- **Weight Decay**: Reflection contribution decreases with each bounce

## Ray Tracing Validation

This tutorial includes support for NVIDIA's `VK_NV_ray_tracing_validation` extension to catch ray tracing errors at runtime.

**Enable validation:**
```bash
# Windows: set NV_ALLOW_RAYTRACING_VALIDATION=1
# Linux/macOS: export NV_ALLOW_RAYTRACING_VALIDATION=1
```

### What It Validates

The ray tracing validation extension checks for:
- **Out-of-bounds memory access** in shaders
- **Invalid acceleration structure access** patterns
- **Incorrect SBT (Shader Binding Table)** configurations
- **Ray recursion depth violations**
- **Invalid ray payload usage**

This is particularly useful when debugging the iterative vs recursive reflection implementations, as it can catch:
- Mismatched pipeline depth vs actual recursion depth in recursive mode
- Invalid payload data access patterns
- SBT misconfigurations

**Try it yourself** - Demonstrate the difference between iterative and recursive modes:

In `onUIRender()`, change the slider maximum:
```cpp
// Recursive mode: exceeds pipeline depth → TRACE_DEPTH_EXCEEDED error
PE::SliderInt("Reflection Depth", &m_pushValues.depthMax, 1, MAX_DEPTH+1, ...);

// Iterative mode: works fine, no recursion depth limit
PE::SliderInt("Reflection Depth", &m_pushValues.depthMax, 1, MAX_DEPTH*10, ...);
```

With recursive mode, moving the slider above `MAX_DEPTH` will trigger:
```
TRACE_DEPTH_EXCEEDED Trace depth exceeded. launch index: [184, 60, 0]
```

With iterative mode, the slider can go much higher without errors (limited only by performance).

References: [NVIDIA Blog](https://developer.nvidia.com/blog/ray-tracing-validation-at-the-driver-level/), [Vulkan Spec](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_NV_ray_tracing_validation.html)

## Usage Instructions

### Choosing Between Iterative and Recursive

**For Production Code: Use Iterative (Default)**
- Set `#define USE_RECURSIVE_REFLECTION 0` in both the shader and C++ file
- Pipeline depth will automatically be set to 2
- Enjoy better performance and no hardware limitations

**For Learning/Comparison: Try Recursive**
- Change to `#define USE_RECURSIVE_REFLECTION 1` in both files
- Pipeline depth will be set to MAX_DEPTH (10)
- Observe the performance difference and potential depth limitations

**Important:** The shader and C++ defines must match, or the pipeline depth will be incorrect.

### Runtime Controls

- **Reflection Depth**: Use the UI slider to adjust reflection quality (1-10 bounces default)
  - **Iterative mode**: Changes loop iterations without pipeline recreation. To use higher depths (e.g., 50), change MAX_DEPTH constant.
  - **Recursive mode**: Limited by pipeline depth and hardware `maxRayRecursionDepth`. Requires pipeline recreation to change maximum depth.
- **Material Setup**: Ensure models have proper metallic/roughness values for visible reflections
- **Performance Tuning**: 
  - Lower depth (1-5) for real-time applications
  - Higher depth (10-50+) for offline rendering or path tracing
- **Scene Design**: Include reflective surfaces and interesting geometry for best visual results

## Practical Example: Deep Reflections

Example showing how the two modes handle 50 reflection bounces:

**Iterative Mode:**
```cpp
#define USE_RECURSIVE_REFLECTION 0
#define MAX_DEPTH 50U

// Pipeline depth remains 2, regardless of MAX_DEPTH
const uint32_t pipelineDepth = 2;

// Shader loop handles 50 iterations
while(payload.depth < 50 && payload.weight > 0.01) {
    TraceRay(...);  // Each call uses depth=2
    depth++;
}
```

**Recursive Mode:**
```cpp
#define USE_RECURSIVE_REFLECTION 1
#define MAX_DEPTH 50U

const uint32_t pipelineDepth = MAX_DEPTH + 2;  // Requests depth=52

// Pipeline creation succeeds (clamped to GPU maxRayRecursionDepth via std::min)
// Most GPUs support maxRayRecursionDepth between 8-31
// Runtime: exceeding device limit triggers TRACE_DEPTH_EXCEEDED validation error
// Performance cost: registers allocated for min(52, maxRayRecursionDepth) levels
```

## Summary

Key difference between the two approaches:

**Recursive Mode:**
- `maxPipelineRayRecursionDepth` must equal desired reflection depth
- Allocates registers for full recursion depth per ray
- Limited by GPU hardware recursion capabilities

**Iterative Mode (Recommended):**
- `maxPipelineRayRecursionDepth = 2` (fixed, regardless of reflection count)
- Handles reflections in ray generation shader loop
- No hardware recursion depth limits

Verify that shader and C++ defines match:
- Shader: `rtreflection.slang` `#define USE_RECURSIVE_REFLECTION 0`
- C++ code: `06_reflection.cpp` `#define USE_RECURSIVE_REFLECTION 0`

## Next Steps

This reflection technique can be extended to:
- **Refraction**: Add transmission through transparent materials (also use iterative approach!)
- **Glossy Reflections**: Implement roughness-based reflection blur
- **Fresnel Effects**: Add realistic reflection based on viewing angle
- **Global Illumination**: Combine with other lighting effects for complete lighting simulation
- **Path Tracing**: Extend iterative approach to full path tracing with importance sampling
