#ifndef OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H
#define OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H

#include <osg/Vec2i>
#include <osg/Vec3f>
#include <vector>

namespace Terrain
{
    class Storage;

    class TerrainOccluder
    {
    public:
        TerrainOccluder(Storage* storage, float cellWorldSize);
        void setLodLevel(int lod) { mLodLevel = lod; }
        void build(const osg::Vec3f& eyePoint, int radiusCells, std::vector<osg::Vec3f>& outPositions,
            std::vector<unsigned int>& outIndices);
        bool hasTerrainData() const;
    private:
        Storage* mStorage;
        float mCellWorldSize;
        int mLodLevel = 3;
        osg::Vec2i mCachedCellPos;
        int mCachedRadius = -1;
        std::vector<osg::Vec3f> mCachedPositions;
        std::vector<unsigned int> mCachedIndices;
        std::vector<float> mQuadMins;
    };
}

#endif
