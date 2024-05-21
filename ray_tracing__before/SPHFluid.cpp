#include "SPHFluid.h"
#include "CudaCheck.h"
#include "hello_vulkan.h"

SPHFluid::SPHFluid()
{
	try {
		nlohmann::json particlesConstants = getJSONPartitionFromFile("C:\\Users\\Alejandro\\Documents\\TFG\\SPH_Fluid-Vulkan_RTX\\RTX_SPH_Fluid\\ray_tracing__before\\simConfig.json",
															"particles-constants");
		
		maxAcc_ = particlesConstants["max-acceleration"].get<double>();
		maxVel_ = particlesConstants["max-velocity"].get<double>();
		viscoK_ = particlesConstants["viscosity"].get<double>();
		cudaMode_ = particlesConstants["cudaMode"].get<std::string>();
		h_ = particlesConstants["smoothing-radius"].get<double>();
		initialDensity_ = particlesConstants["initial-density"].get<double>();
		partMass_ = particlesConstants["particle-mass"].get<double>();
		pressureK_ = particlesConstants["pressure"].get<double>();
		gravityMagnitude_ = particlesConstants["gravity-acceleration"].get<double>();
		
		nlohmann::json sceneSetup = getJSONPartitionFromFile("C:\\Users\\Alejandro\\Documents\\TFG\\SPH_Fluid-Vulkan_RTX\\RTX_SPH_Fluid\\ray_tracing__before\\simConfig.json",
															"scene-config");
		
		xLimitMin_ = sceneSetup["boundaries-volume"]["min-x"].get<double>();
		xLimitMax_ = sceneSetup["boundaries-volume"]["max-x"].get<double>();
		yLimitMin_ = sceneSetup["boundaries-volume"]["min-y"].get<double>();
		yLimitMax_ = sceneSetup["boundaries-volume"]["max-y"].get<double>();
		zLimitMin_ = sceneSetup["boundaries-volume"]["min-z"].get<double>();
		zLimitMax_ = sceneSetup["boundaries-volume"]["max-z"].get<double>();
		numParticles_ = sceneSetup["num-particles"].get<int>();

		hsq_ = h_ * h_;
		hInv_ = 1 / h_;
		poly6K_ = 315.0 / (64.0 * PI * powf(h_, 9.0));
		spikeyK_ = -45.0 / (PI * powf(h_, 6.0));
		sphGrid_ = Grid(h_);
		gravityForce_ = glm::vec3(0.0, gravityMagnitude_, 0.0);
		minBoxBound_ = make_float3(xLimitMin_, yLimitMin_, zLimitMin_);
		maxBoxBound_ = make_float3(xLimitMax_, yLimitMax_, zLimitMax_);
		cellDims_ = ((maxBoxBound_ - minBoxBound_) / h_) + 1.0;
		cellNum_ = int(cellDims_.x * cellDims_.y * cellDims_.z);
		threadsPerGroup_ = 128;
		threadGroupsCell_ = int((cellNum_ + threadsPerGroup_ - 1) / threadsPerGroup_);
		threadGroupsPart_ = int((numParticles_ + threadsPerGroup_ - 1) / threadsPerGroup_);
		cellList_.resize(cellNum_, -1);
		particlesList_.resize(numParticles_, -1);
		eps_ = 0.001;
		damping_ = 0.3;
	}
	catch (const std::exception& e) {
		std::cout << "Error: " << e.what() << std::endl;
	}
	std::cout << "H_dbg:" << h_ << std::endl;
}

void SPHFluid::addFluidParticles(std::vector<glm::vec3> points)
{
	for (auto& point : points)
	{
		int const gridID = sphGrid_.insertPoint(point);
		parts_.gridID_list.push_back(gridID);
		parts_.pos_list.push_back(point);
		parts_.vel_list.emplace_back(0.0, 0.0, 0.0);
		parts_.velHalfDeltaTime_list.emplace_back(0.0, 0.0, 0.0);
		parts_.acc_list.emplace_back(0.0, 0.0, 0.0);
		parts_.neighbours_list.emplace_back();

		parts_.velHalfDeltaTime_isInit.emplace_back(false);

		// Create pressure offset from initial pressure
		// uniform densities will cause uniform pressure of 0, meaning no acceleration
		// of system
		parts_.density_list.push_back(initialDensity_);

		// initial pressure will be calculated once all particles are in place
		parts_.pressure_list.emplace_back(0.0);
	}
}

