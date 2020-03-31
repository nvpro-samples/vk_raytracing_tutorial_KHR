struct hitPayload
{
  vec3 hitValue;
};

struct rayLight
{
  vec3  inHitPosition;
  float outLightDistance;
  vec3  outLightDir;
  float outIntensity;
};
