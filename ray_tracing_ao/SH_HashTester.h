#pragma once
#include<array>
#include<functional>
#include "SH_includes.h"
#include<nvmath/nvmath.h>

struct TestCell
{
  uint32_t counter;
  std::vector<nvmath::vec3f> values{};
  std::vector<int32_t> checksum{};
};

class HashTester
{
public:
  HashTester()
  { checksumTester = new std::unordered_map<uint32_t, TestCell>{};
  }
  ~HashTester()
  {
    delete checksumTester;
  }
  void startTester();
  void test_and_print_result(std::function<uint32_t(int32_t)> hash_function);


private:
  std::unordered_map<uint32_t, TestCell>* checksumTester;

  void printChecksum();

  static uint32_t h0(float x);
  static uint32_t h1(int32_t x);
  static uint32_t h2(float x);

  static std::array<uint32_t, 4> split_bytes(uint32_t x);

  static uint32_t H4D_SWD(nvmath::vec3f position, uint32_t s_wd, std::function<uint32_t(float)> hash_function);
  static uint32_t H4D(ConfigurationValues c, nvmath::vec3f position, std::function<uint32_t(float)> hash_function);
  static uint32_t H7D(ConfigurationValues c, nvmath::vec3f position, nvmath::vec3f normal, std::function<uint32_t(float)> hash_function);


  

};
