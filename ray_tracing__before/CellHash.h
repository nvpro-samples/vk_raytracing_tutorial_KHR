// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/cellhash.cpp

#pragma once

#define GLAD_ONLY_HEADERS
#include "Cell.h"

class CellHash
{
public:
    CellHash();
    bool isCellInHash(int i, int j, int k);
    void insertCell(Cell* cell);
    void removeCell(Cell* cell);
    Cell* getCell(int i, int j, int k);
    Cell* findCell(int i, int j, int k, bool* isCellFound);
    void getCells(std::vector<Cell*>* cells);

private:
    inline long computeHash(int i, int j, int k);
    long maxNumHashValues;
    std::unordered_map<long, std::vector<Cell*>> cellMap;
};

