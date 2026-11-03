#include "nativeeffects.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/Program>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Viewport>

#include <components/debug/debuglog.hpp>
#include <components/settings/settings.hpp>
#include <components/shader/shadermanager.hpp>

#include "vismask.hpp"

namespace MWRender
{
    std::atomic<NativeEffectsProcessor*> NativeEffectsProcessor::sActiveProcessor{nullptr};

    class NativeEffectsProcessor::FramebufferCopyCallback : public osg::Camera::DrawCallback
    {
    public:
        explicit FramebufferCopyCallback(NativeEffectsProcessor* owner)
            : mOwner(owner)
        {
        }

        void detach() { mOwner.store(nullptr, std::memory_order_release); }

        void operator()(osg::RenderInfo& renderInfo) const override
        {
            NativeEffectsProcessor* owner = mOwner.load(std::memory_order_acquire);
            if (!owner)
                return;

            owner->copyFramebuffer(renderInfo);
            if (owner->mOriginalPostDrawCallback)
                (*owner->mOriginalPostDrawCallback)(renderInfo);
        }

    private:
        std::atomic<NativeEffectsProcessor*> mOwner;
    };

    NativeEffectsProcessor::NativeEffectsProcessor(osg::Camera* mainCamera, osg::Group* rootNode,
        Shader::ShaderManager& shaderManager)
        : mMainCamera(mainCamera)
        , mRootNode(rootNode)
        , mOriginalPostDrawCallback(mainCamera ? mainCamera->getPostDrawCallback() : nullptr)
    {
        if (!mainCamera || !rootNode)
        {
            Log(Debug::Error) << "Cannot create ArenaMP native effects without a main camera/root node";
            return;
        }

        // Native post chain: SMAA/bloom plus Luxora-inspired atmospheric fog,
        // classic screen-space god rays, CAS sharpening and output dithering.
        // Reflection rendering remains owned by the water pipeline.
        mSceneTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mDepthTexture = createTexture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, false);
        mOverlayDepthTexture = createTexture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, false);
        mBloomHorizontalTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mBloomVerticalTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mEdgeTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false);
        mWeightTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);

        const Shader::ShaderManager::DefineMap defines;
        osg::ref_ptr<osg::Shader> vertex = shaderManager.getShader("fullscreen_tri.vert", defines, osg::Shader::VERTEX);
        auto getProgram = [&](const char* fragmentName) -> osg::ref_ptr<osg::Program>
        {
            osg::ref_ptr<osg::Shader> fragment = shaderManager.getShader(fragmentName, defines, osg::Shader::FRAGMENT);
            if (!vertex || !fragment)
                return nullptr;
            return shaderManager.getProgram(vertex, fragment);
        };

        osg::ref_ptr<osg::Program> bloomHorizontalProgram = getProgram("bloom_extract_horizontal.frag");
        osg::ref_ptr<osg::Program> bloomVerticalProgram = getProgram("bloom_vertical.frag");
        osg::ref_ptr<osg::Program> edgeProgram = getProgram("native_smaa_edge.frag");
        osg::ref_ptr<osg::Program> weightProgram = getProgram("native_smaa_weights.frag");
        osg::ref_ptr<osg::Program> finalProgram = getProgram("native_final.frag");

        if (!bloomHorizontalProgram || !bloomVerticalProgram
            || !edgeProgram || !weightProgram || !finalProgram)
        {
            Log(Debug::Error) << "Failed to create ArenaMP native SMAA/bloom programs";
            return;
        }

        // All native passes finish before MyGUI (POST_RENDER order 0), keeping
        // HUD/menu rendering untouched.
        mBloomHorizontalCamera = createCamera(-9, true);
        mBloomVerticalCamera = createCamera(-8, true);
        mEdgeCamera = createCamera(-7, true);
        mWeightCamera = createCamera(-6, true);
        mFinalCamera = createCamera(-5, false);

        mBloomHorizontalCamera->attach(osg::Camera::COLOR_BUFFER0, mBloomHorizontalTexture);
        mBloomVerticalCamera->attach(osg::Camera::COLOR_BUFFER0, mBloomVerticalTexture);
        mEdgeCamera->attach(osg::Camera::COLOR_BUFFER0, mEdgeTexture);
        mWeightCamera->attach(osg::Camera::COLOR_BUFFER0, mWeightTexture);


        osg::ref_ptr<osg::Geode> bloomHorizontalPass = createFullscreenPass(bloomHorizontalProgram);
        mBloomHorizontalState = bloomHorizontalPass->getOrCreateStateSet();
        mBloomHorizontalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mBloomHorizontalState->addUniform(new osg::Uniform("sceneTexture", 0));
        mInverseBloomSizeHorizontal = new osg::Uniform("inverseBloomSize", osg::Vec2f(1.f, 1.f));
        mBloomHorizontalState->addUniform(new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f)));
        mBloomHorizontalState->addUniform(mInverseBloomSizeHorizontal);
        mBloomThresholdUniform = new osg::Uniform("bloomThreshold", 0.40f);
        mBloomSoftKneeUniform = new osg::Uniform("bloomSoftKnee", 0.67f);
        mBloomRadiusHorizontalUniform = new osg::Uniform("bloomRadius", 3.f);
        mBloomHorizontalState->addUniform(mBloomThresholdUniform);
        mBloomHorizontalState->addUniform(mBloomSoftKneeUniform);
        mBloomHorizontalState->addUniform(mBloomRadiusHorizontalUniform);
        mBloomHorizontalCamera->addChild(bloomHorizontalPass);

        osg::ref_ptr<osg::Geode> bloomVerticalPass = createFullscreenPass(bloomVerticalProgram);
        osg::StateSet* bloomVerticalState = bloomVerticalPass->getOrCreateStateSet();
        bloomVerticalState->setTextureAttributeAndModes(0, mBloomHorizontalTexture, osg::StateAttribute::ON);
        bloomVerticalState->addUniform(new osg::Uniform("bloomTexture", 0));
        mInverseBloomSizeVertical = new osg::Uniform("inverseBloomSize", osg::Vec2f(1.f, 1.f));
        mBloomRadiusVerticalUniform = new osg::Uniform("bloomRadius", 3.f);
        bloomVerticalState->addUniform(mInverseBloomSizeVertical);
        bloomVerticalState->addUniform(mBloomRadiusVerticalUniform);
        mBloomVerticalCamera->addChild(bloomVerticalPass);

        osg::ref_ptr<osg::Geode> edgePass = createFullscreenPass(edgeProgram);
        mEdgeState = edgePass->getOrCreateStateSet();
        mEdgeState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mEdgeState->setTextureAttributeAndModes(1, mDepthTexture, osg::StateAttribute::ON);
        mEdgeState->addUniform(new osg::Uniform("sceneTexture", 0));
        mEdgeState->addUniform(new osg::Uniform("depthTexture", 1));
        mInverseSceneSizeEdge = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        mSmaaThresholdUniform = new osg::Uniform("smaaThreshold", 0.10f);
        mEdgeState->addUniform(mInverseSceneSizeEdge);
        mEdgeState->addUniform(mSmaaThresholdUniform);
        mEdgeCamera->addChild(edgePass);

        osg::ref_ptr<osg::Geode> weightPass = createFullscreenPass(weightProgram);
        osg::StateSet* weightState = weightPass->getOrCreateStateSet();
        weightState->setTextureAttributeAndModes(0, mEdgeTexture, osg::StateAttribute::ON);
        weightState->addUniform(new osg::Uniform("edgeTexture", 0));
        mInverseSceneSizeWeight = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        weightState->addUniform(mInverseSceneSizeWeight);
        mWeightCamera->addChild(weightPass);

        osg::ref_ptr<osg::Geode> finalPass = createFullscreenPass(finalProgram);
        mFinalState = finalPass->getOrCreateStateSet();
        mFinalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(1, mWeightTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(2, mBloomVerticalTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(3, mDepthTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(4, mOverlayDepthTexture, osg::StateAttribute::ON);
        mFinalState->addUniform(new osg::Uniform("sceneTexture", 0));
        mFinalState->addUniform(new osg::Uniform("weightTexture", 1));
        mFinalState->addUniform(new osg::Uniform("bloomTexture", 2));
        mFinalState->addUniform(new osg::Uniform("depthTexture", 3));
        mFinalState->addUniform(new osg::Uniform("overlayDepthTexture", 4));
        mInverseSceneSizeFinal = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        mSmaaEnabledUniform = new osg::Uniform("smaaEnabled", 0.f);
        mBloomEnabledUniform = new osg::Uniform("bloomEnabled", 0.f);
        mBloomIntensityUniform = new osg::Uniform("bloomIntensity", 0.50f);
        mAtmosphericFogEnabledUniform = new osg::Uniform("atmosphericFogEnabled", 0.f);
        mAtmosphericFogStrengthUniform = new osg::Uniform("atmosphericFogStrength", 0.28f);
        mGodRaysEnabledUniform = new osg::Uniform("godRaysEnabled", 0.f);
        mGodRaysStrengthUniform = new osg::Uniform("godRaysStrength", 0.65f);
        mSharpeningEnabledUniform = new osg::Uniform("sharpeningEnabled", 0.f);
        mSharpeningStrengthUniform = new osg::Uniform("sharpeningStrength", 0.32f);
        mDitheringEnabledUniform = new osg::Uniform("ditheringEnabled", 0.f);
        mFogColorUniform = new osg::Uniform("fogColor", osg::Vec3f(0.5f, 0.5f, 0.5f));
        mFogStartUniform = new osg::Uniform("fogStart", 0.f);
        mFogEndUniform = new osg::Uniform("fogEnd", 8192.f);
        mCameraNearUniform = new osg::Uniform("cameraNear", 1.f);
        mCameraFarUniform = new osg::Uniform("cameraFar", 8192.f);
        mCameraWorldPositionUniform = new osg::Uniform("cameraWorldPosition", osg::Vec3f());
        mCameraRightUniform = new osg::Uniform("cameraRight", osg::Vec3f(1.f, 0.f, 0.f));
        mCameraUpUniform = new osg::Uniform("cameraUp", osg::Vec3f(0.f, 0.f, 1.f));
        mCameraForwardUniform = new osg::Uniform("cameraForward", osg::Vec3f(0.f, 1.f, 0.f));
        mCameraTanHalfFovYUniform = new osg::Uniform("cameraTanHalfFovY", 0.75f);
        mCameraAspectUniform = new osg::Uniform("cameraAspect", 1.f);
        mEnvironmentExteriorUniform = new osg::Uniform("environmentExterior", 1.f);
        mEnvironmentUnderwaterUniform = new osg::Uniform("environmentUnderwater", 0.f);
        mSunScreenPositionUniform = new osg::Uniform("sunScreenPosition", osg::Vec2f(0.5f, 0.5f));
        mSunVisibleUniform = new osg::Uniform("sunVisible", 0.f);
        mSunColorUniform = new osg::Uniform("sunColor", osg::Vec3f(1.f, 0.9f, 0.75f));
        mFirstPersonViewUniform = new osg::Uniform("firstPersonView", 0.f);
        mFrameTimeUniform = new osg::Uniform("frameTime", 0.f);

        mFinalState->addUniform(mInverseSceneSizeFinal);
        mFinalState->addUniform(mSmaaEnabledUniform);
        mFinalState->addUniform(mBloomEnabledUniform);
        mFinalState->addUniform(mBloomIntensityUniform);
        mFinalState->addUniform(mAtmosphericFogEnabledUniform);
        mFinalState->addUniform(mAtmosphericFogStrengthUniform);
        mFinalState->addUniform(mGodRaysEnabledUniform);
        mFinalState->addUniform(mGodRaysStrengthUniform);
        mFinalState->addUniform(mSharpeningEnabledUniform);
        mFinalState->addUniform(mSharpeningStrengthUniform);
        mFinalState->addUniform(mDitheringEnabledUniform);
        mFinalState->addUniform(mFogColorUniform);
        mFinalState->addUniform(mFogStartUniform);
        mFinalState->addUniform(mFogEndUniform);
        mFinalState->addUniform(mCameraNearUniform);
        mFinalState->addUniform(mCameraFarUniform);
        mFinalState->addUniform(mCameraWorldPositionUniform);
        mFinalState->addUniform(mCameraRightUniform);
        mFinalState->addUniform(mCameraUpUniform);
        mFinalState->addUniform(mCameraForwardUniform);
        mFinalState->addUniform(mCameraTanHalfFovYUniform);
        mFinalState->addUniform(mCameraAspectUniform);
        mFinalState->addUniform(mEnvironmentExteriorUniform);
        mFinalState->addUniform(mEnvironmentUnderwaterUniform);
        mFinalState->addUniform(mSunScreenPositionUniform);
        mFinalState->addUniform(mSunVisibleUniform);
        mFinalState->addUniform(mSunColorUniform);
        mFinalState->addUniform(mFirstPersonViewUniform);
        mFinalState->addUniform(mFrameTimeUniform);
        mFinalCamera->addChild(finalPass);

        mRootNode->addChild(mBloomHorizontalCamera);
        mRootNode->addChild(mBloomVerticalCamera);
        mRootNode->addChild(mEdgeCamera);
        mRootNode->addChild(mWeightCamera);
        mRootNode->addChild(mFinalCamera);

        mFramebufferCopyCallback = new FramebufferCopyCallback(this);
        if (mMainCamera.valid())
            mMainCamera->setPostDrawCallback(mFramebufferCopyCallback.get());

        mReady = true;
        sActiveProcessor.store(this, std::memory_order_release);
        reloadSettings();
        applyPassVisibility();
    }

    NativeEffectsProcessor::~NativeEffectsProcessor()
    {
        NativeEffectsProcessor* expected = this;
        sActiveProcessor.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        mEnabled = false;
        applyPassVisibility();
        if (mFramebufferCopyCallback)
            mFramebufferCopyCallback->detach();
        if (mMainCamera.valid() && mMainCamera->getPostDrawCallback() == mFramebufferCopyCallback.get())
            mMainCamera->setPostDrawCallback(mOriginalPostDrawCallback.get());

        if (mRootNode.valid())
        {
            if (mBloomHorizontalCamera) mRootNode->removeChild(mBloomHorizontalCamera);
            if (mBloomVerticalCamera) mRootNode->removeChild(mBloomVerticalCamera);
            if (mEdgeCamera) mRootNode->removeChild(mEdgeCamera);
            if (mWeightCamera) mRootNode->removeChild(mWeightCamera);
            if (mFinalCamera) mRootNode->removeChild(mFinalCamera);
        }
    }

    osg::ref_ptr<osg::Texture2D> NativeEffectsProcessor::createTexture(
        int internalFormat, unsigned int sourceFormat, unsigned int sourceType, bool linear)
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setInternalFormat(internalFormat);
        texture->setSourceFormat(sourceFormat);
        texture->setSourceType(sourceType);
        texture->setFilter(osg::Texture::MIN_FILTER, linear ? osg::Texture::LINEAR : osg::Texture::NEAREST);
        texture->setFilter(osg::Texture::MAG_FILTER, linear ? osg::Texture::LINEAR : osg::Texture::NEAREST);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Camera> NativeEffectsProcessor::createCamera(int orderNum, bool renderToTexture)
    {
        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        camera->setProjectionResizePolicy(osg::Camera::FIXED);
        camera->setProjectionMatrix(osg::Matrix::identity());
        camera->setViewMatrix(osg::Matrix::identity());
        camera->setRenderOrder(osg::Camera::POST_RENDER, orderNum);
        camera->setRenderTargetImplementation(renderToTexture ? osg::Camera::FRAME_BUFFER_OBJECT : osg::Camera::FRAME_BUFFER);
        camera->setClearMask(renderToTexture ? GL_COLOR_BUFFER_BIT : 0);
        camera->setClearColor(osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        camera->setImplicitBufferAttachmentMask(0, 0);
        camera->setAllowEventFocus(false);
        camera->setCullingActive(false);
        return camera;
    }

    osg::ref_ptr<osg::Geode> NativeEffectsProcessor::createFullscreenPass(osg::Program* program)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);
        geometry->setCullingActive(false);
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(-1.f, -1.f, 0.f));
        vertices->push_back(osg::Vec3f(-1.f, 3.f, 0.f));
        vertices->push_back(osg::Vec3f(3.f, -1.f, 0.f));
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, 3));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->setCullingActive(false);
        geode->addDrawable(geometry);
        osg::StateSet* state = geode->getOrCreateStateSet();
        state->setAttributeAndModes(program, osg::StateAttribute::ON);
        state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_BLEND, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        return geode;
    }

    void NativeEffectsProcessor::copyFramebuffer(osg::RenderInfo& renderInfo)
    {
        if (!mEnabled || !mSceneTexture || !mDepthTexture || !mOverlayDepthTexture
            || !renderInfo.getState() || !mMainCamera.valid())
            return;
        osg::Viewport* viewport = mMainCamera->getViewport();
        if (!viewport || viewport->width() <= 0.0 || viewport->height() <= 0.0)
            return;

        const int x = static_cast<int>(viewport->x());
        const int y = static_cast<int>(viewport->y());
        const int w = std::max(1, static_cast<int>(viewport->width()));
        const int h = std::max(1, static_cast<int>(viewport->height()));

        // Scene colour is always captured after the complete frame, including
        // first-person hands/weapons. The final depth is kept separately so the
        // shader can protect that foreground overlay from atmospheric effects.
        mSceneTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);
        mOverlayDepthTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);

        // In third person the post-draw depth is the real world depth. In first
        // person it has already been cleared by DepthClearCallback, so preserve
        // the copy taken immediately before that clear instead.
        const bool hadPreClearDepth = mWorldDepthCapturedBeforeFirstPerson.exchange(false, std::memory_order_acq_rel);
        if (!hadPreClearDepth)
            mDepthTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);

        mCaptureReady.store(true, std::memory_order_release);
    }

    void NativeEffectsProcessor::captureWorldDepth(osg::RenderInfo& renderInfo)
    {
        if (!mEnabled || !mDepthTexture || !renderInfo.getState() || !mMainCamera.valid())
            return;
        osg::Viewport* viewport = mMainCamera->getViewport();
        if (!viewport || viewport->width() <= 0.0 || viewport->height() <= 0.0)
            return;

        const int x = static_cast<int>(viewport->x());
        const int y = static_cast<int>(viewport->y());
        const int w = std::max(1, static_cast<int>(viewport->width()));
        const int h = std::max(1, static_cast<int>(viewport->height()));
        mDepthTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);
        mWorldDepthCapturedBeforeFirstPerson.store(true, std::memory_order_release);
    }

    void NativeEffectsProcessor::captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo)
    {
        NativeEffectsProcessor* processor = sActiveProcessor.load(std::memory_order_acquire);
        if (processor)
            processor->captureWorldDepth(renderInfo);
    }

    void NativeEffectsProcessor::reloadSettings()
    {
        if (!mReady)
            return;

        mSmaaEnabled = Settings::Manager::getBool("smaa enabled", "Shaders");
        mBloomEnabled = Settings::Manager::getBool("bloom enabled", "Shaders");
        mAtmosphericFogEnabled = Settings::Manager::getBool("atmospheric fog enabled", "Shaders");
        mGodRaysEnabled = Settings::Manager::getBool("god rays enabled", "Shaders");
        mSharpeningEnabled = Settings::Manager::getBool("sharpening enabled", "Shaders");
        mDitheringEnabled = Settings::Manager::getBool("dithering enabled", "Shaders");

        mSmaaEnabledUniform->set(mSmaaEnabled ? 1.f : 0.f);
        mSmaaThresholdUniform->set(std::clamp(Settings::Manager::getFloat("smaa threshold", "Shaders"), 0.03f, 0.30f));

        mBloomEnabledUniform->set(mBloomEnabled ? 1.f : 0.f);
        mBloomThresholdUniform->set(std::clamp(Settings::Manager::getFloat("bloom threshold", "Shaders"), 0.f, 2.f));
        mBloomSoftKneeUniform->set(std::clamp(Settings::Manager::getFloat("bloom soft knee", "Shaders"), 0.f, 1.f));
        const float bloomRadius = std::clamp(Settings::Manager::getFloat("bloom radius", "Shaders"), 0.5f, 8.f);
        mBloomRadiusHorizontalUniform->set(bloomRadius);
        mBloomRadiusVerticalUniform->set(bloomRadius);
        mBloomIntensityUniform->set(std::clamp(Settings::Manager::getFloat("bloom intensity", "Shaders"), 0.f, 3.f));

        mAtmosphericFogEnabledUniform->set(mAtmosphericFogEnabled ? 1.f : 0.f);
        mAtmosphericFogStrengthUniform->set(std::clamp(Settings::Manager::getFloat("atmospheric fog strength", "Shaders"), 0.f, 1.f));
        mGodRaysEnabledUniform->set(mGodRaysEnabled ? 1.f : 0.f);
        mGodRaysStrengthUniform->set(std::clamp(Settings::Manager::getFloat("god rays strength", "Shaders"), 0.f, 1.5f));
        mSharpeningEnabledUniform->set(mSharpeningEnabled ? 1.f : 0.f);
        mSharpeningStrengthUniform->set(std::clamp(Settings::Manager::getFloat("sharpening strength", "Shaders"), 0.f, 1.f));
        mDitheringEnabledUniform->set(mDitheringEnabled ? 1.f : 0.f);

        const bool masterEnabled = Settings::Manager::getBool("enabled", "Post Processing");
        const bool wasEnabled = mEnabled;
        mEnabled = masterEnabled && (mSmaaEnabled || mBloomEnabled || mAtmosphericFogEnabled || mGodRaysEnabled
            || mSharpeningEnabled || mDitheringEnabled);
        if (!mEnabled || !wasEnabled)
            mCaptureReady.store(false, std::memory_order_release);
        if (!mEnabled)
            mWidth = mHeight = 0;

        updateSourceBindings();
        applyPassVisibility();

        Log(Debug::Info) << "ArenaMP native effects: fog=" << (mAtmosphericFogEnabled ? "on" : "off")
                         << ", godRays=" << (mGodRaysEnabled ? "on" : "off")
                         << ", firstPersonWorldDepthBridge=on";
    }

    void NativeEffectsProcessor::setEnvironment(const osg::Vec4f& fogColor, float fogStart, float fogEnd,
        bool interior, bool underwater, bool firstPerson, const osg::Vec3f& sunDirection,
        const osg::Vec4f& sunColor)
    {
        mInterior = interior;
        mUnderwater = underwater;
        mSunDirection = sunDirection;
        if (mSunDirection.length2() > 0.000001f)
            mSunDirection.normalize();
        if (!mReady)
            return;

        mFogColorUniform->set(osg::Vec3f(fogColor.r(), fogColor.g(), fogColor.b()));
        mFogStartUniform->set(std::max(fogStart, 0.f));
        mFogEndUniform->set(std::max(fogEnd, fogStart + 1.f));
        mEnvironmentExteriorUniform->set(interior ? 0.f : 1.f);
        mEnvironmentUnderwaterUniform->set(underwater ? 1.f : 0.f);
        mFirstPersonViewUniform->set(firstPerson ? 1.f : 0.f);
        mSunColorUniform->set(osg::Vec3f(std::max(sunColor.r(), 0.f), std::max(sunColor.g(), 0.f), std::max(sunColor.b(), 0.f)));
    }

    void NativeEffectsProcessor::updateSourceBindings()
    {
        if (!mReady)
            return;

        mBloomHorizontalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mEdgeState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(3, mDepthTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(4, mOverlayDepthTexture, osg::StateAttribute::ON);
    }

    void NativeEffectsProcessor::applyPassVisibility()
    {
        const bool visible = mReady && mEnabled && mCaptureReady.load(std::memory_order_acquire);
        const unsigned int on = Mask_RenderToTexture;

        if (mBloomHorizontalCamera) mBloomHorizontalCamera->setNodeMask(visible && mBloomEnabled ? on : 0u);
        if (mBloomVerticalCamera) mBloomVerticalCamera->setNodeMask(visible && mBloomEnabled ? on : 0u);
        if (mEdgeCamera) mEdgeCamera->setNodeMask(visible && mSmaaEnabled ? on : 0u);
        if (mWeightCamera) mWeightCamera->setNodeMask(visible && mSmaaEnabled ? on : 0u);
        if (mFinalCamera) mFinalCamera->setNodeMask(visible ? on : 0u);
    }

    void NativeEffectsProcessor::resizeTargets(int width, int height)
    {
        mWidth = width;
        mHeight = height;
        const int bloomWidth = std::max(1, width / 2);
        const int bloomHeight = std::max(1, height / 2);

        mSceneTexture->setTextureSize(width, height);
        mDepthTexture->setTextureSize(width, height);
        mOverlayDepthTexture->setTextureSize(width, height);
        mEdgeTexture->setTextureSize(width, height);
        mWeightTexture->setTextureSize(width, height);
        mBloomHorizontalTexture->setTextureSize(bloomWidth, bloomHeight);
        mBloomVerticalTexture->setTextureSize(bloomWidth, bloomHeight);

        mBloomHorizontalCamera->setViewport(0, 0, bloomWidth, bloomHeight);
        mBloomVerticalCamera->setViewport(0, 0, bloomWidth, bloomHeight);
        mEdgeCamera->setViewport(0, 0, width, height);
        mWeightCamera->setViewport(0, 0, width, height);
        mFinalCamera->setViewport(0, 0, width, height);

        const osg::Vec2f invScene(1.f/static_cast<float>(width), 1.f/static_cast<float>(height));
        const osg::Vec2f invBloom(1.f/static_cast<float>(bloomWidth), 1.f/static_cast<float>(bloomHeight));
        mInverseSceneSizeEdge->set(invScene);
        mInverseSceneSizeWeight->set(invScene);
        mInverseSceneSizeFinal->set(invScene);
        mInverseBloomSizeHorizontal->set(invBloom);
        mInverseBloomSizeVertical->set(invBloom);
        if (osg::Uniform* u = mBloomHorizontalState->getUniform("inverseSceneSize"))
            u->set(invScene);

        mCaptureReady.store(false, std::memory_order_release);
        applyPassVisibility();
    }

    void NativeEffectsProcessor::update()
    {
        if (!mEnabled || !mMainCamera.valid() || !mMainCamera->getViewport())
        {
            applyPassVisibility();
            return;
        }

        const int width = std::max(1, static_cast<int>(mMainCamera->getViewport()->width()));
        const int height = std::max(1, static_cast<int>(mMainCamera->getViewport()->height()));
        if (width != mWidth || height != mHeight)
            resizeTargets(width, height);

        double fovy = 75.0, aspect = static_cast<double>(width) / static_cast<double>(height);
        double zNear = 1.0, zFar = 8192.0;
        mMainCamera->getProjectionMatrix().getPerspective(fovy, aspect, zNear, zFar);
        zNear = std::max(zNear, 0.001);
        zFar = std::max(zFar, zNear + 1.0);
        mCameraNearUniform->set(static_cast<float>(zNear));
        mCameraFarUniform->set(static_cast<float>(zFar));
        mCameraAspectUniform->set(static_cast<float>(std::max(aspect, 0.01)));
        constexpr double pi = 3.14159265358979323846;
        mCameraTanHalfFovYUniform->set(static_cast<float>(std::tan(fovy * pi / 360.0)));

        const osg::Matrixd inverseView = mMainCamera->getInverseViewMatrix();
        const osg::Vec3d cameraPosition = inverseView.getTrans();
        osg::Vec3d cameraRight = osg::Matrixd::transform3x3(osg::Vec3d(1.0, 0.0, 0.0), inverseView);
        osg::Vec3d cameraUp = osg::Matrixd::transform3x3(osg::Vec3d(0.0, 1.0, 0.0), inverseView);
        osg::Vec3d cameraForward = osg::Matrixd::transform3x3(osg::Vec3d(0.0, 0.0, -1.0), inverseView);
        cameraRight.normalize();
        cameraUp.normalize();
        cameraForward.normalize();
        mCameraWorldPositionUniform->set(osg::Vec3f(static_cast<float>(cameraPosition.x()), static_cast<float>(cameraPosition.y()), static_cast<float>(cameraPosition.z())));
        mCameraRightUniform->set(osg::Vec3f(static_cast<float>(cameraRight.x()), static_cast<float>(cameraRight.y()), static_cast<float>(cameraRight.z())));
        mCameraUpUniform->set(osg::Vec3f(static_cast<float>(cameraUp.x()), static_cast<float>(cameraUp.y()), static_cast<float>(cameraUp.z())));
        mCameraForwardUniform->set(osg::Vec3f(static_cast<float>(cameraForward.x()), static_cast<float>(cameraForward.y()), static_cast<float>(cameraForward.z())));

        const osg::Vec3d sunDir(mSunDirection.x(), mSunDirection.y(), mSunDirection.z());
        const double sunViewX = sunDir * cameraRight;
        const double sunViewY = sunDir * cameraUp;
        const double sunFacing = sunDir * cameraForward;

        // Project from the actual camera basis rather than multiplying a far
        // point by the camera matrices. This stays stable when OpenMW switches
        // between normal and first-person rendering/FOV overrides.
        osg::Vec2f sunUv(0.5f, 0.5f);
        if (sunFacing > 0.0001)
        {
            const double tanHalfFovY = std::max(std::tan(fovy * pi / 360.0), 0.0001);
            const double ndcX = sunViewX / (sunFacing * std::max(aspect, 0.01) * tanHalfFovY);
            const double ndcY = sunViewY / (sunFacing * tanHalfFovY);
            sunUv.set(static_cast<float>(ndcX * 0.5 + 0.5), static_cast<float>(ndcY * 0.5 + 0.5));
        }
        mSunScreenPositionUniform->set(sunUv);
        const bool sunOnUsefulSide = sunFacing > 0.02
            && sunUv.x() > -0.45f && sunUv.x() < 1.45f
            && sunUv.y() > -0.45f && sunUv.y() < 1.45f
            && !mInterior && !mUnderwater;
        mSunVisibleUniform->set(sunOnUsefulSide ? 1.f : 0.f);

        const double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        mFrameTimeUniform->set(static_cast<float>(std::fmod(now, 4096.0)));

        updateSourceBindings();
        applyPassVisibility();
    }
}
