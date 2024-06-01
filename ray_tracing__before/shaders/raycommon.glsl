
struct hitPayload
{
  vec3 hitValue;
  uint seed;
  // Track the depth and the attenuation of the ray
  int  depth;
  vec3 attenuation;
  // Enhance the structure to add information to start new rays if wanted
  int  done;
  vec3 rayOrigin;
  vec3 rayDir;
  // Refraction variables
  bool isRefracted; // True if it is a refracted ray
  vec3 lastParticleCollision;
  vec3 lastPartCollNormal;
  int  particleCollisionCount;
};

struct shadowPayload
{
  bool isHit;
  uint seed;

  bool isRefracted;  // True if it is a refracted ray
  vec3 lastParticleCollision;
  vec3 lastPartCollNormal;
  int  particleCollisionCount;
};