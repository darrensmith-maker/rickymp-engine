#include "occlusionculling.hpp"

#include <algorithm>
#include <cmath>

#include <osg/BoundingSphere>
#include <osg/ComputeBoundsVisitor>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osgUtil/CullVisitor>

#include <components/sceneutil/occlusionculling.hpp>
#include <components/terrain/terrainoccluder.hpp>

namespace
{
    class CollectMeshVisitor : public osg::NodeVisitor
    {
    public:
        CollectMeshVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

        void apply(osg::Transform& transform) override
        {
            osg::Matrix matrix;
            if (!mMatrixStack.empty())
                matrix = mMatrixStack.back();
            transform.computeLocalToWorldMatrix(matrix, this);
            mMatrixStack.push_back(matrix);
            traverse(transform);
            mMatrixStack.pop_back();
        }

        void apply(osg::Drawable& drawable) override
        {
            osg::Geometry* geom = drawable.asGeometry();
            if (!geom)
                return;
            const osg::Vec3Array* verts = dynamic_cast<const osg::Vec3Array*>(geom->getVertexArray());
            if (!verts || verts->empty())
                return;

            osg::Matrix matrix;
            if (!mMatrixStack.empty())
                matrix = mMatrixStack.back();
            const unsigned int base = static_cast<unsigned int>(mVertices.size());
            for (osg::Vec3Array::const_iterator it = verts->begin(); it != verts->end(); ++it)
                mVertices.push_back((*it) * matrix);
            for (unsigned int p = 0; p < geom->getNumPrimitiveSets(); ++p)
                collectTriangles(geom->getPrimitiveSet(p), base);
        }

        std::vector<osg::Vec3f> mVertices;
        std::vector<unsigned int> mIndices;

    private:
        void collectTriangles(const osg::PrimitiveSet* pset, unsigned int base)
        {
            const unsigned int count = pset->getNumIndices();
            switch (pset->getMode())
            {
                case GL_TRIANGLES:
                    for (unsigned int i = 0; i + 2 < count; i += 3)
                    { mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i+2)); }
                    break;
                case GL_TRIANGLE_STRIP:
                    for (unsigned int i = 0; i + 2 < count; ++i)
                        if ((i & 1u) == 0)
                        { mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i+2)); }
                        else
                        { mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+2)); }
                    break;
                case GL_TRIANGLE_FAN:
                    for (unsigned int i = 1; i + 1 < count; ++i)
                    { mIndices.push_back(base + pset->index(0)); mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); }
                    break;
                default:
                    break;
            }
        }
        std::vector<osg::Matrix> mMatrixStack;
    };

    static osgUtil::CullVisitor* asCull(osg::NodeVisitor* nv)
    {
        return dynamic_cast<osgUtil::CullVisitor*>(nv);
    }

    static bool shouldUseOcclusionForCamera(osg::Camera* cam)
    {
        if (!cam)
            return false;

        const std::string name = cam->getName();
        if (name == "ShadowCamera" || name == "ReflectionCamera" || name == "RefractionCamera")
            return false;

        // PRE_RENDER cameras are commonly used for RTT passes such as water and shadows.
        // Keep occlusion culling restricted to the main scene camera.
        if (cam->getRenderOrder() != osg::Camera::NESTED_RENDER)
            return false;

        return true;
    }
}

