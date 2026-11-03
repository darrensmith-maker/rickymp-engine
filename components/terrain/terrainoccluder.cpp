#include "terrainoccluder.hpp"
#include "storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Array>

namespace Terrain
{
    TerrainOccluder::TerrainOccluder(Storage* storage, float cellWorldSize)
        : mStorage(storage)
        , mCellWorldSize(cellWorldSize)
    {
    }

    bool TerrainOccluder::hasTerrainData() const
    {
        return mStorage != nullptr;
    }

    void TerrainOccluder::build(const osg::Vec3f& eyePoint, int radiusCells, std::vector<osg::Vec3f>& outPositions,
        std::vector<unsigned int>& outIndices)
    {
        outPositions.clear();
        outIndices.clear();
        if (!hasTerrainData())
            return;

        const int cellX = static_cast<int>(std::floor(eyePoint.x() / mCellWorldSize));
        const int cellY = static_cast<int>(std::floor(eyePoint.y() / mCellWorldSize));
        const osg::Vec2i cellPos(cellX, cellY);

        if (cellPos == mCachedCellPos && radiusCells == mCachedRadius && !mCachedPositions.empty())
        {
            outPositions = mCachedPositions;
            outIndices = mCachedIndices;
            return;
        }

        const int step = std::max(1, 1 << mLodLevel);
        for (int cy = cellY - radiusCells; cy <= cellY + radiusCells; ++cy)
        {
            for (int cx = cellX - radiusCells; cx <= cellX + radiusCells; ++cx)
            {
                const osg::Vec2f center(cx + 0.5f, cy + 0.5f);
                osg::ref_ptr<osg::Vec3Array> fullRes(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
                colors->setNormalize(true);
                mStorage->fillVertexBuffers(0, 1.0f, center, fullRes, normals, colors);
                if (fullRes->empty())
                    continue;
                const int fullPerSide = static_cast<int>(std::sqrt(static_cast<float>(fullRes->size())));
                if (fullPerSide < 2)
                    continue;
                const int coarsePerSide = (fullPerSide - 1) / step + 1;
                if (coarsePerSide < 2)
                    continue;

                mQuadMins.resize((coarsePerSide - 1) * (coarsePerSide - 1));
                for (int qj = 0; qj < coarsePerSide - 1; ++qj)
                {
                    for (int qi = 0; qi < coarsePerSide - 1; ++qi)
                    {
                        const int startI = qi * step;
                        const int startJ = qj * step;
                        const int endI = std::min((qi + 1) * step, fullPerSide - 1);
                        const int endJ = std::min((qj + 1) * step, fullPerSide - 1);
                        float minH = std::numeric_limits<float>::max();
                        for (int fj = startJ; fj <= endJ; ++fj)
                            for (int fi = startI; fi <= endI; ++fi)
                                minH = std::min(minH, (*fullRes)[fj * fullPerSide + fi].z());
                        mQuadMins[qj * (coarsePerSide - 1) + qi] = minH;
                    }
                }

                const osg::Vec3f worldOffset(center.x() * mCellWorldSize, center.y() * mCellWorldSize, 0.0f);
                const unsigned int baseIndex = static_cast<unsigned int>(outPositions.size());
                for (int cj = 0; cj < coarsePerSide; ++cj)
                {
                    for (int ci = 0; ci < coarsePerSide; ++ci)
                    {
                        float minH = std::numeric_limits<float>::max();
                        for (int dj = -1; dj <= 0; ++dj)
                            for (int di = -1; di <= 0; ++di)
                            {
                                const int qi = ci + di;
                                const int qj = cj + dj;
                                if (qi >= 0 && qi < coarsePerSide - 1 && qj >= 0 && qj < coarsePerSide - 1)
                                    minH = std::min(minH, mQuadMins[qj * (coarsePerSide - 1) + qi]);
                            }
                        const int srcI = std::min(ci * step, fullPerSide - 1);
                        const int srcJ = std::min(cj * step, fullPerSide - 1);
                        osg::Vec3f pos = (*fullRes)[srcJ * fullPerSide + srcI];
                        pos.z() = minH;
                        outPositions.push_back(pos + worldOffset);
                    }
                }

                for (int row = 0; row < coarsePerSide - 1; ++row)
                {
                    for (int col = 0; col < coarsePerSide - 1; ++col)
                    {
                        const unsigned int tl = baseIndex + row * coarsePerSide + col;
                        const unsigned int tr = tl + 1;
                        const unsigned int bl = tl + coarsePerSide;
                        const unsigned int br = bl + 1;
                        outIndices.push_back(tl); outIndices.push_back(bl); outIndices.push_back(tr);
                        outIndices.push_back(tr); outIndices.push_back(bl); outIndices.push_back(br);
                    }
                }
            }
        }

        mCachedCellPos = cellPos;
        mCachedRadius = radiusCells;
        mCachedPositions = outPositions;
        mCachedIndices = outIndices;
    }
}
