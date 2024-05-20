// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/gridcell.cpp

#include "Cell.h"

Cell::Cell() {
    i = 0; j = 0; k=0;
}

void Cell::insertGridPoint(GridPoint* gp)
{
    gp->i = i; gp->j = j; gp->k = k;
    gp->isInGridCell = true;
    cellPoints_.push_back(gp);
}

void Cell::initialize(int ii, int jj, int kk)
{
    i = ii; j = jj; k = kk;
}

std::vector<GridPoint*> Cell::getGridPoints()
{
    return cellPoints_;
}

void Cell::removeGridPoint(GridPoint* gp)
{
    for (int i = 0; i < (int)cellPoints_.size(); i++) {
        if (cellPoints_[i]->id == gp->id) {
            gp->isInGridCell = false;
            cellPoints_.erase(cellPoints_.begin() + i);
            return;
        }
    }
}

bool Cell::isEmpty()
{
    return cellPoints_.empty();
}

void Cell::reset()
{

    for (auto const point : cellPoints_)
    {
        point->isInGridCell = false;
    }

    cellPoints_.clear();
    i = 0; j = 0; k = 0;
}
