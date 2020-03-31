struct hitPayload
{
  vec3 hitValue;
  uint seed;
  int  depth;
  vec3 attenuation;
  int  done;
  vec3 rayOrigin;
  vec3 rayDir;
};


struct rayLight
{
  vec3  inHitPosition;
  float outLightDistance;
  vec3  outLightDir;
  float outIntensity;
};

struct Implicit
{
  vec3 minimum;
  vec3 maximum;
  int  objType;
  int  matId;
};

struct Sphere
{
  vec3  center;
  float radius;
};

struct Aabb
{
  vec3 minimum;
  vec3 maximum;
};

#define KIND_SPHERE 0
#define KIND_CUBE 1