namespace MWRender
{
    OccluderMesh buildSimplifiedMesh(osg::Node* node, int gridRes, float shrinkFactor)
    {
        OccluderMesh mesh;
        CollectMeshVisitor v;
        node->accept(v);
        if (v.mIndices.empty() || v.mVertices.size() < 3)
        {
            osg::ComputeBoundsVisitor cbv;
            node->accept(cbv);
            mesh.aabb = cbv.getBoundingBox();
            return mesh;
        }

        for (std::vector<osg::Vec3f>::const_iterator it = v.mVertices.begin(); it != v.mVertices.end(); ++it)
            mesh.aabb.expandBy(*it);

        const unsigned int res = static_cast<unsigned int>(std::max(1, gridRes));
        const float dx = mesh.aabb.xMax() - mesh.aabb.xMin();
        const float dy = mesh.aabb.yMax() - mesh.aabb.yMin();
        const float dz = mesh.aabb.zMax() - mesh.aabb.zMin();
        const float maxDim = std::max(dx, std::max(dy, dz));
        const float cellSize = maxDim / res;
        if (cellSize > 0.f)
        {
            const unsigned int resX = std::max(1u, static_cast<unsigned int>(std::ceil(dx / cellSize)));
            const unsigned int resY = std::max(1u, static_cast<unsigned int>(std::ceil(dy / cellSize)));
            struct CellData { osg::Vec3f sum; unsigned int count = 0; unsigned int newIndex = 0; };
            std::unordered_map<unsigned int, CellData> cells;
            std::vector<unsigned int> remap(v.mVertices.size());
            for (std::size_t i = 0; i < v.mVertices.size(); ++i)
            {
                const osg::Vec3f& p = v.mVertices[i];
                const unsigned int gx = std::min(static_cast<unsigned int>(std::max((p.x()-mesh.aabb.xMin())/cellSize, 0.f)), resX-1);
                const unsigned int gy = std::min(static_cast<unsigned int>(std::max((p.y()-mesh.aabb.yMin())/cellSize, 0.f)), resY-1);
                const unsigned int gz = std::min(static_cast<unsigned int>(std::max((p.z()-mesh.aabb.zMin())/cellSize, 0.f)), res-1);
                const unsigned int cellId = gx + gy * resX + gz * resX * resY;
                CellData& cell = cells[cellId];
                cell.sum += p;
                cell.count++;
                remap[i] = cellId;
            }
            unsigned int next = 0;
            for (std::unordered_map<unsigned int, CellData>::iterator it = cells.begin(); it != cells.end(); ++it)
            {
                it->second.newIndex = next++;
                mesh.vertices.push_back(it->second.sum / static_cast<float>(it->second.count));
            }
            for (std::size_t i = 0; i + 2 < v.mIndices.size(); i += 3)
            {
                const unsigned int a = cells[remap[v.mIndices[i]]].newIndex;
                const unsigned int b = cells[remap[v.mIndices[i+1]]].newIndex;
                const unsigned int c = cells[remap[v.mIndices[i+2]]].newIndex;
                if (a != b && b != c && a != c)
                { mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c); }
            }
        }
        if (!mesh.vertices.empty())
        {
            osg::Vec3f center(0,0,0);
            for (std::vector<osg::Vec3f>::const_iterator it = mesh.vertices.begin(); it != mesh.vertices.end(); ++it)
                center += *it;
            center /= static_cast<float>(mesh.vertices.size());
            for (std::vector<osg::Vec3f>::iterator it = mesh.vertices.begin(); it != mesh.vertices.end(); ++it)
                *it = center + (*it - center) * shrinkFactor;
        }
        return mesh;
    }

    SceneOcclusionCallback::SceneOcclusionCallback(SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder,
        int radiusCells, bool enableTerrainOccluder)
        : mCuller(culler), mTerrainOccluder(occluder), mRadiusCells(radiusCells), mEnableTerrainOccluder(enableTerrainOccluder), mLastFrameNumber(~0u)
    {
    }

    void SceneOcclusionCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        if (!cv || !mCuller.valid() || !mTerrainOccluder)
        { traverse(node, nv); return; }
        osg::Camera* cam = cv->getCurrentCamera();
        if (!shouldUseOcclusionForCamera(cam))
        { traverse(node, nv); return; }
        const osg::FrameStamp* fs = cv->getFrameStamp();
        const unsigned int frame = fs ? fs->getFrameNumber() : 0u;
        if (frame == mLastFrameNumber)
        { traverse(node, nv); return; }
        mLastFrameNumber = frame;

        mCuller->beginFrame(cam->getViewMatrix(), cam->getProjectionMatrix());
        if (mEnableTerrainOccluder && mTerrainOccluder->hasTerrainData())
        {
            mPositions.clear(); mIndices.clear();
            mTerrainOccluder->build(cv->getEyePoint(), mRadiusCells, mPositions, mIndices);
            if (!mPositions.empty() && !mIndices.empty())
                mCuller->rasterizeOccluder(mPositions, mIndices);
        }
        traverse(node, nv);
    }

    PagedOccluderCallback::PagedOccluderCallback(SceneUtil::OcclusionCuller* culler, float maxDistance)
        : mCuller(culler), mMaxDistanceSq(maxDistance * maxDistance)
    {
    }

    void PagedOccluderCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        if (!cv || !mCuller.valid() || !mCuller->isFrameActive() || !shouldUseOcclusionForCamera(cv->getCurrentCamera()))
        { traverse(node, nv); return; }
        const osg::BoundingSphere& bs = node->getBound();
        if (bs.valid())
        {
            const osg::Matrixd& inv = mCuller->getInverseViewMatrix();
            const osg::Matrixd modelToWorld = (*cv->getModelViewMatrix()) * inv;
            const osg::Vec3f worldCenter = bs.center() * modelToWorld;
            const float r = bs.radius();
            osg::BoundingBox bb(worldCenter.x()-r, worldCenter.y()-r, worldCenter.z()-r, worldCenter.x()+r, worldCenter.y()+r, worldCenter.z()+r);
            if (!mCuller->testVisibleAABB(bb))
                return;
            const osg::Vec3f& eyeWorld = mCuller->getEyeWorld();
            osg::UserDataContainer* udc = node->getUserDataContainer();
            if (udc)
                for (unsigned int i = 0; i < udc->getNumUserObjects(); ++i)
                    if (PagedOccluderData* pod = dynamic_cast<PagedOccluderData*>(udc->getUserObject(i)))
                    {
                        for (std::vector<OccluderMesh>::const_iterator it = pod->mOccluderMeshes.begin(); it != pod->mOccluderMeshes.end(); ++it)
                        {
                            if (it->indices.empty())
                                continue;
                            if ((it->aabb.center() - eyeWorld).length2() > mMaxDistanceSq)
                                continue;
                            mCuller->rasterizeOccluder(it->vertices, it->indices);
                            mCuller->incrementBuildingOccluders(static_cast<unsigned int>(it->indices.size()/3), static_cast<unsigned int>(it->vertices.size()));
                        }
                        break;
                    }
        }
        traverse(node, nv);
    }

    CellOcclusionCallback::CellOcclusionCallback(SceneUtil::OcclusionCuller* culler, float occluderMinRadius,
        float occluderMaxRadius, float occluderShrinkFactor, int occluderMeshResolution,
        int occluderMaxMeshResolution, float occluderInsideThreshold,
        float occluderMaxDistance, bool enableStaticOccluders)
        : mCuller(culler), mOccluderMinRadius(occluderMinRadius), mOccluderMaxRadius(occluderMaxRadius),
          mOccluderShrinkFactor(occluderShrinkFactor), mOccluderMeshResolution(occluderMeshResolution),
          mOccluderMaxMeshResolution(occluderMaxMeshResolution), mOccluderInsideThreshold(occluderInsideThreshold),
          mOccluderMaxDistanceSq(occluderMaxDistance * occluderMaxDistance), mEnableStaticOccluders(enableStaticOccluders)
    {
    }

    const OccluderMesh& CellOcclusionCallback::getOccluderMesh(osg::Node* node)
    {
        std::unordered_map<osg::Node*, OccluderMesh>::iterator it = mMeshCache.find(node);
        if (it != mMeshCache.end())
            return it->second;
        int meshRes = mOccluderMeshResolution;
        const float radius = node->getBound().radius();
        if (radius > mOccluderMinRadius && mOccluderMinRadius > 0.f)
        {
            const float scale = radius / mOccluderMinRadius;
            meshRes = std::max(mOccluderMeshResolution, std::min(mOccluderMaxMeshResolution, static_cast<int>(mOccluderMeshResolution * scale)));
        }
        return mMeshCache.emplace(node, buildSimplifiedMesh(node, meshRes, mOccluderShrinkFactor)).first->second;
    }

    void CellOcclusionCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        osg::Group* group = node ? node->asGroup() : nullptr;
        if (!cv || !group || !mCuller.valid() || !mCuller->isFrameActive() || !shouldUseOcclusionForCamera(cv->getCurrentCamera()))
        { traverse(node, nv); return; }

        const osg::BoundingSphere& cellBS = group->getBound();
        if (cellBS.valid())
        {
            osg::BoundingBox cellBB; cellBB.expandBy(cellBS);
            if (!mCuller->testVisibleAABB(cellBB))
                return;
        }

        const unsigned int count = group->getNumChildren();
        for (unsigned int i = 0; i < count; ++i)
        {
            osg::Node* child = group->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();
            if (!bs.valid() || bs.radius() < mOccluderMinRadius)
                continue;
            if (bs.radius() > mOccluderMaxRadius)
            {
                osg::BoundingBox bb; bb.expandBy(bs);
                if (mCuller->testVisibleAABB(bb)) child->accept(*cv);
                continue;
            }
            const OccluderMesh& mesh = getOccluderMesh(child);
            if (!mesh.aabb.valid())
                continue;
            if (mCuller->testVisibleAABB(mesh.aabb))
            {
                if (mEnableStaticOccluders && !mesh.indices.empty() && (bs.center() - cv->getEyePoint()).length2() < mOccluderMaxDistanceSq)
                {
                    const osg::Vec3f center = mesh.aabb.center();
                    const osg::Vec3f halfExtent = (osg::Vec3f(mesh.aabb.xMax(), mesh.aabb.yMax(), mesh.aabb.zMax()) - center) * mOccluderInsideThreshold;
                    osg::BoundingBox scaledBB; scaledBB.expandBy(center - halfExtent); scaledBB.expandBy(center + halfExtent);
                    if (!scaledBB.contains(cv->getEyePoint()))
                    {
                        mCuller->rasterizeOccluder(mesh.vertices, mesh.indices);
                        mCuller->incrementBuildingOccluders(static_cast<unsigned int>(mesh.indices.size()/3), static_cast<unsigned int>(mesh.vertices.size()));
                    }
                }
                child->accept(*cv);
            }
        }

        for (unsigned int i = 0; i < count; ++i)
        {
            osg::Node* child = group->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();
            if (!bs.valid())
            { child->accept(*cv); continue; }
            if (bs.radius() >= mOccluderMinRadius)
                continue;
            bool skipOcclusion = false;
            child->getUserValue("skipOcclusion", skipOcclusion);
            osg::BoundingBox bb; bb.expandBy(bs);
            if (skipOcclusion || mCuller->testVisibleAABB(bb))
                child->accept(*cv);
        }
    }
}
