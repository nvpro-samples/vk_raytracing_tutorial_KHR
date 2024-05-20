// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/cellhash.cpp

#include "Cellhash.h"

CellHash::CellHash()
{
    maxNumHashValues = 10000;
}

inline long CellHash::computeHash(int i, int j, int k) {
    return (abs(541 * (long)i + 79 * (long)j + 31 * (long)k) % maxNumHashValues);
}

void CellHash::insertCell(Cell* cell) {
    long h = computeHash(cell->i, cell->j, cell->k);

    if (cellMap.find(h) == cellMap.end()) {
        std::vector<Cell*> newChain;
        std::pair<long, std::vector<Cell*>> pair(h, newChain);
        cellMap.insert(pair);
    }

    cellMap[h].push_back(cell);
}

void CellHash::removeCell(Cell* cell) {
    int i = cell->i;
    int j = cell->j;
    int k = cell->k;
    long h = computeHash(i, j, k);

    if (cellMap.find(h) == cellMap.end()) {
        std::cout << "cant find cell" << i << j << k << h;
        return;
    }

    // remove from hash chain
    bool isRemoved = false;
    std::vector<Cell*> chain = cellMap[h];
    for (int idx = 0; idx < (int)chain.size(); idx++) {
        Cell* c = (cellMap[h])[idx];
        if (c->i == i && c->j == j && c->k == k) {
            cellMap[h].erase(cellMap[h].begin() + idx);
            isRemoved = true;
            break;
        }
    }

    if (!isRemoved) {
        std::cout << "Could not find/remove Cell" << i << j << k;
    }

    // remove chain from map if empty
    if (chain.size() == 0) {
        cellMap.erase(h);
    }

}

Cell* CellHash::getCell(int i, int j, int k) {
    long h = computeHash(i, j, k);

    Cell* c = new Cell();
    std::vector<Cell*> chain = cellMap[h];
    for (int idx = 0; idx < (int)chain.size(); idx++) {
        c = chain[idx];
        if (c->i == i && c->j == j && c->k == k) {
            return c;
        }
    }

    return c;
}

Cell* CellHash::findCell(int i, int j, int k, bool* isCellFound) {
    long h = computeHash(i, j, k);

    Cell* c = new Cell();
    std::vector<Cell*> chain = cellMap[h];
    for (int idx = 0; idx < (int)chain.size(); idx++) {
        c = chain[idx];
      if(c->i == i && c->j == j && c->k == k)
      {
            *isCellFound = true;
            return c;
        }
    }

    *isCellFound = false;
    return c;
}

bool CellHash::isCellInHash(int i, int j, int k) {
    long h = computeHash(i, j, k);

    if (cellMap.find(h) == cellMap.end()) {
        return false;
    }

    Cell* c = new Cell();
    std::vector<Cell*> chain = cellMap[h];
    for (int idx = 0; idx < (int)chain.size(); idx++) {
        c = chain[idx];
        if (c->i == i && c->j == j && c->k == k) {
            return true;
        }
    }

    return false;
}



void CellHash::getCells(std::vector<Cell*>* cells) {
    for (std::pair<int, std::vector<Cell*>> pair : cellMap) {
        for (int i = 0; i < (int)pair.second.size(); i++) {
            cells->push_back(pair.second[i]);
        }
    }

}










