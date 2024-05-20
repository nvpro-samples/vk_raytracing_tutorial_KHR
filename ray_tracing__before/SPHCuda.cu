#pragma once

#include "SPHCuda.h"
#include <stdio.h>

__device__ int3 calculate_cell_idx(float3 pos_float3, float3 min_box_bound, float3 cell_dims, float h_inv, size_t tid) {

    float3 v = (pos_float3 - (min_box_bound)) * h_inv;
    int3 cell_idx = make_int3(int(v.x - (v.x - floorf(v.x))), int(v.y - (v.y - floorf(v.y))), int(v.z - (v.z - floorf(v.z)))); v;

    if (cell_idx.x < 0 || cell_idx.y < 0 || cell_idx.z < 0 || cell_idx.x >= cell_dims.x || cell_idx.y >= cell_dims.y || cell_idx.z >= cell_dims.z) {
        // Avoid illegal memory access
        return make_int3(-1, -1, -1);
    }

    return cell_idx;

}

/* --------------------- KERNEL FUNCs ----------------------*/

__global__ void updateParticlesDensityAndPressureKernel(
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
    int* neighboursListOffsets
) {

    int tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid < numParticles) {
        int pi = tid;
        double density = 0.0;
        glm::vec3 r;

        // Iterate through neighbors using the flattened list and offsets
        int start = neighboursListOffsets[tid];
        int end = neighboursListOffsets[tid + 1];

        for (int i = start; i < end; ++i) {
            int pj = neighboursListData[i];
            r = pos_list[pi] - pos_list[pj];
            const double rsq = glm::dot(r, r);
            density += partMass * poly6K * glm::pow(hsq - rsq, 3);
        }

        density_list[pi] = fmax(density, initialDensity);
        pressure_list[pi] = pressureK * (density_list[pi] - initialDensity);
    }
}

__global__ void updateParticlesAccelerationKernel(
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
    int* neighboursListOffsets
) {

    int tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid < numParticles) {
        glm::vec3 acc = glm::vec3(0.0, 0.0, 0.0);
        glm::vec3 r;
        glm::vec3 vdiff;
        int pi = tid;
        // Iterate through neighbors using the flattened list and offsets
        int start = neighboursListOffsets[tid];
        int end = neighboursListOffsets[tid + 1];

        for (int i = start; i < end; ++i) {
            int pj = neighboursListData[i];
            r = pos_list[pi] - pos_list[pj];
            double dist = glm::length(r);

            if (dist == 0.0) {
                continue;
            }

            float inv = 1 / dist;
            r = inv * r;

            // acceleration due to pressure
            float diff = h - dist;
            float spikey = spikeyK * diff * diff;
            float massRatio = 1; // mj = mi, mj/mi = 1
            float pterm = (pressure_list[pi] + pressure_list[pj]) / (2 * density_list[pi] * density_list[pj]);
            acc -= (float)(massRatio * pterm * spikey) * r;

            // acceleration due to viscosity
            float lap = viscoK * diff;
            vdiff = vel_list[pj] - vel_list[pi];
            acc += (float)(viscoK * massRatio * (1 / density_list[pj]) * lap) * vdiff;
        }

        // acceleration due to gravity
        acc += glm::vec3(0.0, -9.8, 0.0);  // Assuming gravity along the y-axis

        double acc_length = glm::length(acc);

        if (acc_length > maxAcc) {
            acc = (acc / (float)acc_length) * (float)maxAcc;
        }

        acc_list[pi] = acc;
    }
}

__global__ void updateParticlesPositionAndBoundsKernel(
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
    double damping

) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid < numParticles)
    {
        // Update velocity and position
        vel_list[tid] += ((float)deltaTime * acc_list[tid]);
        pos_list[tid] += ((float)deltaTime * vel_list[tid]);

        // Check if velocity exceeds the maximum
        if (glm::length(vel_list[tid]) > maxVel)
        {
            vel_list[tid] = (float)maxVel * glm::normalize(vel_list[tid]);
        }

        // Handle special case when idx is 0
        if (tid == 0)
        {
            // Do something specific for idx == 0
            printf("Y_POS_PART01: %f\n", pos_list[tid].y);
            printf("Y_VEL_PART01: %f\n", vel_list[tid].y);
        }

        // Update plane bounds
        if (pos_list[tid].x > xLimitMax) {
            pos_list[tid].x = (float)(xLimitMax - eps);
            vel_list[tid].x = (float)(-damping * vel_list[tid].x);
        }
        else if (pos_list[tid].x < xLimitMin) {
            pos_list[tid].x = (float)(xLimitMin + eps);
            vel_list[tid].x = (float)(-damping * vel_list[tid].x);
        }

        if (pos_list[tid].z > zLimitMax) {
            pos_list[tid].z = (float)(zLimitMax - eps);
            vel_list[tid].z = (float)(-damping * vel_list[tid].z);
        }
        else if (pos_list[tid].z < zLimitMin) {
            pos_list[tid].z = (float)(zLimitMin + eps);
            vel_list[tid].z = (float)(-damping * vel_list[tid].z);
        }

        if (pos_list[tid].y > yLimitMax) {
            pos_list[tid].y = (float)(yLimitMax - eps);
            vel_list[tid].y = (float)(-damping * vel_list[tid].y);
        }
        else if (pos_list[tid].y < yLimitMin) {
            pos_list[tid].y = (float)(yLimitMin + eps);
            vel_list[tid].y = (float)(-damping * vel_list[tid].y);
        }
    }
}

