/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

// Set the float value on 11 bit for packing
inline auto floatToR11 = [](float val) { return static_cast<uint16_t>(val * ((1 << 11) - 1)); };  // Mult by 2047


//--------------------------------------------------------------------------------------------------
// The BitPacker will store a value on n-bits, each push will append the next value.
// This is particular useful with the 11 bit packing
class BitPacker
{
public:
  explicit BitPacker(void* data)
      : m_data(static_cast<uint32_t*>(data))
  {
  }

  void setData(void* data) { m_data = static_cast<uint32_t*>(data); }

  void push(uint32_t value, uint32_t bits)
  {
    for(uint32_t b = 0; b < bits; b++)
    {
      uint32_t& word = m_data[m_curBit / (8 * sizeof(uint32_t))];
      uint32_t  mask = 1 << (m_curBit % 32);
      if((value & (1 << b)) != 0U)
      {
        word |= mask;
      }
      else
      {
        word &= ~mask;
      }
      m_curBit++;
    }
  }

private:
  uint32_t* m_data;
  uint32_t  m_curBit{0};
};

//--------------------------------------------------------------------------------------------------
// Specialization of BitPacke with 11 bits, typical packing for Micro-Mesh
//
class BitPacker11 : public BitPacker
{
public:
  explicit BitPacker11(void* data)
      : BitPacker(data) {};
  void push(uint32_t value) { BitPacker::push(value, 11); };
  void push(float value) { BitPacker::push(floatToR11(value), 11); };
};
