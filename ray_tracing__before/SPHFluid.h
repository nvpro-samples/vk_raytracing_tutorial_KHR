#pragma once

#define GLAD_ONLY_HEADERS
#include <vector_types.h>
#include "Grid.h"
#include "Particle.h"
#include <iostream>
#include <execution>
#define PI 3.14159265f
#include <json.hpp>
#include "utils.h"
#include "SPHCuda.h"

class SPHFluid
{
public:
    Particle* getFluidParticles();
    SPHFluid();
    void   addFluidParticles(std::vector<glm::vec3> points);
    void   update(double deltaTime);
    void   configurationShow();
    void   gpuCudaMalloc();
    void   gpuCudaFreeMem();
    void   gpuCudaCpyFromDevice();
    void   gpuCudaCpyFromHost();
    double getYLimitMin();
    std::string cudaMode_; // full, physics, none

private:
    int numParticles_;
    double h_;
    double hsq_;
    double partMass_;
    double initialDensity_;
    double pressureK_;
    Grid sphGrid_;
    Particle parts_; //!Particles in fluid
    double poly6K_;  //Poly6K Kernel constants
    double spikeyK_; //Spikey Kernel constants
    double viscoK_;
    glm::vec3 gravityForce_;
    double gravityMagnitude_;
    double maxAcc_;
    double maxVel_;

    /* Container boundaries */
    double xLimitMax_;
    double xLimitMin_;
    double yLimitMax_;
    double yLimitMin_;
    double zLimitMax_;
    double zLimitMin_;

    /* cudaMode = full variables */
    float3 cellDims_;
    int cellNum_;
    float3 minBoxBound_;
    float3 maxBoxBound_;
    int threadsPerGroup_;
    int threadGroupsPart_;
    int threadGroupsCell_;
    int hInv_;
    std::vector<int> cellList_, particlesList_;
    double eps_;
    double damping_;

    /* Cuda device variables */
    int* d_cell_list;
    int* d_particle_list;
    glm::vec3* d_pos_list;
    double* d_density_list;
    double* d_pressure_list;
    int* d_neighboursListData;
    int* d_neighboursListOffsets;
    glm::vec3* d_vel_list;
    glm::vec3* d_acc_list;

    void updateGrid();
    void updateNeighbours();
    void updateParticlesDensityAndPressure();
    void updateParticlesAcceleration();
    void updateBounds(glm::vec3* pVel, glm::vec3* pPos);
    void updateParticlesPosition(double deltaTime);
    void gpuCalculation(double deltaTime);
    void gpuPhysics(double deltaTime);
};
