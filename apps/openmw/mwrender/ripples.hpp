#ifndef OPENMW_MWRENDER_RIPPLES_H
#define OPENMW_MWRENDER_RIPPLES_H

#include <array>

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/ref_ptr>

namespace Resource
{
    class ResourceSystem;
}

namespace osg
{
    class Texture;
    class Texture2D;
    class FrameBufferObject;
    class Program;
    class StateSet;
    class NodeVisitor;
    class State;
}

namespace MWRender
{
    class RipplesSurface : public osg::Geometry
    {
    public:
        RipplesSurface(Resource::ResourceSystem* resourceSystem);

        osg::Texture* getColorTexture() const;
        void emit(const osg::Vec3f pos, float sizeInCellUnits);

        void drawImplementation(osg::RenderInfo& renderInfo) const override;
        void traverse(osg::NodeVisitor& nv) override;
        void releaseGLObjects(osg::State* state) const override;

        void setPaused(bool paused) { mPaused = paused; }

        static const std::size_t sRTTSize = 1024;
        static constexpr float sWorldScaleFactor = 2.5f;

    private:
        struct State
        {
            bool mPaused = true;
            osg::ref_ptr<osg::StateSet> mStateset;
        };

        void updateState(const osg::FrameStamp& frameStamp, State& state);
        void setupFragmentPipeline();

        Resource::ResourceSystem* mResourceSystem;
        std::size_t mPositionCount;
        std::array<osg::Vec3f, 100> mPositions;
        std::array<State, 2> mState;
        osg::Vec2f mCurrentPlayerPos;
        osg::Vec2f mLastPlayerPos;
        std::array<osg::ref_ptr<osg::Texture2D>, 2> mTextures;
        std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mFBOs;
        osg::ref_ptr<osg::Program> mProgramBlobber;
        osg::ref_ptr<osg::Program> mProgramSimulation;
        bool mPaused;
        double mLastSimulationTime;
        double mRemainingWaveTime;
    };

    class Ripples : public osg::Camera
    {
    public:
        Ripples(Resource::ResourceSystem* resourceSystem);

        osg::Texture* getColorTexture() const;
        void emit(const osg::Vec3f pos, float sizeInCellUnits);
        void setPaused(bool paused);

    private:
        osg::ref_ptr<RipplesSurface> mRipples;
    };
}

#endif
