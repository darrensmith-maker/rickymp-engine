#ifndef OPENMW_MWRENDER_POSTPROCESSOR_H
#define OPENMW_MWRENDER_POSTPROCESSOR_H

#include <components/settings/settings.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Camera>
#include <osg/Matrixf>
#include <osg/Vec4f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

namespace osg
{
    class FrameBufferObject;
    class NodeCallback;
    class Geode;
    class Group;
    class Program;
    class RenderInfo;
    class StateSet;
    class Texture;
    class Texture2D;
    class Uniform;
}

namespace osgUtil
{
    class RenderBin;
    class RenderLeaf;
}

namespace Fx
{
    class Technique;
    struct RenderTargetSpec;
    struct SamplerSpec;
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

        // The "omw" state block every .omwfx pass reads, with the same fields and setters as OpenMW 0.51
        // (components/fx/stateupdater.hpp there). RenderingManager feeds the world values below; the camera,
        // resolution and time values are read here every frame.
        void setFogRange(float fogNear, float fogFar);
        void setFogColor(const osg::Vec4f& color);
        void setAmbientColor(const osg::Vec4f& color);
        void setSkyColor(const osg::Vec4f& color);
        void setSunColor(const osg::Vec4f& color, float sunVis);
        void setSunPos(const osg::Vec4f& pos, bool night);
        void setSunVec(const osg::Vec4f& vec);
        void setGameHour(float hour);
        void setWeather(int weatherId, int nextWeatherId, float transition, float windSpeed);
        void setWaterHeight(float height);
        void setWaterEnabled(bool enabled);

        bool isEnabled() const { return mEnabled; }

        // true while an active effect asks for the vanilla sun glare to be hidden (disable_sunglare flag)
        bool sunglareDisabled() const { return mEnabled && mSunglareDisabled; }

        // Point lights for effects with pass_lights (OpenMW 0.51 layout: per light three vec4, world position,
        // diffuse colour, then constant / linear / quadratic attenuation with the radius in w)
        static constexpr int sMaxPointLights = 40;
        bool wantsPointLights() const { return mEnabled && mPassLights; }
        void setPointLights(const std::vector<osg::Vec4f>& packed);
        bool transparentPostpassEnabled() const { return mTransparentPostpass; }
        float autoExposureSpeed() const { return mAutoExposureSpeed; }

        const std::vector<std::string>& getAvailableEffects() const { return mAvailableEffects; }
        const std::vector<std::string>& getChain() const { return mChain; }

        // Settings UI bridge. OpenMW 0.52 exposes its shader HUD directly from
        // the post processor; ArenaMP keeps the UI inside the normal settings
        // window, so provide a lightweight read-only access point instead.
        static PostProcessor* getActiveProcessor();
        std::shared_ptr<Fx::Technique> getTechniqueInfo(const std::string& name);

