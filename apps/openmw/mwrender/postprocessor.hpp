#ifndef OPENMW_MWRENDER_POSTPROCESSOR_H
#define OPENMW_MWRENDER_POSTPROCESSOR_H

#include <components/settings/settings.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Camera>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

namespace osg
{
    class Geode;
    class Group;
    class Program;
    class RenderInfo;
    class StateSet;
    class Texture2D;
    class Uniform;
}

namespace Fx
{
    class Technique;
    struct RenderTargetSpec;
}

namespace Resource
{
    class ResourceSystem;
}

namespace VFS
{
    class Manager;
}

namespace MWRender
{
    // OpenMW 0.52 post-processing backport for the ArenaMP/OpenMW-0.47 renderer.
    //
    // R5.1 switches from the experimental framebuffer-copy bridge to direct
    // scene-FBO ownership, matching the core OpenMW 0.52 design much more
    // closely. The world is rendered into mSceneTexture first, then the .omwfx
    // chain runs as POST_RENDER passes and the result is presented before MyGUI.
    // This makes live uniforms/slider changes deterministic instead of depending
    // on a one-frame-late framebuffer copy callback.
    class PostProcessor
    {
    public:
        PostProcessor(osg::Camera* mainCamera, osg::Group* rootNode, Resource::ResourceSystem* resourceSystem);
        ~PostProcessor();

        void update(float dt);
        void setSceneContext(bool interior, bool underwater);
        void processChangedSettings(const Settings::CategorySettingVector& settings);

        bool isEnabled() const { return mEnabled; }
        bool transparentPostpassEnabled() const { return mTransparentPostpass; }
        float autoExposureSpeed() const { return mAutoExposureSpeed; }

        const std::vector<std::string>& getAvailableEffects() const { return mAvailableEffects; }
        const std::vector<std::string>& getChain() const { return mChain; }

        // Settings UI bridge. OpenMW 0.52 exposes its shader HUD directly from
        // the post processor; ArenaMP keeps the UI inside the normal settings
        // window, so provide a lightweight read-only access point instead.
        static PostProcessor* getActiveProcessor();
        std::shared_ptr<Fx::Technique> getTechniqueInfo(const std::string& name);

        // OpenMW 0.47 clears the world depth immediately before drawing the
        // first-person body. Preserve the world depth for .omwfx depth queries.
        static void captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo);

    private:
        struct RuntimePass;
        struct RuntimeTechnique;

        void reloadSettings();
        void discoverEffects();
        void rebuildChainNames();
        void rebuildRuntimeChain();
        void clearRuntimeChain();
        bool effectExists(const std::string& name) const;
        std::shared_ptr<Fx::Technique> loadTechnique(const std::string& name);

        void captureWorldDepth(osg::RenderInfo& renderInfo);
        void updateFinalSource();
        void resizeTargets(int width, int height);
        void updateDynamicUniforms();
        void updateEffectUniforms();
        void applyPassVisibility();
        bool techniqueVisible(const RuntimeTechnique& technique) const;
        void createDistortionResources(int& order);
        void clearDistortionResources();
        void createEyeAdaptationPasses(int& order);
        void clearEyeAdaptationPasses();

        osg::ref_ptr<osg::Texture2D> createSceneTexture() const;
        static osg::ref_ptr<osg::Texture2D> createDepthTexture();
        static osg::ref_ptr<osg::Texture2D> createEyeAdaptationTexture();
        static osg::ref_ptr<osg::Texture2D> createDistortionTexture();
        static osg::ref_ptr<osg::Texture2D> createTargetTexture(const Fx::RenderTargetSpec& spec);
        static osg::ref_ptr<osg::Camera> createCamera(int orderNum, bool renderToTexture);
        static osg::ref_ptr<osg::Geode> createFullscreenPass(osg::Program* program);
        static osg::ref_ptr<osg::Program> createProgram(const std::string& name, const std::string& fragmentSource);
        static osg::ref_ptr<osg::Program> createBlitProgram();
        static std::string makeFragmentSource(const Fx::Technique& technique, const std::string& passSource,
            const std::vector<std::string>& sampledTargets);

        const VFS::Manager* mVfs = nullptr;
        osg::observer_ptr<osg::Camera> mMainCamera;
        osg::observer_ptr<osg::Group> mRootNode;

        bool mEnabled = true;
        bool mTransparentPostpass = true;
        bool mInterior = false;
        bool mUnderwater = false;
        bool mInternalDistortionAvailable = false;
        float mAutoExposureSpeed = 0.9f;
        float mDeltaTime = 0.f;
        bool mChainDirty = true;
        bool mReady = false;
        std::atomic<bool> mWorldDepthCapturedBeforeFirstPerson{false};

        int mWidth = 0;
        int mHeight = 0;
        float mSimulationTime = 0.f;
        float mNear = 1.f;
        float mFar = 8192.f;
        float mInvProjectionScaleX = 1.f;
        float mInvProjectionScaleY = 1.f;

        std::vector<std::string> mAvailableEffects;
        std::vector<std::string> mChain;
        std::vector<std::shared_ptr<Fx::Technique>> mLoadedTechniques;
        std::vector<std::unique_ptr<RuntimeTechnique>> mRuntimeTechniques;
        std::vector<std::unique_ptr<RuntimePass>> mRuntimePasses;
        std::vector<osg::ref_ptr<osg::Camera>> mPassCameras;

        osg::ref_ptr<osg::Texture2D> mSceneTexture;
        osg::ref_ptr<osg::Texture2D> mDepthTexture;
        osg::ref_ptr<osg::Texture2D> mPingTextureA;
        osg::ref_ptr<osg::Texture2D> mPingTextureB;
        osg::ref_ptr<osg::Texture2D> mPingTextureC;
        osg::ref_ptr<osg::Texture2D> mDistortionTexture;
        osg::ref_ptr<osg::Camera> mDistortionClearCamera;
        osg::ref_ptr<osg::Texture2D> mEyeAdaptTextureA;
        osg::ref_ptr<osg::Texture2D> mEyeAdaptTextureB;
        osg::ref_ptr<osg::Camera> mEyeAdaptCamera;
        osg::ref_ptr<osg::Camera> mEyeAdaptCopyCamera;
        osg::ref_ptr<osg::StateSet> mEyeAdaptState;
        osg::ref_ptr<osg::Uniform> mEyeAdaptDeltaUniform;
        osg::ref_ptr<osg::Uniform> mEyeAdaptSpeedUniform;
        osg::ref_ptr<osg::Camera> mFinalCamera;
        osg::ref_ptr<osg::StateSet> mFinalState;
        osg::ref_ptr<osg::Uniform> mFinalSampler;
        osg::ref_ptr<osg::Texture2D> mProcessedOutput;

        static std::atomic<PostProcessor*> sActiveProcessor;
    };
}

#endif
