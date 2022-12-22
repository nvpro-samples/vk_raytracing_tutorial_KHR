#pragma once
#include "nvmath/nvmath.h"

#define HASH_MAP_SIZE 10'000'000
#define S_MIN 0.0000000001

struct HashCell
{
  float    ao_value;  // the averaged ambient occlusion value in the given hash cell
  uint32_t contribution_counter;  // number of samples contributing to value for blending in new values (old * cc/(cc+1) + new * 1 / (cc+1))
  uint32_t checksum;  // checksum for deciding if cell should be resetted or contribution added
  uint32_t replacement_counter;
  float    s_wd;
  float    s_wd_real;
  int      j;
  int      written;
};

struct ConfigurationValues
{
  nvmath::vec3f  camera_position;  // camera position
  float       s_nd;             // normal coarseness
  float       s_p;              // user-defined level of coarseness in pixel
  float			 f;                // camera aperture
  nvmath::vec2ui res;              // screen resolution in pixel
};