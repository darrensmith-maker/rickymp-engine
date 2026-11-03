#ifndef OPENMW_MWRENDER_NATIVEEFFECTS_H
#define OPENMW_MWRENDER_NATIVEEFFECTS_H

#include <atomic>

#include <osg/Camera>
#include <osg/ref_ptr>
#include <osg/observer_ptr>
#include <osg/Vec3>
#include <osg/Vec4>

namespace osg
{
    class Group;
    class Geode;
    class RenderInfo;
    class Program;
    class StateSet;
    class Texture2D;
    class Uniform;
}

namespace Shader
{
    class ShaderManager;
}

namespace MWRender
{
    class NativeEffectsProcessor
    {
    public:
        NativeEffectsProcessor(osg::Camera* mainCamera, osg::Group* rootNode,
            Shader::ShaderManager& shaderManager);
        ~NativeEffectsProcessor();

        void reloadSettings();
        void setEnvironment(const osg::Vec4f& fogColor, float fogStart, float fogEnd,
            bool interior, bool underwater, bool firstPerson, const osg::Vec3f& sunDirection,
            const osg::Vec4f& sunColor);
        void update();

        // OpenMW 0.47 clears the depth buffer immediately before drawing the
        // first-person player mesh. Capture the world depth at that exact
        // point so native fog/god-rays see the same world in 1st and 3rd person.
        static void captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo);

        bool isEnabled() const { return mEnabled; }

    private:
        class FramebufferCopyCallback;

        static osg::ref_ptr<osg::Texture2D> createTexture(
            int internalFormat, unsigned int sourceFormat, unsigned int sourceType, bool linear = true);
        static osg::ref_ptr<osg::Camera> createCamera(int orderNum, bool renderToTexture);
        static osg::ref_ptr<osg::Geode> createFullscreenPass(osg::Program* program);

        void copyFramebuffer(osg::RenderInfo& renderInfo);
        void captureWorldDepth(osg::RenderInfo& renderInfo);
        void resizeTargets(int width, int height);
        void updateSourceBindings();
        void applyPassVisibility();

        osg::observer_ptr<osg::Camera> mMainCamera;
        osg::observer_ptr<osg::Group> mRootNode;
        osg::ref_ptr<osg::Camera::DrawCallback> mOriginalPostDrawCallback;
        osg::ref_ptr<FramebufferCopyCallback> mFramebufferCopyCallback;

        bool mReady = false;
        bool mEnabled = false;
        bool mSmaaEnabled = false;
        bool mBloomEnabled = false;
        bool mAtmosphericFogEnabled = false;
        bool mGodRaysEnabled = false;
        bool mSharpeningEnabled = false;
        bool mDitheringEnabled = false;
        std::atomic<bool> mCaptureReady{false};
        std::atomic<bool> mWorldDepthCapturedBeforeFirstPerson{false};
        int mWidth = 0;
        int mHeight = 0;

        osg::ref_ptr<osg::Texture2D> mSceneTexture;
        osg::ref_ptr<osg::Texture2D> mDepthTexture;
        osg::ref_ptr<osg::Texture2D> mOverlayDepthTexture;
        osg::ref_ptr<osg::Texture2D> mBloomHorizontalTexture;
        osg::ref_ptr<osg::Texture2D> mBloomVerticalTexture;
        osg::ref_ptr<osg::Texture2D> mEdgeTexture;
        osg::ref_ptr<osg::Texture2D> mWeightTexture;

        osg::ref_ptr<osg::Camera> mBloomHorizontalCamera;
        osg::ref_ptr<osg::Camera> mBloomVerticalCamera;
        osg::ref_ptr<osg::Camera> mEdgeCamera;
        osg::ref_ptr<osg::Camera> mWeightCamera;
        osg::ref_ptr<osg::Camera> mFinalCamera;

        osg::ref_ptr<osg::StateSet> mBloomHorizontalState;
        osg::ref_ptr<osg::StateSet> mEdgeState;
        osg::ref_ptr<osg::StateSet> mFinalState;

        osg::ref_ptr<osg::Uniform> mInverseSceneSizeEdge;
        osg::ref_ptr<osg::Uniform> mInverseSceneSizeWeight;
        osg::ref_ptr<osg::Uniform> mInverseSceneSizeFinal;
        osg::ref_ptr<osg::Uniform> mInverseBloomSizeHorizontal;
        osg::ref_ptr<osg::Uniform> mInverseBloomSizeVertical;
        osg::ref_ptr<osg::Uniform> mSmaaEnabledUniform;
        osg::ref_ptr<osg::Uniform> mSmaaThresholdUniform;
        osg::ref_ptr<osg::Uniform> mBloomEnabledUniform;
        osg::ref_ptr<osg::Uniform> mBloomThresholdUniform;
        osg::ref_ptr<osg::Uniform> mBloomSoftKneeUniform;
        osg::ref_ptr<osg::Uniform> mBloomRadiusHorizontalUniform;
        osg::ref_ptr<osg::Uniform> mBloomRadiusVerticalUniform;
        osg::ref_ptr<osg::Uniform> mBloomIntensityUniform;

        osg::ref_ptr<osg::Uniform> mAtmosphericFogEnabledUniform;
        osg::ref_ptr<osg::Uniform> mAtmosphericFogStrengthUniform;
        osg::ref_ptr<osg::Uniform> mGodRaysEnabledUniform;
        osg::ref_ptr<osg::Uniform> mGodRaysStrengthUniform;
        osg::ref_ptr<osg::Uniform> mSharpeningEnabledUniform;
        osg::ref_ptr<osg::Uniform> mSharpeningStrengthUniform;
        osg::ref_ptr<osg::Uniform> mDitheringEnabledUniform;
        osg::ref_ptr<osg::Uniform> mFogColorUniform;
        osg::ref_ptr<osg::Uniform> mFogStartUniform;
        osg::ref_ptr<osg::Uniform> mFogEndUniform;
        osg::ref_ptr<osg::Uniform> mCameraNearUniform;
        osg::ref_ptr<osg::Uniform> mCameraFarUniform;
        osg::ref_ptr<osg::Uniform> mCameraWorldPositionUniform;
        osg::ref_ptr<osg::Uniform> mCameraRightUniform;
        osg::ref_ptr<osg::Uniform> mCameraUpUniform;
        osg::ref_ptr<osg::Uniform> mCameraForwardUniform;
        osg::ref_ptr<osg::Uniform> mCameraTanHalfFovYUniform;
        osg::ref_ptr<osg::Uniform> mCameraAspectUniform;
        osg::ref_ptr<osg::Uniform> mEnvironmentExteriorUniform;
        osg::ref_ptr<osg::Uniform> mEnvironmentUnderwaterUniform;
        osg::ref_ptr<osg::Uniform> mSunScreenPositionUniform;
        osg::ref_ptr<osg::Uniform> mSunVisibleUniform;
        osg::ref_ptr<osg::Uniform> mSunColorUniform;
        osg::ref_ptr<osg::Uniform> mFirstPersonViewUniform;
        osg::ref_ptr<osg::Uniform> mFrameTimeUniform;

        osg::Vec3f mSunDirection{0.f, 0.f, 1.f};
        bool mInterior = false;
        bool mUnderwater = false;
        static std::atomic<NativeEffectsProcessor*> sActiveProcessor;
    };
}

#endif