Particle* SPHFluid::getFluidParticles()
{
	return &parts_;
}

void SPHFluid::updateGrid()
{
	for (auto id : parts_.gridID_list) {
		sphGrid_.movePoint(id, parts_.pos_list[id]);
	}
	sphGrid_.update();
}

void SPHFluid::updateNeighbours()
{
	// Timing
	//double deltaTime;
	//double lastTimeSim = glfwGetTime();  // first time for deltaTime calculation
	
	for (auto id : parts_.gridID_list) {
		parts_.neighbours_list[id].clear();
		std::vector<int> refs = sphGrid_.getIDsInRadiusOfPoint(id, h_);
		for (auto n : refs) {
			parts_.neighbours_list[id].push_back(n);
		}
    //std::cout << "----UPDATED NEIGHBOURS FOR PARTICLE " << id << std::endl;
	}
	//double lastTimeFPS = glfwGetTime();
	//std::cout << "-----FINISHED UPDATING NEIGHBOURS IN: " << lastTimeFPS - lastTimeSim << " seconds-----" << std::endl;
}

void SPHFluid::updateParticlesDensityAndPressure()
{
	/*
		 Particle density
		 pi = sumj(mjWij)
		 Wij = poly6*(h^2-r^2)
	*/

	glm::vec3 r;
	for (auto& pi : parts_.gridID_list) {

		double density = 0.0;

		for (auto& pj : parts_.neighbours_list[pi]) {
			r = parts_.pos_list[pi] - parts_.pos_list[pj];
			const double rsq = glm::dot(r, r);
			density += partMass_ * poly6K_ * glm::pow(hsq_ - rsq, 3);
		}
		// less than initial density produces negative pressures
		parts_.density_list[pi] = fmax(density, initialDensity_);
		// Particle pressure (Ideal Gas Law)
		// P = K(p-p0)
		parts_.pressure_list[pi] = pressureK_ * (parts_.density_list[pi] - initialDensity_);

	}
}

void SPHFluid::updateParticlesAcceleration() {

	/*
		ai = - Sumj [(mj/mi) * (Pi+P2/2*pi*pj) v Wij * rij]
		Wij = -(45/PI*h^6)*(h-r)^2  ---> (Spiky Kernel)
		avi = e * Sumj [(mj/mi) * (1/pj) * (vj-vi) v^2 Wij * rij]
		e = 0.018
		Wij = -(r^3/2*h^3)+(r^2/h^2)+(h/2*r)-1  ---> (Laplacian Viscosity Kernel)
	*/

	glm::vec3 acc;
	glm::vec3 r;
	glm::vec3 vdiff;
	for (auto& pi : parts_.gridID_list) {
		acc = glm::vec3(0.0, 0.0, 0.0);

		for (auto& pj : parts_.neighbours_list[pi]) {
			r = parts_.pos_list[pi] - parts_.pos_list[pj];
			double dist = glm::length(r);
			if (dist == 0.0) {
				continue;
			}
			float inv = 1 / dist;
			r = inv * r;
			// acceleration due to pressure
			float diff = h_ - dist;
			float spikey = spikeyK_ * diff * diff;
			float massRatio = 1; // mj = mi, mj/mi = 1, todas las particulsa tienen la misma masa
			float pterm = (parts_.pressure_list[pi] + parts_.pressure_list[pj]) / (2 * parts_.density_list[pi] * parts_.density_list[pj]);
			acc -= (float)(massRatio * pterm * spikey) * r;

			// acceleration due to viscosity
			float lap = viscoK_ * diff;
			vdiff = parts_.vel_list[pj] - parts_.vel_list[pi];
			acc += (float)(viscoK_ * massRatio * (1 / parts_.density_list[pj]) * lap) * vdiff;
		}

		// acceleration due to gravity
		acc += gravityForce_;

		double acc_length = glm::length(acc);

		if (acc_length > maxAcc_) {
			acc = (acc / (float)acc_length) * (float)maxAcc_;
		}

		parts_.acc_list[pi] = acc;
	}
}