        // OpenMW 0.47 clears the depth before drawing the first-person arms. Effects read a copy instead: the world
        // depth is copied before that clear, and the arms are then drawn into the copy depth-only, as OpenMW 0.51's
        // DepthClearCallback does, so the copy is complete. Both are called from NpcAnimation's DepthClearCallback.
        static void captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo);
        static void addFirstPersonDepth(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo,
            osgUtil::RenderLeaf*& previous);

        // The framebuffer the world is drawn into (mSceneTexture and mDepthTexture), set on the main render stage
        // every frame
        osg::FrameBufferObject* sceneFbo() const { return mSceneFbo.get(); }

        // Draws the transparent render bin. For the world it also starts the effects' depth copy from the opaque
        // world and then adds the transparent objects clipped to their opaque texels (OpenMW 0.51's transparent
        // post-pass), so leaf cards do not cover the sky in the copy with their see-through corners.
        void drawTransparent(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous);

        // Fills the depth copy once per frame when nothing in first person did (third-person view); every effect
        // pass camera calls it before drawing
        void ensureWorldDepth(osg::RenderInfo& renderInfo);

        // At frames 600, 3000 and 9000: logs the centre pixel of the rendered world and of the screen after the
        // final blit, so a black picture says which half failed
        void checkOutputOnce(osg::RenderInfo& renderInfo);

    private:
        struct RuntimePass;
        struct RuntimeTechnique;

        void reloadSettings();
        void discoverEffects();
        void rebuildChainNames();
        void rebuildRuntimeChain();
        void clearRuntimeChain();
        bool effectExists(const std::string& name) const;
        std::string availableEffectName(const std::string& name) const;   // as discovered, "" if there is none
        std::shared_ptr<Fx::Technique> loadTechnique(const std::string& name);

        void captureWorldDepth(osg::RenderInfo& renderInfo);
        void drawFirstPersonDepth(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous);
        void copySceneDepth(osg::State& state);
        void createCopyFbos();
        void createTransparentDepthState();
        void updateFinalSource();
        void resizeTargets(int width, int height);
        void createStateUniforms();
        osg::Uniform* stateUniform(const std::string& field) const;
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
        // name: how the pass is called if it raises an OpenGL error (logged once)
        static osg::ref_ptr<osg::Geode> createFullscreenPass(osg::Program* program, const std::string& name);
        static osg::ref_ptr<osg::Program> createProgram(const std::string& name, const std::string& fragmentSource);
        static osg::ref_ptr<osg::Program> createProgram(
            const std::string& name, const std::string& vertexSource, const std::string& fragmentSource);
        static osg::ref_ptr<osg::Program> createBlitProgram();
        osg::ref_ptr<osg::Texture> createSamplerTexture(const Fx::SamplerSpec& spec) const;
        // One stage of an effect pass with OpenMW 0.51's header; vertexStage selects the vertex shader
        std::string makeShaderSource(const Fx::Technique& technique, bool vertexStage, const std::string& passSource,
            const std::vector<std::string>& sampledTargets) const;

        const VFS::Manager* mVfs = nullptr;
        Resource::ResourceSystem* mResourceSystem = nullptr;
        // GLSL the graphics driver offers (e.g. 460), read once at start; 330+ lets effects use GLSL 3.30 features
        int mDriverGLSLVersion = 120;
        bool mSunglareDisabled = false;
        bool mPassLights = false;
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
        int mClearTargetFrames = 0;   // updates left in which effect targets are still cleared (see update)
        bool mReady = false;
        std::atomic<unsigned int> mWorldDepthFrame{~0u};   // frame whose depth is in mWorldDepthTexture
        std::atomic<unsigned int> mOutputChecks{0};   // how many of checkOutputOnce's samples are logged

        int mWidth = 0;
        int mHeight = 0;
        float mSimulationTime = 0.f;
        float mNear = 1.f;
        float mFar = 8192.f;
        float mFov = 60.f;
        int mFrameNumber = 0;
        osg::Matrixf mViewMatrix;
        osg::Matrixf mPrevViewMatrix;

        // "omw.<field>" uniforms, created once and shared by every pass, so a value is set in one place
        std::unordered_map<std::string, osg::ref_ptr<osg::Uniform>> mStateUniforms;

        std::vector<std::string> mAvailableEffects;
        std::vector<std::string> mChain;
        std::vector<std::shared_ptr<Fx::Technique>> mLoadedTechniques;
        std::vector<std::unique_ptr<RuntimeTechnique>> mRuntimeTechniques;
        std::vector<std::unique_ptr<RuntimePass>> mRuntimePasses;
        std::vector<osg::ref_ptr<osg::Camera>> mPassCameras;

        osg::ref_ptr<osg::Texture2D> mSceneTexture;
        osg::ref_ptr<osg::Texture2D> mDepthTexture;
        osg::ref_ptr<osg::Texture2D> mWorldDepthTexture;   // what effects sample as depth (see captureWorldDepth)
        osg::ref_ptr<osg::FrameBufferObject> mSceneFbo;
        osg::ref_ptr<osg::NodeCallback> mSceneFboCallback;
        osg::ref_ptr<osg::FrameBufferObject> mSceneDepthFbo;
        osg::ref_ptr<osg::FrameBufferObject> mWorldDepthFbo;
        osg::ref_ptr<osg::FrameBufferObject> mSceneColorFbo;
        osg::ref_ptr<osg::StateSet> mDepthOnlyState;
        osg::ref_ptr<osg::StateSet> mTransparentDepthState;
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
