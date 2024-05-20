// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/spatialgrid.h

#pragma once

#define GLAD_ONLY_HEADERS
#include <unordered_map>

#include "Cell.h"
#include "GridPoint.h"
#include "CellHash.h"

class Grid {
public:
    Grid();
    Grid(double cell_size);
    void initFreeCells();
    int insertPoint(glm::vec3 pos);
    std::vector<int> getIDsInRadiusOfPoint(int ref, double radius);
    void movePoint(int id, glm::vec3 position);
    void update();
private:
    double size_;
    int initialFreeCells_;
    int gridPointerID_;
    std::vector<Cell*> freeCells_;
    int generateUniqueGridPointID();
    std::vector<GridPoint*> points_;
    std::unordered_map<int, GridPoint*> gridPointsByID_;
    void insertGridPointIntoGrid(GridPoint* p);
    void positionToIJK(glm::vec3 p, int* i, int* j, int* k);
    CellHash cellHashTable;
    Cell* getNewGridCell(int i, int j, int k);
    void updateGridPointCellOffset(GridPoint* gp, int i, int j, int k);
    glm::vec3 IJKToPosition(int i, int j, int k);
    std::vector<int> fastIDNeighbourSearch(int ref, double r, GridPoint* p);
};

