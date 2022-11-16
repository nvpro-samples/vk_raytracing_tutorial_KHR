#pragma once
#include "nvmath/nvmath.h"

#define HASH_MAP_SIZE 100'000

struct HashCell
{
  float    ao_value;  // the averaged ambient occlusion value in the given hash cell
  uint32_t contribution_counter;  // number of samples contributing to value for blending in new values (old * cc/(cc+1) + new * 1 / (cc+1))
  uint32_t checksum;  // checksum for deciding if cell should be resetted or contribution added
};

struct ConfigurationValues
{
  nvmath::vec3f  camera_position;  // camera position
  uint32_t       s_nd;             // normal coarseness
  uint32_t       s_p;              // user-defined level of coarseness in pixel
  float			 f;                // camera aperture
  nvmath::vec2ui res;              // screen resolution in pixel
};