//#include "raycommon.hlsl"
//#include "wavefront.hlsl"

struct MyAttrib
{
        float3 attribs;
};

struct Payload
{
   bool isShadowed;
};

[[vk::binding(0,0)]] RaytracingAccelerationStructure topLevelAS;

[[vk::binding(2,1)]] RWStructuredBuffer<sceneDesc> scnDesc;

[[vk::binding(5,1)]] StructuredBuffer<Vertex> vertices[];

[[vk::binding(6,1)]] StructuredBuffer<uint> indices[];


[[vk::binding(1,1)]] StructuredBuffer<WaveFrontMaterial> materials[];

[[vk::binding(3,1)]] Texture2D textures[];
[[vk::binding(3,1)]] SamplerState samplers[];

[[vk::binding(4,1)]] StructuredBuffer<int> matIndex[];

// clang-format on

struct Constants
{
        float4 clearColor;
        float3 lightPosition;
        float lightIntensity;
        int lightType;
};

[[vk::push_constant]] Constants pushC;

[shader("closesthit")]
void main(inout hitPayload prd, in MyAttrib attr)
{
  // Object of this instance
  uint objId = scnDesc[InstanceIndex()].objId;

  // Indices of the triangle
  int3 ind = int3(indices[objId][3 * PrimitiveIndex() + 0],
                    indices[objId][3 * PrimitiveIndex() + 1],
                    indices[objId][3 * PrimitiveIndex() + 2]);
  // Vertex of the triangle
  Vertex v0 = vertices[objId][ind.x];
  Vertex v1 = vertices[objId][ind.y];
  Vertex v2 = vertices[objId][ind.z];

  const float3 barycentrics = float3(1.0 - attr.attribs.x - 
  attr.attribs.y, attr.attribs.x, attr.attribs.y);

  // Computing the normal at hit position
  float3 normal = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + 
  v2.nrm * barycentrics.z;
  // Transforming the normal to world space
  normal = normalize((mul(scnDesc[InstanceIndex()].transfoIT 
           ,float4(normal, 0.0))).xyz);


  // Computing the coordinates of the hit position
  float3 worldPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y 
                    + v2.pos * barycentrics.z;
  // Transforming the position to world space
  worldPos = (mul(scnDesc[InstanceIndex()].transfo, float4(worldPos, 
              1.0))).xyz;

  // Vector toward the light
  float3  L;
  float lightIntensity = pushC.lightIntensity;
  float lightDistance  = 100000.0;

  // Point light
  if(pushC.lightType == 0)
  {
    float3 lDir      = pushC.lightPosition - worldPos;
    lightDistance  = length(lDir);
    lightIntensity = pushC.lightIntensity / (lightDistance * 
                     lightDistance);
    L              = normalize(lDir);
  }
  else  // Directional light
  {
    L = normalize(pushC.lightPosition - float3(0,0,0));
  }

  // Material of the object
  int               matIdx = matIndex[objId][PrimitiveIndex()];
  WaveFrontMaterial mat    = materials[objId][matIdx];


  // Diffuse
  float3 diffuse = computeDiffuse(mat, L, normal);
  if(mat.textureId >= 0)
  {
    uint txtId = mat.textureId + scnDesc[InstanceIndex()].txtOffset;
    float2 texCoord =
        v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + 
                  v2.texCoord * barycentrics.z;
    diffuse *= textures[txtId].SampleLevel(samplers[txtId], texCoord,
            0).xyz;
  }

  float3  specular    = float3(0,0,0);
  float attenuation = 1;

  // Tracing shadow ray only if the light is visible from the surface
  if(dot(normal, L) > 0)
  {
    float tMin   = 0.001;
    float tMax   = lightDistance;
    float3  origin = WorldRayOrigin() + WorldRayDirection() * 
        RayTCurrent();
    float3  rayDir = L;
    uint  flags =
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | 
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

    RayDesc desc;
    desc.Origin = origin;
    desc.Direction = rayDir;
    desc.TMin = tMin;
    desc.TMax = tMax;

    Payload shadowPayload;
    shadowPayload.isShadowed = true;
    TraceRay(topLevelAS,
             flags,
             0xFF,
             0,
             0,
             1,
             desc,
             shadowPayload
    );

    if(shadowPayload.isShadowed)
    {
      attenuation = 0.9;
    }
    else
    {
      // Specular
      specular = computeSpecular(mat, WorldRayDirection(), L, normal);
    }
  }

  prd.hitValue = float3(lightIntensity * attenuation * (diffuse + 
  specular));
}