__global__ void resetCellGridKernel(int* d_cell_list, int numParticles)
{
    int tid = (blockIdx.x * blockDim.x) + threadIdx.x;

    if (tid < numParticles) {
        d_cell_list[tid] = -1;
    }
}

__global__ void assingCellIdKernel(
    glm::vec3* pos_list,
    int* cell_list,
    int* particle_list,
    int N,
    float3 cell_dims,
    float3 min_box_bound,
    float h_inv
)
{
    int tid = (blockIdx.x * blockDim.x) + threadIdx.x;

    if (tid <= N) {
        float3 pos_float3;
        pos_float3.x = pos_list[tid].x;
        pos_float3.y = pos_list[tid].y;
        pos_float3.z = pos_list[tid].z;
        int3 cell_idx = calculate_cell_idx(pos_float3, min_box_bound, cell_dims, h_inv, tid);

        if (cell_idx.x != -1) {
            int flat_cell_idx = cell_idx.x + cell_dims.x * cell_idx.y + cell_dims.x * cell_dims.y * cell_idx.z;

            particle_list[tid] = atomicExch(&cell_list[flat_cell_idx], tid);

        }
    }
}

__global__ void densityAndPressureKernel(
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
    double* pressure_list
)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid < numParticles)
    {
        int pi = tid;
        float3 pos_float3;

        pos_float3.x = pos_list[tid].x;
        pos_float3.y = pos_list[tid].y;
        pos_float3.z = pos_list[tid].z;

        int3 cell_idx = calculate_cell_idx(pos_float3, min_box_bound, cell_dims, h_inv, tid);

        if (cell_idx.x == -1) {
            return;
        }

        double density = 0.0;
        glm::vec3 r;

        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++)
                {
                    int3 neighbor_cell_idx = cell_idx + make_int3(x, y, z);
                    if (neighbor_cell_idx.x < 0 || neighbor_cell_idx.y < 0 || neighbor_cell_idx.z < 0 || neighbor_cell_idx.x >= cell_dims.x || neighbor_cell_idx.y >= cell_dims.y || neighbor_cell_idx.z >= cell_dims.z) {
                        continue;
                    }
                    int neighbor_flat_idx = neighbor_cell_idx.x + neighbor_cell_idx.y * cell_dims.x + neighbor_cell_idx.z * cell_dims.x * cell_dims.y;

                    int neighbor_particle_idx = cell_list[neighbor_flat_idx];
                    while (neighbor_particle_idx != -1)
                    {
                        int pj = neighbor_particle_idx;
                        r = pos_list[pi] - pos_list[pj];
                        const double rsq = glm::dot(r, r);
                        density += partMass * poly6K * glm::pow(hsq - rsq, 3);
                        neighbor_particle_idx = particle_list[neighbor_particle_idx];
                    }
                }
            }
        }
        density_list[pi] = fmax(density, initialDensity);
        pressure_list[pi] = pressureK * (density_list[pi] - initialDensity);
    }
}

