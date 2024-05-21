// Original source code: https://github.com/rlguy/SPHFluidSim/blob/master/src/spatialgrid.cpp

#include "Grid.h"
#define EPS 1e-9

Grid::Grid(double cell_size) {
    size_ = cell_size;
    gridPointerID_ = 0;
    initialFreeCells_ = 10000;
    initFreeCells();
}

Grid::Grid() {

}

void Grid::initFreeCells() {
    for (int i = 0; i < initialFreeCells_; ++i) {
        Cell *cell = new Cell();
        freeCells_.push_back(cell);
    }
}

int Grid::insertPoint(glm::vec3 pos)
{
    GridPoint* point = new GridPoint();
    point->pos = pos;
    point->id = generateUniqueGridPointID();
    point->isMarkedForRemoval = false;
    points_.push_back(point);
    gridPointsByID_.insert({ point->id, point });

    insertGridPointIntoGrid(point);
    return point->id;
}

std::vector<int> Grid::getIDsInRadiusOfPoint(int ref, double radius)
{
    // Searches the container for an element with k as key and returns an iterator to it if found,
    //otherwise it returns an iterator to unordered_map::end (the element past the end of the container).
    if (gridPointsByID_.find(ref) == gridPointsByID_.end()) {
        std::vector<int> objects;
        return objects;
    }

    GridPoint* p = gridPointsByID_[ref];
    double tx = p->tx;
    double ty = p->ty;
    double tz = p->tz;
    int i, j, k;
    positionToIJK(p->pos, &i, &j, &k);
    double inv = 1 / size_;
    double rsq = radius * radius;

    int imin = i - fmax(0, ceil((radius - tx) * inv));
    int jmin = j - fmax(0, ceil((radius - ty) * inv));
    int kmin = k - fmax(0, ceil((radius - tz) * inv));
    int imax = i + fmax(0, ceil((radius - size_ + tx) * inv));
    int jmax = j + fmax(0, ceil((radius - size_ + ty) * inv));
    int kmax = k + fmax(0, ceil((radius - size_ + tz) * inv));

    if (imax - imin <= 3 && imax - imin >= 1) {
        return fastIDNeighbourSearch(ref, radius, p);
    }

    std::vector<int> objects;
    GridPoint* gp;
    Cell* cell;
    glm::vec3 v;
    std::vector<GridPoint*> points;
    for (int ii = imin; ii <= imax; ii++) {
        for (int jj = jmin; jj <= jmax; jj++) {
            for (int kk = kmin; kk <= kmax; kk++) {
                bool isInHash = false;
                cell = cellHashTable.findCell(ii, jj, kk, &isInHash);
                if (isInHash) {
                    points = cell->getGridPoints();
                    for (int idx = 0; idx < (int)points.size(); idx++) {
                        gp = points[idx];
                        if (gp->id != ref) {
                            v = p->pos - gp->pos;
                            if (glm::dot(v, v) < rsq) {
                                objects.push_back(gp->id);
                            }
                        }
                    }
                }
            }
        }
    }

    return objects;
}

int Grid::generateUniqueGridPointID() {
    int id = gridPointerID_;
    gridPointerID_++;
    return id;
}

void Grid::insertGridPointIntoGrid(GridPoint* p)
{
    int i, j, k;
    positionToIJK(p->pos, &i, &j, &k);

    bool isCellInTable = false;
    Cell* cell = cellHashTable.findCell(i, j, k, &isCellInTable);
    if (isCellInTable) {
        cell->insertGridPoint(p);
    }
    else {
        cell = getNewGridCell(i, j, k);
        cell->insertGridPoint(p);
        cellHashTable.insertCell(cell);
    }

    // offset used for updating position
    updateGridPointCellOffset(p, i, j, k);
}

void Grid::positionToIJK(glm::vec3 p, int* i, int* j, int* k)
{
    // ceil : Rounds x upward, returning the smallest integral value that is not less than x.
    // fabs : Returns the absolute value of x: |x|.
    // fmod : Returns the floating-point remainder of numer/denom (rounded towards zero)

	const double inv = 1 / size_;
    *i = ceil(p.x * inv) - 1;
    *j = ceil(p.y * inv) - 1;
    *k = ceil(p.z * inv) - 1;


    if (fabs(fmod(p.x, size_)) < EPS) {
        *i = *i + 1;
    }
    if (fabs(fmod(p.y, size_)) < EPS) {
        *j = *j + 1;
    }
    if (fabs(fmod(p.z, size_)) < EPS) {
        *k = *k + 1;
    }
}

