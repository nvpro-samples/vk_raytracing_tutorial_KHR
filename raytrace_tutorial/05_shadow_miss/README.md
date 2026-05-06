# 05 Shadow Miss Shader - Tutorial
![05 Shadow miss shader result](/docs/images/05.png)

This tutorial demonstrates how to optimize shadow ray tracing performance by using a dedicated shadow miss shader with a minimal payload structure. Instead of reusing the main miss shader for shadow testing, we create a specialized miss shader that only performs the essential shadow occlusion test, resulting in significant performance improvements.

## Key Changes from 02_basic.cpp

### 1. Shader Changes
**Modified: `shaders/rtshadowmiss.slang`**
- Added a new dedicated shadow miss shader (`rmissShadowMain`) specifically for shadow rays
- Introduced a minimal `ShadowPayload` structure containing only essential shadow testing data
- Modified shadow testing to use the dedicated miss shader group instead of the main miss shader

```cpp
// Minimal payload for shadow rays - only what we need for shadow testing
struct ShadowPayload
{
  int depth;  // Only need to know if we hit something or not
};

[shader("miss")]
void rmissShadowMain(inout ShadowPayload payload)
{
  // Simple shadow miss shader - no complex lighting calculations
  payload.depth = MISS_DEPTH;
}
```

### 2. C++ Application Changes
**Modified: `05_shadow_miss.cpp`**
- Added a new shader stage for the shadow miss shader in the ray tracing pipeline
- Created an additional shader group (group 2) for the shadow miss shader in the SBT
- Updated the pipeline creation to include the dedicated shadow miss shader

```cpp
enum StageIndices
{
  eRaygen,
  eMiss,
  eMissShadow,  // <---- Dedicated shadow miss shader
  eClosestHit,
  eShaderGroupCount
};

// Shadow Miss shader group
group.type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
group.generalShader = eMissShadow;  // <---- Shadow miss shader group
shaderGroups.push_back(group);
```

### 3. Data Structure Changes
**Modified: Shadow Testing Function**
- Updated `testShadow()` function to use the dedicated shadow miss shader (group 1)
- Changed shadow ray tracing to use the minimal `ShadowPayload` instead of the full `HitPayload`

```cpp
// Trace the shadow ray using the dedicated shadow miss shader (group 1)
TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 1,
         shadowRay, shadowPayload);
```

## How It Works

The optimization works by separating shadow ray handling from primary ray handling:

1. **Primary rays** use the main miss shader (`rmissMain`) which handles complex sky rendering and lighting calculations
2. **Shadow rays** use the dedicated shadow miss shader (`rmissShadowMain`) which only sets a simple flag indicating no occlusion occurred
3. **Payload optimization** reduces memory bandwidth by using a minimal payload structure for shadow rays

## Benefits

- **Reduced Memory Bandwidth**: Shadow rays carry only essential data instead of full lighting information
- **Faster Miss Shader Execution**: Shadow miss shader avoids expensive sky and lighting calculations
- **Better Cache Utilization**: Smaller payload structures improve cache efficiency
- **Scalable Performance**: Benefits increase with the number of shadow rays in the scene

## Technical Details

The tutorial uses four shader groups in the Shader Binding Table (SBT):
- **Group 0**: Ray generation shader
- **Group 1**: Main miss shader for primary rays (handles sky rendering)
- **Group 2**: Shadow miss shader for shadow rays (minimal processing)
- **Group 3**: Closest hit shader group

Shadow rays still use `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` to avoid unnecessary hit processing, but now benefit from a specialized miss shader that only performs the essential shadow occlusion test.

## Scene Setup

The tutorial creates a complex scene with multiple Wuson character instances and a ground plane to demonstrate realistic shadow casting and the performance benefits of the dedicated shadow miss shader approach.
