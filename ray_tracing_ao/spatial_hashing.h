#pragma once
#include "nvmath/nvmath.h"
#include <array>
#include <functional>

#define S_MIN 0.0000000001

#define HASHMAP_SIZE 2048


class SpatialHashing
{

  struct HASHMAP
  {
    float   current_avg;
    uint32_t sample_count;
    uint64_t alignment_padding; //is this needed?
  };


public:
  SpatialHashing()
  {
    h = std::hash<uint32_t>{};
  }

  uint32_t H4D(nvmath::vec3f position, nvmath::vec3f camera_position)
  {
    float  distance = (position - camera_position).norm();
    float  s_w      = distance * std::tanf(std::max(f / R_x, f * R_x / (R_y * R_y)) * s_p);
    size_t s_wd     = std::pow(2, static_cast<uint32_t>(std::log2f(s_w / S_MIN)) * S_MIN);
    return h(s_wd
             + h(static_cast<uint32_t>(position.z / s_wd)
                 + h(static_cast<uint32_t>(position.y / s_wd) + h(static_cast<uint32_t>(position.x / s_wd)))));
  }

  uint32_t H7D(nvmath::vec3f position, nvmath::vec3f normal, nvmath::vec3f camera_position)
  {
    static uint32_t s_nd = 3;

    normal *= s_nd;
    nvmath::vec3ui normal_d{static_cast<uint32_t>(normal.x), static_cast<uint32_t>(normal.y),
                            static_cast<uint32_t>(normal.z)};

    return h(normal_d.z + h(normal_d.y + h(normal_d.x + H4D(position, camera_position))));
  }

  uint32_t checksum(nvmath::vec3f position, nvmath::vec3f normal, nvmath::vec3f camera_position)
  {
    position *= position;
    normal *= normal;
    return H7D(position, normal, camera_position);
  }

  //todo: hash another function as checksum to check for sameness when colliding
  //todo: create hash table and stuff


private:
  std::hash<uint32_t> h;

  uint32_t s_p;  //user-defined level of coarseness in pixel
  uint32_t s_l;  //level of coarseness in world space without distance calc

  uint32_t f;    //camera aperture
  uint32_t R_x;  //screen resolution in X direction
  uint32_t R_y;  //screen resolution in Y direction

};