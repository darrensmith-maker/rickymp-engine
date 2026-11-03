#ifndef OPENMW_MWRENDER_OCCLUSIONCULLING_H
#define OPENMW_MWRENDER_OCCLUSIONCULLING_H

#include <osg/BoundingBox>
#include <osg/Object>
#include <osg/NodeCallback>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <unordered_map>
#include <vector>

namespace osg
{
    class Group;
    class Node;
    class NodeVisitor;
}

namespace SceneUtil
{
    class OcclusionCuller;
}

namespace Terrain
{
    class TerrainOccluder;
}

namespace MWRender
{
    struct OccluderMesh
    {
        osg::BoundingBox aabb;
        std::vector<osg::Vec3f> vertices;
        std::vector<unsigned int> indices;
    };

    OccluderMesh buildSimplifiedMesh(osg::Node* node, int gridRes, float shrinkFactor);

    class PagedOccluderData : public osg::Object
    {
    public:
        PagedOccluderData() = default;
        PagedOccluderData(const PagedOccluderData& copy, const osg::CopyOp& copyop = osg::CopyOp())
            : osg::Object(copy, copyop), mOccluderMeshes(copy.mOccluderMeshes) {}
        META_Object(MWRender, PagedOccluderData)
        std::vector<OccluderMesh> mOccluderMeshes;
    };

    class SceneOcclusionCallback : public osg::NodeCallback
    {
    public:
        SceneOcclusionCallback(SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder,
            int radiusCells, bool enableTerrainOccluder);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        Terrain::TerrainOccluder* mTerrainOccluder;
        int mRadiusCells;
        bool mEnableTerrainOccluder;
        unsigned int mLastFrameNumber;
        std::vector<osg::Vec3f> mPositions;
        std::vector<unsigned int> mIndices;
    };

    class PagedOccluderCallback : public osg::NodeCallback
    {
    public:
        PagedOccluderCallback(SceneUtil::OcclusionCuller* culler, float maxDistance);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        float mMaxDistanceSq;
    };

    class CellOcclusionCallback : public osg::NodeCallback
    {
    public:
        CellOcclusionCallback(SceneUtil::OcclusionCuller* culler, float occluderMinRadius,
            float occluderMaxRadius, float occluderShrinkFactor, int occluderMeshResolution,
            int occluderMaxMeshResolution, float occluderInsideThreshold,
            float occluderMaxDistance, bool enableStaticOccluders);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        const OccluderMesh& getOccluderMesh(osg::Node* node);
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        float mOccluderMinRadius;
        float mOccluderMaxRadius;
        float mOccluderShrinkFactor;
        int mOccluderMeshResolution;
        int mOccluderMaxMeshResolution;
        float mOccluderInsideThreshold;
        float mOccluderMaxDistanceSq;
        bool mEnableStaticOccluders;
        std::unordered_map<osg::Node*, OccluderMesh> mMeshCache;
    };
}

#endif