void SPHFluid::updateBounds(glm::vec3* pVel, glm::vec3* pPos) {

	if (pPos->x > xLimitMax_) {

		*pPos = glm::vec3((float)(xLimitMax_ - eps_), pPos->y, pPos->z);
		*pVel = glm::vec3((float)(-damping_ * pVel->x), pVel->y, pVel->z);
	}
	else if (pPos->x < xLimitMin_) {
		*pPos = glm::vec3((float)(xLimitMin_ + eps_), pPos->y, pPos->z);
		*pVel = glm::vec3((float)(-damping_ * pVel->x), pVel->y, pVel->z);
	}

	if (pPos->z > zLimitMax_) {

		*pPos = glm::vec3(pPos->x, pPos->y, (float)(zLimitMax_ - eps_));
		*pVel = glm::vec3(pVel->x, pVel->y, (float)(-damping_ * pVel->z));
	}
	else if (pPos->z < zLimitMin_) {
		*pPos = glm::vec3(pPos->x, pPos->y, (float)(zLimitMin_ + eps_));
		*pVel = glm::vec3(pVel->x, pVel->y, (float)(-damping_ * pVel->z));
	}

	if (pPos->y > yLimitMax_) {

		*pPos = glm::vec3(pPos->x, (float)(yLimitMax_ - eps_), pPos->z);
		*pVel = glm::vec3(pVel->x, (float)(-damping_ * pVel->y), pVel->z);
	}
	else if (pPos->y < yLimitMin_) {

		*pPos = glm::vec3(pPos->x, (float)(yLimitMin_ + eps_), pPos->z);
		*pVel = glm::vec3(pVel->x, (float)(-damping_ * pVel->y), pVel->z);
	}
}

void SPHFluid::gpuCudaMalloc()
{
	// Allocate device (GPU) memory
	cudaCheck(cudaMalloc((void**)&d_pos_list, sizeof(glm::vec3) * numParticles_));
	cudaCheck(cudaMalloc((void**)&d_vel_list, sizeof(glm::vec3) * numParticles_));
	cudaCheck(cudaMalloc((void**)&d_acc_list, sizeof(glm::vec3) * numParticles_));
	cudaCheck(cudaMalloc((void**)&d_density_list, sizeof(double) * numParticles_));
	cudaCheck(cudaMalloc((void**)&d_pressure_list, sizeof(double) * numParticles_));
	cudaCheck(cudaMalloc((void**)&d_cell_list, sizeof(int) * cellNum_));
	cudaCheck(cudaMalloc((void**)&d_particle_list, sizeof(int) * numParticles_));
}

void SPHFluid::gpuCudaFreeMem()
{
	// Free device (GPU) memory
	cudaCheck(cudaFree(d_pos_list));
	cudaCheck(cudaFree(d_density_list));
	cudaCheck(cudaFree(d_pressure_list));
	cudaCheck(cudaFree(d_vel_list));
	cudaCheck(cudaFree(d_acc_list));
	cudaCheck(cudaFree(d_neighboursListData));
	cudaCheck(cudaFree(d_neighboursListOffsets));
}

void SPHFluid::gpuCudaCpyFromDevice()
{
	// Transfer data from device (GPU) to host (CPU)
	cudaCheck(cudaMemcpy(parts_.pressure_list.data(), d_pressure_list, sizeof(double) * numParticles_, cudaMemcpyDeviceToHost));
	cudaCheck(cudaMemcpy(parts_.pos_list.data(), d_pos_list, sizeof(glm::vec3) * numParticles_, cudaMemcpyDeviceToHost));
	cudaCheck(cudaMemcpy(parts_.vel_list.data(), d_vel_list, sizeof(glm::vec3) * numParticles_, cudaMemcpyDeviceToHost));
}

void SPHFluid::gpuCudaCpyFromHost()
{
	// Copy data from host to device
	cudaCheck(cudaMemcpy(d_pos_list, parts_.pos_list.data(), sizeof(glm::vec3) * numParticles_, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_vel_list, parts_.vel_list.data(), sizeof(glm::vec3) * numParticles_, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_cell_list, cellList_.data(), sizeof(int) * cellNum_, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_particle_list, particlesList_.data(), sizeof(int) * numParticles_, cudaMemcpyHostToDevice));
}