__global__ void accelerationKernel(
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
    glm::vec3* acc_list
)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid < numParticles)
    {
        int pi = tid;
        float3 pos_float3_pi;
        glm::vec3 acc = glm::vec3(0.0, 0.0, 0.0);
        glm::vec3 r;
        glm::vec3 vdiff;

        pos_float3_pi.x = pos_list[tid].x;
        pos_float3_pi.y = pos_list[tid].y;
        pos_float3_pi.z = pos_list[tid].z;

        int3 cell_idx = calculate_cell_idx(pos_float3_pi, min_box_bound, cell_dims, h_inv, tid);

        if (cell_idx.x == -1) {
            return;
        }

        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++)
                {
                    int3 neighbor_cell_idx = cell_idx + make_int3(x, y, z);
                    if (neighbor_cell_idx.x < 0 || neighbor_cell_idx.y < 0 || neighbor_cell_idx.z < 0 || neighbor_cell_idx.x >= cell_dims.x || neighbor_cell_idx.y >= cell_dims.y || neighbor_cell_idx.z >= cell_dims.z) {
                        continue;
                    }
                    int neighbor_flat_idx = neighbor_cell_idx.x + neighbor_cell_idx.y * cell_dims.x + neighbor_cell_idx.z * cell_dims.x * cell_dims.y;
                    int neighbor_particle_idx = cell_list[neighbor_flat_idx];

                    while (neighbor_particle_idx != -1)
                    {
                        int pj = neighbor_particle_idx;

                        r = pos_list[pi] - pos_list[pj];
                        double dist = length(r);

                        if (dist > 0 && dist < h) {
                            float inv = 1 / dist;
                            r = inv * r;

                            // acceleration due to pressure
                            float diff = h - dist;
                            float spikey = spikeyK * diff * diff;
                            float massRatio = 1; // mj = mi, mj/mi = 1
                            float pterm = (pressure_list[pi] + pressure_list[pj]) / (2 * density_list[pi] * density_list[pj]);
                            acc -= (float)(massRatio * pterm * spikey) * r;

                            // acceleration due to viscosity
                            float lap = viscoK * diff;
                            vdiff = vel_list[pj] - vel_list[pi];
                            acc += (float)(viscoK * massRatio * (1 / density_list[pj]) * lap) * vdiff;
                        }

                        neighbor_particle_idx = particle_list[neighbor_particle_idx];
                    }
                }
            }
        }

        // acceleration due to gravity
        acc += glm::vec3(0.0, -9.8, 0.0);  // Assuming gravity along the y-axis

        double acc_length = glm::length(acc);

        if (acc_length > maxAcc) {
            acc = (acc / (float)acc_length) * (float)maxAcc;
        }

        acc_list[pi] = acc;
    }
}


/* --------------------- AUX FUNC ---------------------- */
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
    int* neighboursListOffsets
) {

    int numBlocks = (numParticles + 255) / 256;
    int numThreads = 256;

    updateParticlesDensityAndPressureKernel <<< numBlocks, numThreads >>> (
        numParticles,
        hsq,
        partMass,
        poly6K,
        initialDensity,
        pressureK,
        pos_list,
        density_list,
        pressure_list,
        neighboursListData,
        neighboursListOffsets
        );
}

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
) {

    int numBlocks = (numParticles + 255) / 256;
    int numThreads = 256;

    updateParticlesAccelerationKernel << <numBlocks, numThreads >> > (
        numParticles,
        h,
        spikeyK,
        viscoK,
        maxAcc,
        pos_list,
        vel_list,
        density_list,
        pressure_list,
        acc_list,
        neighboursListData,
        neighboursOffsets
        );
}


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
)
{
    int numBlocks = (numParticles + 255) / 256;
    int numThreads = 256;

    updateParticlesPositionAndBoundsKernel <<<threadGroupsPart, threadsPerGroup>>> (
        numParticles,
        pos_list,
        vel_list,
        acc_list,
        deltaTime,
        maxVel,
        xLimitMin,
        xLimitMax,
        yLimitMin,
        yLimitMax,
        zLimitMin,
        zLimitMax,
        eps,
        damping
        );
}

void resetCellGridCUDA(
    int* d_cell_list,
    int numParticles,
    int threadGroupsCell,
    int threadsPerGroup
)
{
    resetCellGridKernel <<<threadGroupsCell, threadsPerGroup>>> (d_cell_list, numParticles);
}

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
)
{
    assingCellIdKernel <<<threadGroupsPart, threadsPerGroup>>> (
        pos_list,
        cell_list,
        particle_list,
        N,
        cell_dims,
        min_box_bound,
        h_inv
        );
}

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
)
{
    densityAndPressureKernel <<<threadGroupsPart, threadsPerGroup>>> (
        pos_list,
        cell_list,
        particle_list,
        density_list,
        cell_dims,
        min_box_bound,
        numParticles,
        hsq,
        h_inv,
        partMass,
        poly6K,
        initialDensity,
        pressureK,
        pressure_list
        );
}

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
)
{
    accelerationKernel <<<threadGroupsPart, threadsPerGroup>>> (
        pos_list,
        cell_list,
        particle_list,
        density_list,
        cell_dims,
        min_box_bound,
        numParticles,
        h,
        h_inv,
        pressure_list,
        spikeyK,
        viscoK,
        maxAcc,
        vel_list,
        acc_list
        );
}
