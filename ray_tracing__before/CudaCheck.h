#pragma once

#include <cuda_runtime.h>
#include <iostream>

__host__ void cudaCheck(cudaError_t err)
{
    if (err != cudaSuccess)
    {
        std::cout << cudaGetErrorString(err) << std::endl;
        exit(-1);
    }
}