void SPHFluid::updateParticlesPosition(double deltaTime) {

	for (auto& p : parts_.gridID_list)
	{
		//LeapFrog Integration
		if (parts_.velHalfDeltaTime_isInit[p])
		{
			parts_.velHalfDeltaTime_list[p] += ((float)deltaTime * parts_.acc_list[p]);
			parts_.velHalfDeltaTime_isInit[p] = false;
		}
		else {
			parts_.velHalfDeltaTime_list[p] = (float)(deltaTime / 2) * parts_.acc_list[p] + parts_.vel_list[p];
			parts_.velHalfDeltaTime_isInit[p] = true;
		}

		parts_.pos_list[p] += (float)deltaTime * parts_.velHalfDeltaTime_list[p];

		parts_.vel_list[p] = (float)(deltaTime / 2) * parts_.acc_list[p] + parts_.velHalfDeltaTime_list[p];

		//Cut max velocity
		if (glm::length(parts_.vel_list[p]) > maxVel_)
		{
			parts_.vel_list[p] = (float)maxVel_ * glm::normalize(parts_.vel_list[p]);
		}

		updateBounds(&parts_.vel_list[p], &parts_.pos_list[p]);

		if (p == 0) {
			std::cout << "Y_POS_PART01: " << parts_.pos_list[p].y << std::endl;
			std::cout << "Y_VEL_PART01: " << parts_.vel_list[p].y << std::endl;
			std::cout << "Y_VEL/2_PART01: " << parts_.velHalfDeltaTime_list[p].y << std::endl;
		}
	}
}

