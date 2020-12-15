struct LanternIndirectEntry
{
  // VkTraceRaysIndirectCommandKHR
  int indirectWidth;
  int indirectHeight;
  int indirectDepth;

  // Pixel coordinate of scissor rect upper-left.
  int offsetX;
  int offsetY;

  // Lantern starts here:
  // Can't use vec3 due to alignment.
  float x, y, z;
  float red, green, blue;
  float brightness;
  float radius;
};
