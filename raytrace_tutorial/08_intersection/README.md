# 08 Intersection - Tutorial
![08 Intersection shader procedural primitives result](/docs/images/08.png)

This tutorial demonstrates how to use intersection shaders to render implicit primitives (spheres and cubes) alongside traditional triangle geometry. It introduces the concept of procedural hit groups and custom ray-primitive intersection tests, enabling ray tracing of mathematically defined shapes without requiring explicit triangle meshes.

## Key Changes from 02_basic.cpp

### 1. Shader Changes
**Modified: `shaderio.h`**
- Added new binding point `eImplicit` for implicit object data
- Introduced `ImplicitObjectKind` enum to distinguish between spheres and cubes
- Added `Sphere` and `Aabb` structures for implicit primitive data

**New: Intersection Shader**
- Created `rintMain()` function that performs custom ray-primitive intersection tests
- Implements both ray-sphere and ray-AABB intersection algorithms
- Uses `ReportHit()` to communicate intersection results to the ray tracing pipeline

```glsl
[shader("intersection")]
void rintMain()
{
  float3 rayOrigin = WorldRayOrigin();
  float3 rayDirection = WorldRayDirection();
  Sphere sphere = allSpheres[PrimitiveIndex()];
  
  // Determine primitive type and perform intersection test
  int hitKind = PrimitiveIndex() % 2 == 0 ? eSphere : eCube;
  float tHit = (hitKind == eSphere) ? hitSphere(sphere, rayOrigin, rayDirection) 
                                   : hitAabb(sphere, rayOrigin, rayDirection);
  
  if(tHit > 0)
  {
    BuiltInTriangleIntersectionAttributes attr;
    ReportHit(tHit, hitKind, attr);
  }
}
```

**Modified: Closest Hit Shader**
- Added `rchitMain2()` for implicit object shading
- Calculates analytical normals for spheres and cubes
- Handles different material properties based on primitive type

### 2. C++ Application Changes
**Modified: `08_intersection.cpp`**
- Added member variables for implicit object storage and buffers
- Implemented `createSpheres()` function to generate 2M implicit primitives
- Created AABB-based acceleration structure using `VK_GEOMETRY_TYPE_AABBS_KHR`

**Pipeline Changes:**
- Added procedural hit group combining intersection and closest hit shaders
- Configured shader group type as `VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR`

```cpp
// Procedural hit group for implicit objects
group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
group.closestHitShader = eClosestHit2;
group.intersectionShader = eIntersection;
```

### 3. Data Structure Changes
**New Buffer Types:**
- `m_spheresBuffer`: Storage buffer containing sphere data
- `m_spheresAabbBuffer`: AABB data for acceleration structure building
- `m_spheresMatColorBuffer`: Material properties for implicit objects
- `m_spheresMatIndexBuffer`: Material index mapping

**Descriptor Binding:**
- Added `eImplicit` binding point for implicit object data access
- Updated descriptor layout to include storage buffer for sphere data

## How It Works

Intersection shaders extend ray tracing beyond triangle geometry by allowing custom ray-primitive intersection tests. Instead of relying on hardware triangle intersection, the intersection shader performs mathematical calculations to determine if a ray intersects with implicit primitives like spheres or boxes.

The process works as follows:
1. **AABB Culling**: The acceleration structure uses axis-aligned bounding boxes to quickly cull non-intersecting primitives
2. **Custom Intersection**: When a ray potentially intersects an AABB, the intersection shader performs the actual geometric test
3. **Hit Reporting**: If an intersection is found, the shader reports the hit distance and primitive type
4. **Shading**: The closest hit shader calculates lighting using analytical normals and material properties

## Benefits

- **Memory Efficiency**: Implicit primitives require only mathematical parameters (center, radius) rather than full triangle meshes
- **Scalability**: Can render millions of simple shapes with minimal memory overhead
- **Flexibility**: Supports any mathematically definable primitive through custom intersection tests
- **Performance**: AABB-based acceleration structures provide efficient culling for implicit objects

## Technical Details

**Intersection Algorithms:**
- **Ray-Sphere**: Uses quadratic formula to solve intersection equation
- **Ray-AABB**: Implements slab method for efficient box intersection testing

**Normal Calculation:**
- **Spheres**: Normal = normalize(hitPoint - sphereCenter)
- **Cubes**: Normal aligned to the major axis of the intersection face

**Performance Considerations:**
- Intersection shaders add computational overhead compared to hardware triangle intersection
- AABB culling is crucial for performance with large numbers of implicit objects
- Memory bandwidth is reduced since only primitive parameters are stored, not full geometry

## Usage Instructions

The tutorial automatically generates 2,000,000 implicit objects with:
- Random positions following normal distribution
- Varying radii between 0.05 and 0.2 units
- Alternating materials (cyan and yellow)
- Mixed sphere and cube primitives

The scene demonstrates the scalability of intersection shaders while maintaining interactive frame rates through efficient acceleration structure culling.
