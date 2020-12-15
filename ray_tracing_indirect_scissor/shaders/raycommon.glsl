#include "LanternIndirectEntry.glsl"

struct hitPayload
{
  vec3 hitValue;
  bool additiveBlending;
};

layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;         // 0: point, 1: infinite
  int   lanternPassNumber; // -1 if this is the full-screen pass. Otherwise, used to lookup trace indirect parameters.
  int   screenX;
  int   screenY;
  int   lanternDebug;
}
pushC;