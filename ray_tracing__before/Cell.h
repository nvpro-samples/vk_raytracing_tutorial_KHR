// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/gridcell.h

#pragma once

#define GLAD_ONLY_HEADERS
#include "GridPoint.h"

class Cell {
public:
    Cell();
    int i, j, k;
    void insertGridPoint(GridPoint* gp);
    void initialize(int ii, int jj, int kk);
    std::vector<GridPoint*> getGridPoints();
    std::vector<Cell*> neighbours;
    void removeGridPoint(GridPoint* gp);
    bool isEmpty();
    void reset();
private:
    std::vector<GridPoint*> cellPoints_;

};

