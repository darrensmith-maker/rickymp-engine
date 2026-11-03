#include "ripples.hpp"

#include <cmath>
#include <string>

#include <osg/FrameBufferObject>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Viewport>
#include <osgUtil/CullVisitor>

#include <components/debug/debuglog.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>

#include "vismask.hpp"
#include "../mwmechanics/actorutil.hpp"

#ifndef GL_RGBA16F
#  ifdef GL_RGBA16F_ARB
#    define GL_RGBA16F GL_RGBA16F_ARB
#  else
#    define GL_RGBA16F 0x881A
#  endif
#endif

namespace MWRender
{
    RipplesSurface::RipplesSurface(Resource::ResourceSystem* resourceSystem)
        : osg::Geometry()
        , mResourceSystem(resourceSystem)
        , mPositionCount(0)
        , mPaused(false)
        , mLastSimulationTime(0.0)
        , mRemainingWaveTime(0.0)
    {
        setUseDisplayList(false);
        setUseVertexBufferObjects(true);

        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        verts->push_back(osg::Vec3f(-1.f, -1.f, 0.f));
        verts->push_back(osg::Vec3f(-1.f, 3.f, 0.f));
        verts->push_back(osg::Vec3f(3.f, -1.f, 0.f));
        setVertexArray(verts);

        setCullingActive(false);
        addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, 3));

        for (std::size_t i = 0; i < mState.size(); ++i)
        {
            osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
            stateset->addUniform(new osg::Uniform("imageIn", 0));
            stateset->addUniform(new osg::Uniform("offset", osg::Vec2f()));
            stateset->addUniform(new osg::Uniform("positionCount", 0));
            stateset->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC3, "positions", 100));
            stateset->setAttributeAndModes(new osg::Viewport(0, 0, static_cast<int>(sRTTSize), static_cast<int>(sRTTSize)));
            mState[i].mStateset = stateset;
        }

        for (std::size_t i = 0; i < mTextures.size(); ++i)
        {
            osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
            texture->setSourceFormat(GL_RGBA);
            texture->setInternalFormat(GL_RGBA16F);
            texture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture::LINEAR);
            texture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture::LINEAR);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_BORDER);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_BORDER);
            texture->setBorderColor(osg::Vec4(0.f, 0.f, 0.f, 0.f));
            texture->setTextureSize(static_cast<int>(sRTTSize), static_cast<int>(sRTTSize));
            mTextures[i] = texture;

            mFBOs[i] = new osg::FrameBufferObject;
            mFBOs[i]->setAttachment(osg::Camera::COLOR_BUFFER0, osg::FrameBufferAttachment(mTextures[i]));
        }

        setupFragmentPipeline();

        if (mProgramBlobber != nullptr && mProgramSimulation != nullptr)
            Log(Debug::Info) << "Initialized fragment shader pipeline for water ripples";

        setCullCallback(new osg::NodeCallback);
        setUpdateCallback(new osg::NodeCallback);
    }

    void RipplesSurface::setupFragmentPipeline()
    {
        Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();
        Shader::ShaderManager::DefineMap defineMap;
        defineMap["rippleMapSize"] = std::to_string(static_cast<double>(sRTTSize)) + ".0";

        osg::ref_ptr<osg::Shader> vertex = shaderManager.getShader("fullscreen_tri.vert", Shader::ShaderManager::DefineMap(), osg::Shader::VERTEX);
        osg::ref_ptr<osg::Shader> blobber = shaderManager.getShader("ripples_blobber.frag", defineMap, osg::Shader::FRAGMENT);
        osg::ref_ptr<osg::Shader> simulate = shaderManager.getShader("ripples_simulate.frag", defineMap, osg::Shader::FRAGMENT);
        if (vertex == nullptr || blobber == nullptr || simulate == nullptr)
        {
            Log(Debug::Error) << "Failed to load shaders required for water ripple pipeline";
            return;
        }

        mProgramBlobber = shaderManager.getProgram(vertex, blobber);
        mProgramSimulation = shaderManager.getProgram(vertex, simulate);
    }

    void RipplesSurface::updateState(const osg::FrameStamp& frameStamp, State& state)
    {
        state.mPaused = mPaused;
        if (mPaused)
            return;

        const double updateFrequency = 60.0;
        const double updatePeriod = 1.0 / updateFrequency;
        const double simulationTime = frameStamp.getSimulationTime();
        const double frameDuration = simulationTime - mLastSimulationTime;
        mLastSimulationTime = simulationTime;

        mRemainingWaveTime += frameDuration;
        const double ticks = std::floor(mRemainingWaveTime * updateFrequency);
        mRemainingWaveTime -= ticks * updatePeriod;

        if (ticks == 0.0)
        {
            state.mPaused = true;
            return;
        }

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const ESM::Position& playerPos = player.getRefData().getPosition();

        mCurrentPlayerPos = osg::Vec2f(
            std::floor(playerPos.pos[0] / sWorldScaleFactor),
            std::floor(playerPos.pos[1] / sWorldScaleFactor));
        const osg::Vec2f offset = mCurrentPlayerPos - mLastPlayerPos;
        mLastPlayerPos = mCurrentPlayerPos;

        state.mStateset->getUniform("positionCount")->set(static_cast<int>(mPositionCount));
        state.mStateset->getUniform("offset")->set(offset);

        osg::Uniform* positions = state.mStateset->getUniform("positions");
        for (std::size_t i = 0; i < mPositionCount; ++i)
        {
            osg::Vec3f pos = mPositions[i]
                - osg::Vec3f(mCurrentPlayerPos.x() * sWorldScaleFactor, mCurrentPlayerPos.y() * sWorldScaleFactor, 0.f)
                + osg::Vec3f(static_cast<float>(sRTTSize) * sWorldScaleFactor / 2.f,
                             static_cast<float>(sRTTSize) * sWorldScaleFactor / 2.f, 0.f);
            pos /= sWorldScaleFactor;
            positions->setElement(static_cast<unsigned int>(i), pos);
        }
        positions->dirty();

        mPositionCount = 0;
    }

    void RipplesSurface::traverse(osg::NodeVisitor& nv)
    {
        const osg::FrameStamp* frameStamp = nv.getFrameStamp();
        if (frameStamp == nullptr)
            return;

        if (nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR)
            updateState(*frameStamp, mState[frameStamp->getFrameNumber() % 2]);

        osg::Geometry::traverse(nv);
    }

    void RipplesSurface::drawImplementation(osg::RenderInfo& renderInfo) const
    {
        if (mProgramBlobber == nullptr || mProgramSimulation == nullptr)
            return;

        osg::State& state = *renderInfo.getState();
        const std::size_t currentFrame = state.getFrameStamp()->getFrameNumber() % 2;
        const State& frameState = mState[currentFrame];
        if (frameState.mPaused)
            return;

        state.pushStateSet(frameState.mStateset);
        state.apply();

        state.applyAttribute(mProgramBlobber.get());
        if (state.getLastAppliedProgramObject())
        {
            const osg::State::UniformMap& uniformMap = state.getUniformMap();
            for (osg::State::UniformMap::const_iterator it = uniformMap.begin(); it != uniformMap.end(); ++it)
                if (!it->second.uniformVec.empty())
                    state.getLastAppliedProgramObject()->apply(*(it->second.uniformVec.back().first));
        }
        mFBOs[1]->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        state.applyTextureAttribute(0, mTextures[0].get());
        osg::Geometry::drawImplementation(renderInfo);

        state.applyAttribute(mProgramSimulation.get());
        if (state.getLastAppliedProgramObject())
        {
            const osg::State::UniformMap& uniformMap = state.getUniformMap();
            for (osg::State::UniformMap::const_iterator it = uniformMap.begin(); it != uniformMap.end(); ++it)
                if (!it->second.uniformVec.empty())
                    state.getLastAppliedProgramObject()->apply(*(it->second.uniformVec.back().first));
        }
        mFBOs[0]->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        state.applyTextureAttribute(0, mTextures[1].get());
        osg::Geometry::drawImplementation(renderInfo);

        state.popStateSet();
    }

    osg::Texture* RipplesSurface::getColorTexture() const
    {
        return mTextures[0].get();
    }

    void RipplesSurface::emit(const osg::Vec3f pos, float sizeInCellUnits)
    {
        if (mPositionCount >= mPositions.size())
            return;

        mPositions[mPositionCount] = osg::Vec3f(pos.x(), pos.y(), sizeInCellUnits);
        ++mPositionCount;
    }

    void RipplesSurface::releaseGLObjects(osg::State* state) const
    {
        for (std::size_t i = 0; i < mTextures.size(); ++i)
            if (mTextures[i])
                mTextures[i]->releaseGLObjects(state);
        for (std::size_t i = 0; i < mFBOs.size(); ++i)
            if (mFBOs[i])
                mFBOs[i]->releaseGLObjects(state);

        if (mProgramBlobber)
            mProgramBlobber->releaseGLObjects(state);
        if (mProgramSimulation)
            mProgramSimulation->releaseGLObjects(state);
    }

    Ripples::Ripples(Resource::ResourceSystem* resourceSystem)
        : osg::Camera()
        , mRipples(new RipplesSurface(resourceSystem))
    {
        getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        setRenderOrder(osg::Camera::PRE_RENDER);
        setReferenceFrame(osg::Camera::ABSOLUTE_RF);
        setNodeMask(Mask_RenderToTexture);
        setClearMask(GL_NONE);
        setViewport(0, 0, static_cast<int>(RipplesSurface::sRTTSize), static_cast<int>(RipplesSurface::sRTTSize));
        addChild(mRipples);
        setCullingActive(false);
        setImplicitBufferAttachmentMask(0, 0);
    }

    osg::Texture* Ripples::getColorTexture() const
    {
        return mRipples->getColorTexture();
    }

    void Ripples::emit(const osg::Vec3f pos, float sizeInCellUnits)
    {
        mRipples->emit(pos, sizeInCellUnits);
    }

    void Ripples::setPaused(bool paused)
    {
        mRipples->setPaused(paused);
    }
}
