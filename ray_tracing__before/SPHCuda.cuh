#pragma once

#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "CudaMathOperators.h"
#include "glm/glm.hpp"

void updateParticlesDensityAndPressureCUDA(
    int numParticles,
    double hsq,
    double partMass,
    double poly6K,
    double initialDensity,
    double pressureK,
    glm::vec3* pos_list,
    double* density_list,
    double* pressure_list,
    int* neighboursListData,
    int* neighboursOffsets,
);

void densityAndPressureCUDA(
    glm::vec3* pos_list,
    int* cell_list,
    int* particle_list,
    double* density_list,
    float3 cell_dims,
    float3 min_box_bound,
    int numParticles,
    double hsq,
    float h_inv,
    double partMass,
    double poly6K,
    double initialDensity,
    double pressureK,
    double* pressure_list,
    int threadGroupsPart,
    int threadsPerGroup
);

void updateParticlesAccelerationCUDA(
    int numParticles,
    double h,
    double spikeyK,
    double viscoK,
    double maxAcc,
    glm::vec3* pos_list,
    glm::vec3* vel_list,
    double* density_list,
    double* pressure_list,
    glm::vec3* acc_list,
    int* neighboursListData,
    int* neighboursOffsets
);

void accelerationCUDA(
    glm::vec3* pos_list,
    int* cell_list,
    int* particle_list,
    double* density_list,
    float3 cell_dims,
    float3 min_box_bound,
    int numParticles,
    double h,
    float h_inv,
    double* pressure_list,
    double spikeyK,
    double viscoK,
    double maxAcc,
    glm::vec3* vel_list,
    glm::vec3* acc_list,
    int threadGroupsPart,
    int threadsPerGroup
);

void updateParticlesPositionAndBoundsCUDA(
    int numParticles,
    glm::vec3* pos_list,
    glm::vec3* vel_list,
    glm::vec3* acc_list,
    double deltaTime,
    double maxVel,
    double xLimitMin,
    double xLimitMax,
    double yLimitMin,
    double yLimitMax,
    double zLimitMin,
    double zLimitMax,
    double eps,
    double damping,
    int threadGroupsPart,
    int threadsPerGroup
);

void resetCellGridCUDA(
    int* d_cell_list,
    int numParticles,
    int threadGroupsCell,
    int threadsPerGroup
);

void assingCellIdCUDA(
    glm::vec3* pos_list,
    int* cell_list,
    int* particle_list,
    int N,
    float3 cell_dims,
    float3 min_box_bound,
    float h_inv,
    int threadGroupsPart,
    int threadsPerGroup
);
