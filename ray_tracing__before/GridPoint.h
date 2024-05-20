// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/gridpoint.h

#pragma once

#define GLAD_ONLY_HEADERS
#include <stdio.h>
#include <iostream>
#include <vector>
#include <map>
#include <fstream>
#include <string>
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

typedef struct GridPoint {

    glm::vec3 pos;
    int id;
    double tx, ty, tz;
    int i, j, k;
    bool isInGridCell = false;
    bool isMarkedForRemoval = false;

}GridPoint;