Cell* Grid::getNewGridCell(int i, int j, int k)
{
    if (freeCells_.empty()) {
        int n = 200;
        for (int i = 0; i < n; i++) {
            freeCells_.push_back(new Cell());
        }
    }

    Cell* cell = freeCells_.back();
    freeCells_.pop_back();

    cell->initialize(i, j, k);
    return cell;
}

void Grid::updateGridPointCellOffset(GridPoint* gp, int i, int j, int k)
{
    glm::vec3 cp = IJKToPosition(i, j, k);
    gp->tx = gp->pos.x - cp.x;
    gp->ty = gp->pos.y - cp.y;
    gp->tz = gp->pos.z - cp.z;

}

glm::vec3 Grid::IJKToPosition(int i, int j, int k)
{
    return glm::vec3(i * size_, j * size_, k * size_);
}

std::vector<int> Grid::fastIDNeighbourSearch(int ref, double r, GridPoint* p)
{
    std::vector<int> objects;

    bool isInHash = false;
    Cell* cell = cellHashTable.findCell(p->i, p->j, p->k, &isInHash);

	if (!isInHash) {
        return objects;
    }

    std::vector<GridPoint*> points = cell->getGridPoints();
    GridPoint* gp;
    glm::vec3 v;
    double rsq = r * r;
    for (unsigned int i = 0; i < points.size(); i++) {
        gp = points[i];
        if (gp->id != ref) {
            v = p->pos - gp->pos;
            if (glm::dot(v, v) < rsq) {
                objects.push_back(gp->id);
            }
        }
    }

    std::vector<Cell*> neighbours = cell->neighbours;

    for (unsigned int i = 0; i < neighbours.size(); i++) {
        points = neighbours[i]->getGridPoints();
        for (unsigned int j = 0; j < points.size(); j++) {
            gp = points[j];
            v = p->pos - gp->pos;
            if (glm::dot(v, v) < rsq) {
                objects.push_back(gp->id);
            }
        }
    }

    return objects;
}

void Grid::movePoint(int id, glm::vec3 newPos) {
    if (gridPointsByID_.find(id) == gridPointsByID_.end()) {
        return;
    }

    GridPoint* point = gridPointsByID_[id];
    int i = point->i;
    int j = point->j;
    int k = point->k;

    glm::vec3 trans = newPos - point->pos;
    point->tx += trans.x;
    point->ty += trans.y;
    point->tz += trans.z;
    point->pos = newPos;

    // point has moved to new cell
    if (point->tx >= size_ || point->ty >= size_ || point->tz >= size_ ||
        point->tx < 0 || point->ty < 0 || point->tz < 0) {
        int nexti, nextj, nextk;
        positionToIJK(point->pos, &nexti, &nextj, &nextk);

        // remove grid point from old cell
        Cell* oldCell = cellHashTable.getCell(i, j, k);
        oldCell->removeGridPoint(point);

        // remove cell from hash if empty
        if (oldCell->isEmpty()) {
            cellHashTable.removeCell(oldCell);
            oldCell->reset();
            freeCells_.push_back(oldCell);
        }

        // insert into new cell
        bool isCellInTable = false;
        Cell* cell = cellHashTable.findCell(nexti, nextj, nextk, &isCellInTable);
        if (isCellInTable) {
            cell->insertGridPoint(point);
        }
        else {
            Cell* cell = getNewGridCell(nexti, nextj, nextk);
            cell->insertGridPoint(point);
            cellHashTable.insertCell(cell);
        }

        updateGridPointCellOffset(point, nexti, nextj, nextk);
    }
}

void Grid::update()
{
    // update each cell's cell neighbours
    std::vector<Cell*> cells;
    cellHashTable.getCells(&cells);

    Cell* cell;
    Cell* gc;
    for (unsigned idx = 0; idx < cells.size(); idx++) {
        cell = cells[idx];
        cell->neighbours.clear();

        int ii = cell->i;
        int jj = cell->j;
        int kk = cell->k;

        for (int k = kk - 1; k <= kk + 1; k++) {
            for (int j = jj - 1; j <= jj + 1; j++) {
                for (int i = ii - 1; i <= ii + 1; i++) {
                    if (!(i == ii && j == jj && k == kk)) {
                        bool isInTable = false;
                        gc = cellHashTable.findCell(i, j, k, &isInTable);
                        if (isInTable) {
                            cell->neighbours.push_back(gc);
                        }
                    }
                }
            }
        }
    }
    //std::cout << "-----FINISHED UPDATING GRID-----" << std::endl;
}
