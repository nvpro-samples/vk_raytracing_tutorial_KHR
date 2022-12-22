#include "SH_HashTester.h"
#include <iostream>
#include <cstdlib>
#include <nvmath/nvmath.h>

#define TEST_AMOUNT 100
#define RANGE_FROM_ZERO 10

std::ostream& operator<<(std::ostream& os, const nvmath::vec3f& dt) {
  os << "(" << dt.x << ", " << dt.y << ", " << dt.z << ")";
  return os;
}

void HashTester::startTester() {
  test_and_print_result(HashTester::h1);
}

void HashTester::test_and_print_result(std::function<uint32_t(int32_t)> hash_function)
{

   srand(234234);

  for(int i = 0; i < TEST_AMOUNT; i++)
  {
    float randomx = ((float(rand()) / float(RAND_MAX)) * 2 * RANGE_FROM_ZERO) - RANGE_FROM_ZERO;
    float randomy = ((float(rand()) / float(RAND_MAX)) * 2 * RANGE_FROM_ZERO) - RANGE_FROM_ZERO;
    float randomz = ((float(rand()) / float(RAND_MAX)) * 2 * RANGE_FROM_ZERO) - RANGE_FROM_ZERO;
    nvmath::vec3f randomvec{randomx, randomy, randomz};
    uint32_t      hash = H4D_SWD(randomvec, 1, hash_function);
    (*checksumTester)[hash].counter += 1;
    (*checksumTester)[hash].values.push_back(randomvec);
    (*checksumTester)[hash].checksum.push_back(0);
  }

  printChecksum();

}

void HashTester::printChecksum()
{
  std::cout << "Printing Checksum map of size " << (*checksumTester).size() << ":" << std::endl;
  for each(auto pair in *checksumTester)
  {
    std::cout << "Hash: \t" << pair.first << "\tCount:\t" << pair.second.counter << std::endl;
    for(int i = 0; i < pair.second.counter; ++i)
    {
      std::cout << pair.second.values[i] << "\t";
    }
    std::cout << std::endl;
    for(int i = 0; i < pair.second.counter; ++i)
    {
      std::cout << pair.second.checksum[i] << "\t";
    }
    std::cout << std::endl;

  }
}

std::array<uint32_t, 4> HashTester::split_bytes(uint32_t x) {
  std::array<uint32_t, 4> res;
  res[0]  = (x & 0xFF000000) >> 24;
  res[1]  = (x & 0x00FF0000) >> 16;
  res[2]  = (x & 0x0000FF00) >> 8;
  res[3]  = (x & 0x000000FF);
  return res;
}

// Hash function: murmur something
uint32_t HashTester::h0(float f) {
    uint32_t x = *reinterpret_cast<uint32_t*>(&f);
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

//  Hash Function: Fowler-Noll-Vo (FNV-1 hash 32-bit version)
uint32_t HashTester::h1(int32_t x) {

    const uint32_t FNV_offset_basis = 0x811c9dc5;
    const uint32_t FNV_prime        = 0x01000193;

    auto _encode = [&](uint32_t hash, uint32_t byte) -> uint32_t {
      hash = hash * FNV_prime;
      hash = hash ^ byte;
      return hash;
    };

    auto     bytes = HashTester::split_bytes(x);
    uint32_t hash  = 1;
    for(int i = 0; i < 4; i++)
        hash = _encode(hash, bytes[i]);
    return hash;

}

//  Jenkings Hash Function
uint32_t HashTester::h2(float f) {
    uint32_t x        = *reinterpret_cast<uint32_t*>(&f);
    auto     bytes = HashTester::split_bytes(x);
    uint32_t hash     = 0;

    for(int i = 0; i < 4; i++)
    {
        hash += bytes[i];
        hash += hash << 10;
        hash = hash ^ (hash >> 6);
    }
    hash += hash << 3;
    hash = hash ^ (hash >> 11);
    hash += hash << 15;
    return hash;
}

//function to specifically adress different levels for blurr later ?
uint32_t HashTester::H4D_SWD(nvmath::vec3f position, uint32_t s_wd, std::function<uint32_t(float)> hash_function)
{
    uint32_t step1 = hash_function(int32_t(position.x / s_wd));
    uint32_t step2 = hash_function(int32_t(position.y / s_wd) + step1);
    uint32_t step3 = hash_function(int32_t(position.z / s_wd) + step2);
    uint32_t step4 = hash_function(int32_t(s_wd) + step3);

    std::cout << "x: " << position.x << ", y: " << position.y << ", z: " << position.z << std::endl;
    std::cout << "x: " << (position.x / s_wd) << ", y: " << (position.y / s_wd) << ", z: " << (position.z / s_wd) << std::endl;
    std::cout << "step1: " << step1 << ", step2: " << step2 << ", step3: " << step3 << ", step 4: " << step4 << std::endl;

    return step4 % HASH_MAP_SIZE;
}

//hash function to hash points without normal
uint32_t HashTester::H4D(ConfigurationValues c, nvmath::vec3f position, std::function<uint32_t(float)> hash_function)
{
    float     dis  = (position - c.camera_position).norm();
    float s_w  = dis * std::tanf(std::max((c.f / c.res.x), c.f * c.res.x / float(pow(c.res.y, 2))) * float(c.s_p));
    uint32_t  s_wd = uint32_t(pow(2, uint32_t(std::log2(s_w / S_MIN)) * S_MIN));
    return H4D_SWD(position, s_wd, hash_function);
}

// Actually used all-inclusive function
uint32_t HashTester::H7D(ConfigurationValues c, nvmath::vec3f position, nvmath::vec3f normal, std::function<uint32_t(float)> hash_function)
{
    normal         = normal * float(c.s_nd);
    nvmath::vec3i normal_d = nvmath::vec3i{normal};
    return hash_function(normal_d.z + hash_function(normal_d.y + hash_function(normal_d.x + H4D(c, position, hash_function))))
           % HASH_MAP_SIZE;
}