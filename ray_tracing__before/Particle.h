#pragma once

typedef struct Particle {

	std::vector<int> gridID_list;
	std::vector<glm::vec3> pos_list;
	std::vector<glm::vec3> vel_list;
	std::vector<glm::vec3> velHalfDeltaTime_list;
	std::vector<glm::vec3> acc_list;
	std::vector<glm::vec3> force_list;
	std::vector<std::vector<int>> neighbours_list;
	std::vector<double> density_list;
	std::vector<double> pressure_list;
	std::vector<bool> velHalfDeltaTime_isInit;

}Particle;