void SPHFluid::gpuPhysics(double deltaTime)
{
	//double lastTimeSim = glfwGetTime();  // first time for deltaTime calculation
	// Flatten the neighbours list to push to GPU
	std::vector<int> flattenedNeighbours;
	std::vector<int> offsets;

	offsets.push_back(0);
	for (const auto& neighbors : parts_.neighbours_list) {
		flattenedNeighbours.insert(flattenedNeighbours.end(), neighbors.begin(), neighbors.end());
		offsets.push_back(flattenedNeighbours.size());
	}

	cudaCheck(cudaMalloc((void**)&d_neighboursListData, sizeof(int) * flattenedNeighbours.size()));
	cudaCheck(cudaMalloc((void**)&d_neighboursListOffsets, sizeof(int) * offsets.size()));
	cudaCheck(cudaMemcpy(d_neighboursListData, flattenedNeighbours.data(), sizeof(int) * flattenedNeighbours.size(), cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_neighboursListOffsets, offsets.data(), sizeof(int) * offsets.size(), cudaMemcpyHostToDevice));

	updateParticlesDensityAndPressureCUDA(
		numParticles_,
		hsq_,
		partMass_,
		poly6K_,
		initialDensity_,
		pressureK_,
		d_pos_list,
		d_density_list,
		d_pressure_list,
		d_neighboursListData,
		d_neighboursListOffsets
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	updateParticlesAccelerationCUDA(
		numParticles_,
		h_,
		spikeyK_,
		viscoK_,
		maxAcc_,
		d_pos_list,
		d_vel_list,
		d_density_list,
		d_pressure_list,
		d_acc_list,
		d_neighboursListData,
		d_neighboursListOffsets
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	updateParticlesPositionAndBoundsCUDA(
		numParticles_,
		d_pos_list,
		d_vel_list,
		d_acc_list,
		deltaTime,
		maxVel_,
		xLimitMin_,
		xLimitMax_,
		yLimitMin_,
		yLimitMax_,
		zLimitMin_,
		zLimitMax_,
		eps_,
		damping_,
		threadGroupsPart_,
		threadsPerGroup_
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	gpuCudaCpyFromDevice();
    //double lastTimeFPS = glfwGetTime();
    //std::cout << "-----FINISHED CALCULATING PHYSICS IN: " << lastTimeFPS - lastTimeSim << " seconds-----" << std::endl;
}

void SPHFluid::gpuCalculation(double deltaTime)
{
	resetCellGridCUDA(d_cell_list, cellNum_, threadGroupsCell_, threadsPerGroup_);

	cudaPeekAtLastError();
	cudaDeviceSynchronize();

	assingCellIdCUDA(
		d_pos_list,
		d_cell_list,
		d_particle_list,
		numParticles_,
		cellDims_,
		minBoxBound_,
		hInv_,
		threadGroupsPart_,
		threadsPerGroup_
	);

	cudaPeekAtLastError();
	cudaDeviceSynchronize();

	densityAndPressureCUDA(
		d_pos_list,
		d_cell_list,
		d_particle_list,
		d_density_list,
		cellDims_,
		minBoxBound_,
		numParticles_,
		hsq_,
		hInv_,
		partMass_,
		poly6K_,
		initialDensity_,
		pressureK_,
		d_pressure_list,
		threadGroupsPart_,
		threadsPerGroup_
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	accelerationCUDA(
		d_pos_list,
		d_cell_list,
		d_particle_list,
		d_density_list,
		cellDims_,
		minBoxBound_,
		numParticles_,
		h_,
		hInv_,
		d_pressure_list,
		spikeyK_,
		viscoK_,
		maxAcc_,
		d_vel_list,
		d_acc_list,
		threadGroupsPart_,
		threadsPerGroup_
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	updateParticlesPositionAndBoundsCUDA(
		numParticles_,
		d_pos_list,
		d_vel_list,
		d_acc_list,
		deltaTime,
		maxVel_,
		xLimitMin_,
		xLimitMax_,
		yLimitMin_,
		yLimitMax_,
		zLimitMin_,
		zLimitMax_,
		eps_,
		damping_,
		threadGroupsPart_,
		threadsPerGroup_
	);

	cudaCheck(cudaPeekAtLastError());
	cudaCheck(cudaDeviceSynchronize());

	gpuCudaCpyFromDevice();
}

void SPHFluid::update(double deltaTime)
{
	if (cudaMode_ == "full") {
		gpuCalculation(deltaTime);
	}
	else if (cudaMode_ == "physics") {
		updateGrid();
		updateNeighbours();
		gpuPhysics(deltaTime);
	}
	else if (cudaMode_ == "none") {
		updateGrid();
		updateNeighbours();
		updateParticlesDensityAndPressure();
		updateParticlesAcceleration();
		updateParticlesPosition(deltaTime);
	}
	else {
		exit(EXIT_FAILURE);
	}
}

void SPHFluid::configurationShow() {
	std::cout << "numParticles_: " << numParticles_ << std::endl;
	std::cout << "cudaMode_: " << cudaMode_ << std::endl;
	std::cout << "h_: " << h_ << std::endl;
	std::cout << "partMass_: " << partMass_ << std::endl;
	std::cout << "initialDensity_: " << initialDensity_ << std::endl;
	std::cout << "pressureK_: " << pressureK_ << std::endl;
	std::cout << "poly6K_: " << poly6K_ << std::endl;
	std::cout << "spikeyK_: " << spikeyK_ << std::endl;
	std::cout << "viscoK_: " << viscoK_ << std::endl;
	std::cout << "gravityForce_: (" << gravityForce_.x << ", " << gravityForce_.y << ", " << gravityForce_.z << ")" << std::endl;
	std::cout << "gravityMagnitude_: " << gravityMagnitude_ << std::endl;
	std::cout << "maxAcc_: " << maxAcc_ << std::endl;
	std::cout << "maxVel_: " << maxVel_ << std::endl;
	std::cout << "xLimitMax_: " << xLimitMax_ << std::endl;
	std::cout << "xLimitMin_: " << xLimitMin_ << std::endl;
	std::cout << "yLimitMax_: " << yLimitMax_ << std::endl;
	std::cout << "yLimitMin_: " << yLimitMin_ << std::endl;
	std::cout << "zLimitMax_: " << zLimitMax_ << std::endl;
	std::cout << "zLimitMin_: " << zLimitMin_ << std::endl;
	std::cout << "hInv_: " << hInv_ << std::endl;
}

double SPHFluid::getYLimitMin()
{
  return yLimitMin_;
}