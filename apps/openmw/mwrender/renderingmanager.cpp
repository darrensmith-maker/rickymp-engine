#include "renderingmanager.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <cstdlib>
#include <cmath>

#include <osg/Light>
#include <osg/Math>
#include <osg/LightModel>
#include <osg/Fog>
#include <osg/Material>
#include <osg/PolygonMode>
#include <osg/Group>
#include <osg/UserDataContainer>
#include <osg/ComputeBoundsVisitor>
#include <osg/Timer>
#include <osg/Texture2D>
#include <osg/Geometry>
#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/Program>
#include <osg/Viewport>
#include <osg/Uniform>
#include <osg/observer_ptr>

#include <osgUtil/LineSegmentIntersector>

#include <osgViewer/Viewer>

#include <components/nifosg/nifloader.hpp>

#include <components/debug/debuglog.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/keyframemanager.hpp>

#include <components/shader/removedalphafunc.hpp>
#include <components/shader/shadermanager.hpp>

#include <components/settings/settings.hpp>
#include <components/misc/stringops.hpp>

#include <components/sceneutil/util.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/writescene.hpp>
#include <components/sceneutil/shadow.hpp>

#include <components/terrain/terraingrid.hpp>
#include <components/terrain/quadtreeworld.hpp>

#include <components/esm/loadcell.hpp>

#include <components/detournavigator/navigator.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/timestamp.hpp"
#include "../mwworld/class.hpp"
#include "../mwgui/loadingscreen.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "sky.hpp"
#include "effectmanager.hpp"
#include "npcanimation.hpp"
#include "vismask.hpp"
#include "pathgrid.hpp"
#include "camera.hpp"
#include "viewovershoulder.hpp"
#include "water.hpp"
#include "terrainstorage.hpp"
#include "navmesh.hpp"
#include "actorspaths.hpp"
#include "recastmesh.hpp"
#include "fogmanager.hpp"
#include "objectpaging.hpp"
#include "screenshotmanager.hpp"
#include "postprocessor.hpp"
#include "groundcover.hpp"
#include "occlusionculling.hpp"
#include <components/sceneutil/occlusionculling.hpp>
#include <components/terrain/terrainoccluder.hpp>

namespace
{
    constexpr float sLandOptimizationUpdateInterval = 0.5f;
    constexpr float sLandOptimizationRecoveryMarginFps = 3.f;
    constexpr float sLandOptimizationMinimumStep = 256.f;
    constexpr float sLandOptimizationMaximumStep = 2048.f;
    constexpr float sLandOptimizationRecoveryStep = 512.f;
    // The optimizer still reacts at the same rate, but its target distance is approached
    // continuously instead of being applied as a visible 256-2048 unit jump every 0.5 s.
    constexpr float sLandOptimizationDistanceResponse = 4.f;
    constexpr float sLandOptimizationSmoothingMaxFrameTime = 0.1f;
    constexpr float sLandOptimizationMinimumScale = 0.5f;
    constexpr float sLandOptimizationShadowDistanceRatio = 0.25f;
    constexpr float sLandOptimizationGroundcoverDistanceRatio = 0.40f;

    int getMaterialQualityLevel()
    {
        std::string mode = Settings::Manager::getString("material quality", "Shaders");
        Misc::StringUtils::lowerCaseInPlace(mode);
        if (mode == "none")
            return 0;
        if (mode == "simple")
            return 1;
        if (mode == "quality")
            return 3;
        if (mode == "ultra")
            return 3; // Enhanced ArenaMP PBR level 4 is retired during the 0.52 migration.
        return 2;
    }

    bool materialUsesNormalMaps(int quality)
    {
        return quality >= 1;
    }

    bool materialUsesSpecularMaps(int quality)
    {
        return quality >= 2;
    }
}

namespace MWRender
{

    class StateUpdater : public SceneUtil::StateSetUpdater
    {
    public:
        StateUpdater()
            : mFogStart(0.f)
            , mFogEnd(0.f)
            , mWireframe(false)
            , mIsInterior(false)
            , mUnderwaterBlend(0.f)
        {
        }

        void setDefaults(osg::StateSet *stateset) override
        {
            osg::LightModel* lightModel = new osg::LightModel;
            stateset->setAttribute(lightModel, osg::StateAttribute::ON);
            osg::Fog* fog = new osg::Fog;
            fog->setMode(osg::Fog::LINEAR);
            stateset->setAttributeAndModes(fog, osg::StateAttribute::ON);
            if (mWireframe)
            {
                osg::PolygonMode* polygonmode = new osg::PolygonMode;
                polygonmode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
                stateset->setAttributeAndModes(polygonmode, osg::StateAttribute::ON);
            }
            else
                stateset->removeAttribute(osg::StateAttribute::POLYGONMODE);

            stateset->addUniform(new osg::Uniform("isInterior", false));
            stateset->addUniform(new osg::Uniform("isInventoryPreview", false));
            stateset->addUniform(new osg::Uniform("waterCausticsIntensity", 1.0f));
            stateset->addUniform(new osg::Uniform("waterUnderwaterTint", 1.0f));
            stateset->addUniform(new osg::Uniform("waterUnderwaterBlend", 0.0f));
            stateset->addUniform(new osg::Uniform("waterWaveStrength", 1.0f));
            stateset->addUniform(new osg::Uniform("waterSurfaceRoughness", 0.22f));

            // Runtime Enhanced PBR controls. These are inherited by object,
            // terrain and groundcover shaders so tweaking them does not require
            // shader recreation or disturbing the HDR/Bloom pipeline.
            stateset->addUniform(new osg::Uniform("pbrEnhancedLighting", 1.0f));
            stateset->addUniform(new osg::Uniform("pbrDiffuseResponse", 1.0f));
            stateset->addUniform(new osg::Uniform("pbrObjectRoughness", 0.84f));
            stateset->addUniform(new osg::Uniform("pbrTerrainRoughness", 0.92f));
            stateset->addUniform(new osg::Uniform("pbrSpecularStrength", 1.0f));
            stateset->addUniform(new osg::Uniform("pbrAmbientStrength", 0.75f));
            stateset->addUniform(new osg::Uniform("pbrSubsurfaceStrength", 0.35f));

            // Shadow filtering is also runtime-only. Keep map generation and
            // cascades untouched; only the sampling kernel changes.
            stateset->addUniform(new osg::Uniform("arenaEnhancedShadowFiltering", 1.0f));
            stateset->addUniform(new osg::Uniform("arenaShadowSoftness", 1.0f));
            stateset->addUniform(new osg::Uniform("arenaShadowAdaptiveSoftness", 1.0f));
            stateset->addUniform(new osg::Uniform("arenaShadowTexelSize", 1.0f / 512.0f));
        }

        void apply(osg::StateSet* stateset, osg::NodeVisitor*) override
        {
            osg::LightModel* lightModel = static_cast<osg::LightModel*>(stateset->getAttribute(osg::StateAttribute::LIGHTMODEL));
            lightModel->setAmbientIntensity(mAmbientColor);
            osg::Fog* fog = static_cast<osg::Fog*>(stateset->getAttribute(osg::StateAttribute::FOG));
            fog->setColor(mFogColor);
            fog->setStart(mFogStart);
            fog->setEnd(mFogEnd);
            if (osg::Uniform* uniform = stateset->getUniform("isInterior"))
                uniform->set(mIsInterior);
            if (osg::Uniform* uniform = stateset->getUniform("waterCausticsIntensity"))
                uniform->set(std::clamp(Settings::Manager::getFloat("caustics intensity", "Water"), 0.0f, 3.0f));
            if (osg::Uniform* uniform = stateset->getUniform("waterUnderwaterTint"))
                uniform->set(std::clamp(Settings::Manager::getFloat("underwater tint", "Water"), 0.0f, 2.0f));
            if (osg::Uniform* uniform = stateset->getUniform("waterUnderwaterBlend"))
                uniform->set(mUnderwaterBlend);
            if (osg::Uniform* uniform = stateset->getUniform("waterWaveStrength"))
                uniform->set(std::clamp(Settings::Manager::getFloat("wave strength", "Water"), 0.0f, 1.0f));
            if (osg::Uniform* uniform = stateset->getUniform("waterSurfaceRoughness"))
                uniform->set(std::clamp(Settings::Manager::getFloat("surface roughness", "Water"), 0.02f, 1.0f));

            if (osg::Uniform* uniform = stateset->getUniform("pbrEnhancedLighting"))
                uniform->set(0.0f); // legacy ArenaMP PBR lighting retired during renderer 0.52 migration
            if (osg::Uniform* uniform = stateset->getUniform("pbrDiffuseResponse"))
                uniform->set(std::clamp(1.0f, 0.0f, 1.0f));
            if (osg::Uniform* uniform = stateset->getUniform("pbrObjectRoughness"))
                uniform->set(std::clamp(0.84f, 0.08f, 1.0f));
            if (osg::Uniform* uniform = stateset->getUniform("pbrTerrainRoughness"))
                uniform->set(std::clamp(0.92f, 0.08f, 1.0f));
            if (osg::Uniform* uniform = stateset->getUniform("pbrSpecularStrength"))
                uniform->set(std::clamp(1.0f, 0.0f, 2.5f));
            if (osg::Uniform* uniform = stateset->getUniform("pbrAmbientStrength"))
                uniform->set(std::clamp(0.75f, 0.0f, 1.5f));
            if (osg::Uniform* uniform = stateset->getUniform("pbrSubsurfaceStrength"))
                uniform->set(std::clamp(0.35f, 0.0f, 1.5f));

            if (osg::Uniform* uniform = stateset->getUniform("arenaEnhancedShadowFiltering"))
                uniform->set(Settings::Manager::getBool("enhanced filtering", "Shadows") ? 1.0f : 0.0f);
            if (osg::Uniform* uniform = stateset->getUniform("arenaShadowSoftness"))
                uniform->set(std::clamp(Settings::Manager::getFloat("filter softness", "Shadows"), 0.25f, 3.0f));
            if (osg::Uniform* uniform = stateset->getUniform("arenaShadowAdaptiveSoftness"))
                uniform->set(Settings::Manager::getBool("adaptive softness", "Shadows") ? 1.0f : 0.0f);
            if (osg::Uniform* uniform = stateset->getUniform("arenaShadowTexelSize"))
            {
                const int shadowMapResolution = std::max(1, Settings::Manager::getInt("shadow map resolution", "Shadows"));
                uniform->set(1.0f / static_cast<float>(shadowMapResolution));
            }
        }

        void setAmbientColor(const osg::Vec4f& col)
        {
            mAmbientColor = col;
        }

        void setFogColor(const osg::Vec4f& col)
        {
            mFogColor = col;
        }

        void setFogStart(float start)
        {
            mFogStart = start;
        }

        void setFogEnd(float end)
        {
            mFogEnd = end;
        }

        void setInterior(bool interior)
        {
            mIsInterior = interior;
        }

        bool isInterior() const
        {
            return mIsInterior;
        }

        void setUnderwaterBlend(float blend)
        {
            mUnderwaterBlend = std::clamp(blend, 0.f, 1.f);
        }

        void setWireframe(bool wireframe)
        {
            if (mWireframe != wireframe)
            {
                mWireframe = wireframe;
                reset();
            }
        }

        bool getWireframe() const
        {
            return mWireframe;
        }

    private:
        osg::Vec4f mAmbientColor;
        osg::Vec4f mFogColor;
        float mFogStart;
        float mFogEnd;
        bool mWireframe;
        bool mIsInterior;
        float mUnderwaterBlend;
    };

    class PreloadCommonAssetsWorkItem : public SceneUtil::WorkItem
    {
    public:
        PreloadCommonAssetsWorkItem(Resource::ResourceSystem* resourceSystem)
            : mResourceSystem(resourceSystem)
        {
        }

        void doWork() override
        {
            try
            {
                for (std::vector<std::string>::const_iterator it = mModels.begin(); it != mModels.end(); ++it)
                    mResourceSystem->getSceneManager()->cacheInstance(*it);
                for (std::vector<std::string>::const_iterator it = mTextures.begin(); it != mTextures.end(); ++it)
                    mResourceSystem->getImageManager()->getImage(*it);
                for (std::vector<std::string>::const_iterator it = mKeyframes.begin(); it != mKeyframes.end(); ++it)
                    mResourceSystem->getKeyframeManager()->get(*it);
            }
            catch (std::exception&)
            {
                // ignore error (will be shown when these are needed proper)
            }
        }

        std::vector<std::string> mModels;
        std::vector<std::string> mTextures;
        std::vector<std::string> mKeyframes;

    private:
        Resource::ResourceSystem* mResourceSystem;
    };

    RenderingManager::RenderingManager(osgViewer::Viewer* viewer, osg::ref_ptr<osg::Group> rootNode,
                                       Resource::ResourceSystem* resourceSystem, SceneUtil::WorkQueue* workQueue,
                                       const std::string& resourcePath, DetourNavigator::Navigator& navigator)
        : mViewer(viewer)
        , mRootNode(rootNode)
        , mResourceSystem(resourceSystem)
        , mWorkQueue(workQueue)
        , mUnrefQueue(new SceneUtil::UnrefQueue)
        , mNavigator(navigator)
        , mMinimumAmbientLuminance(0.f)
        , mNightEyeFactor(0.f)
        , mNearClip(0.f)
        , mViewDistance(0.f)
        , mConfiguredViewDistance(0.f)
        , mLandOptimizationDistance(0.f)
        , mLandOptimizationTargetFps(0.f)
        , mLandOptimizationMinDistance(8192.f)
        , mLandOptimizationTimer(0.f)
        , mLandOptimizationFrameTime(0.f)
        , mLandOptimizationFrameCount(0)
        , mLandOptimizationEnabled(false)
        , mLandOptimizationWasExterior(false)
        , mUnderwaterFogActive(false)
        , mFieldOfViewOverridden(false)
        , mFieldOfViewOverride(0.f)
    {
        auto lightingMethod = SceneUtil::LightManager::getLightingMethodFromString(Settings::Manager::getString("lighting method", "Shaders"));

        resourceSystem->getSceneManager()->setParticleSystemMask(MWRender::Mask_ParticleSystem);
        resourceSystem->getSceneManager()->setShaderPath(resourcePath + "/shaders");
        // Shadows and radial fog have problems with fixed-function mode
        bool forceShaders = Settings::Manager::getBool("radial fog", "Shaders")
                            || Settings::Manager::getBool("force shaders", "Shaders")
                            || Settings::Manager::getBool("enable shadows", "Shadows")
                            || lightingMethod != SceneUtil::LightingMethod::FFP;
        resourceSystem->getSceneManager()->setForceShaders(forceShaders);
        // FIXME: calling dummy method because terrain needs to know whether lighting is clamped
        resourceSystem->getSceneManager()->setClampLighting(Settings::Manager::getBool("clamp lighting", "Shaders"));
        const int materialQuality = getMaterialQualityLevel();
        resourceSystem->getSceneManager()->setAutoUseNormalMaps(materialUsesNormalMaps(materialQuality));
        resourceSystem->getSceneManager()->setNormalMapPattern(Settings::Manager::getString("normal map pattern", "Shaders"));
        resourceSystem->getSceneManager()->setNormalHeightMapPattern(Settings::Manager::getString("normal height map pattern", "Shaders"));
        resourceSystem->getSceneManager()->setAutoUseSpecularMaps(materialUsesSpecularMaps(materialQuality));
        resourceSystem->getSceneManager()->setSpecularMapPattern(Settings::Manager::getString("specular map pattern", "Shaders"));
        resourceSystem->getSceneManager()->setApplyLightingToEnvMaps(Settings::Manager::getBool("apply lighting to environment maps", "Shaders"));
        resourceSystem->getSceneManager()->setConvertAlphaTestToAlphaToCoverage(Settings::Manager::getBool("antialias alpha test", "Shaders") && Settings::Manager::getInt("antialiasing", "Video") > 1);

        // Let LightManager choose which backend to use based on our hint. For methods besides legacy lighting, this depends on support for various OpenGL extensions.
        osg::ref_ptr<SceneUtil::LightManager> sceneRoot = new SceneUtil::LightManager(lightingMethod == SceneUtil::LightingMethod::FFP);
        resourceSystem->getSceneManager()->getShaderManager().setLightingMethod(sceneRoot->getLightingMethod());
        resourceSystem->getSceneManager()->setLightingMethod(sceneRoot->getLightingMethod());
        resourceSystem->getSceneManager()->setSupportedLightingMethods(sceneRoot->getSupportedLightingMethods());
        mMinimumAmbientLuminance = std::clamp(Settings::Manager::getFloat("minimum interior brightness", "Shaders"), 0.f, 1.f);

        sceneRoot->setLightingMask(Mask_Lighting);
        mSceneRoot = sceneRoot;
        sceneRoot->setStartLight(1);
        sceneRoot->setNodeMask(Mask_Scene);
        sceneRoot->setName("Scene Root");

        int shadowCastingTraversalMask = Mask_Scene;
        if (Settings::Manager::getBool("actor shadows", "Shadows"))
            shadowCastingTraversalMask |= Mask_Actor;
        if (Settings::Manager::getBool("player shadows", "Shadows"))
            shadowCastingTraversalMask |= Mask_Player;
        if (Settings::Manager::getBool("terrain shadows", "Shadows"))
            shadowCastingTraversalMask |= Mask_Terrain;

        int indoorShadowCastingTraversalMask = shadowCastingTraversalMask;
        if (Settings::Manager::getBool("object shadows", "Shadows"))
            shadowCastingTraversalMask |= (Mask_Object|Mask_Static);

        mShadowManager.reset(new SceneUtil::ShadowManager(sceneRoot, mRootNode, shadowCastingTraversalMask, indoorShadowCastingTraversalMask, mResourceSystem->getSceneManager()->getShaderManager()));

        Shader::ShaderManager::DefineMap shadowDefines = mShadowManager->getShadowDefines();
        Shader::ShaderManager::DefineMap lightDefines = sceneRoot->getLightDefines();
        Shader::ShaderManager::DefineMap globalDefines = mResourceSystem->getSceneManager()->getShaderManager().getGlobalDefines();

        for (auto itr = shadowDefines.begin(); itr != shadowDefines.end(); itr++)
            globalDefines[itr->first] = itr->second;

        globalDefines["forcePPL"] = Settings::Manager::getBool("force per pixel lighting", "Shaders") ? "1" : "0";
        globalDefines["clamp"] = Settings::Manager::getBool("clamp lighting", "Shaders") ? "1" : "0";
        globalDefines["preLightEnv"] = Settings::Manager::getBool("apply lighting to environment maps", "Shaders") ? "1" : "0";
        globalDefines["radialFog"] = Settings::Manager::getBool("radial fog", "Shaders") ? "1" : "0";
        globalDefines["materialQuality"] = std::to_string(materialQuality);
        globalDefines["useGPUShader4"] = "0";

        for (auto itr = lightDefines.begin(); itr != lightDefines.end(); itr++)
            globalDefines[itr->first] = itr->second;

        // Refactor this at some point - most shaders don't care about these defines
        float groundcoverDistance = std::max(0.f, Settings::Manager::getFloat("rendering distance", "Groundcover"));
        globalDefines["groundcoverFadeStart"] = std::to_string(groundcoverDistance * 0.9f);
        globalDefines["groundcoverFadeEnd"] = std::to_string(groundcoverDistance);
        globalDefines["groundcoverStompMode"] = std::to_string(std::clamp(Settings::Manager::getInt("stomp mode", "Groundcover"), 0, 2));
        globalDefines["groundcoverStompIntensity"] = std::to_string(std::clamp(Settings::Manager::getInt("stomp intensity", "Groundcover"), 0, 2));

        // It is unnecessary to stop/start the viewer as no frames are being rendered yet.
        mResourceSystem->getSceneManager()->getShaderManager().setGlobalDefines(globalDefines);

        mNavMesh.reset(new NavMesh(mRootNode, Settings::Manager::getBool("enable nav mesh render", "Navigator")));
        mActorsPaths.reset(new ActorsPaths(mRootNode, Settings::Manager::getBool("enable agents paths render", "Navigator")));
        mRecastMesh.reset(new RecastMesh(mRootNode, Settings::Manager::getBool("enable recast mesh render", "Navigator")));
        mPathgrid.reset(new Pathgrid(mRootNode));

        if (Settings::Manager::getBool("occlusion culling", "Camera"))
            mOcclusionCuller = new SceneUtil::OcclusionCuller(Settings::Manager::getInt("occlusion buffer width", "Camera"), Settings::Manager::getInt("occlusion buffer height", "Camera"));

        mObjects.reset(new Objects(mResourceSystem, sceneRoot, mUnrefQueue.get(), mOcclusionCuller.get()));

        if (getenv("OPENMW_DONT_PRECOMPILE") == nullptr)
        {
            mViewer->setIncrementalCompileOperation(new osgUtil::IncrementalCompileOperation);
            mViewer->getIncrementalCompileOperation()->setTargetFrameRate(Settings::Manager::getFloat("target framerate", "Cells"));
        }

        mResourceSystem->getSceneManager()->setIncrementalCompileOperation(mViewer->getIncrementalCompileOperation());

        mEffectManager.reset(new EffectManager(sceneRoot, mResourceSystem));

        const std::string normalMapPattern = Settings::Manager::getString("normal map pattern", "Shaders");
        const std::string heightMapPattern = Settings::Manager::getString("normal height map pattern", "Shaders");
        const std::string specularMapPattern = Settings::Manager::getString("terrain specular map pattern", "Shaders");
        const bool useTerrainNormalMaps = materialUsesNormalMaps(materialQuality);
        const bool useTerrainSpecularMaps = materialUsesSpecularMaps(materialQuality);

        mTerrainStorage.reset(new TerrainStorage(mResourceSystem, normalMapPattern, heightMapPattern, useTerrainNormalMaps, specularMapPattern, useTerrainSpecularMaps));
        const float lodFactor = Settings::Manager::getFloat("lod factor", "Terrain");

        // Distant land is always backed by the quadtree terrain renderer. Runtime
        // presets change its LOD, compositing and paging thresholds without restart.
        const int compMapResolution = Settings::Manager::getInt("composite map resolution", "Terrain");
        int compMapPower = Settings::Manager::getInt("composite map level", "Terrain");
        compMapPower = std::max(-3, compMapPower);
        const float compMapLevel = std::pow(2.f, static_cast<float>(compMapPower));
        const int vertexLodMod = Settings::Manager::getInt("vertex lod mod", "Terrain");
        const float maxCompGeometrySize = std::max(1.f, Settings::Manager::getFloat("max composite geometry size", "Terrain"));

        mTerrain.reset(new Terrain::QuadTreeWorld(
            sceneRoot, mRootNode, mResourceSystem, mTerrainStorage.get(), Mask_Terrain, Mask_PreCompile, Mask_Debug,
            compMapResolution, compMapLevel, lodFactor, vertexLodMod, maxCompGeometrySize));

        // Object paging is part of the mandatory distant-land backend. Detail and
        // merge thresholds are configurable, but the paging system itself stays on.
        mObjectPaging.reset(new ObjectPaging(mResourceSystem->getSceneManager(), mOcclusionCuller.get()));
        static_cast<Terrain::QuadTreeWorld*>(mTerrain.get())->addChunkManager(mObjectPaging.get());
        mResourceSystem->addResourceManager(mObjectPaging.get());

        mTerrain->setTargetFrameRate(Settings::Manager::getFloat("target framerate", "Cells"));
        mTerrain->setWorkQueue(mWorkQueue.get());

        if (mOcclusionCuller.valid())
        {
            mTerrainOccluder.reset(new Terrain::TerrainOccluder(mTerrainStorage.get(), ESM::Land::REAL_SIZE));
            mTerrainOccluder->setLodLevel(std::max(0, Settings::Manager::getInt("occlusion terrain lod", "Camera")));
            sceneRoot->addCullCallback(new SceneOcclusionCallback(
                mOcclusionCuller.get(),
                mTerrainOccluder.get(),
                std::max(1, Settings::Manager::getInt("occlusion terrain radius", "Camera")),
                Settings::Manager::getBool("occlusion culling terrain", "Camera")));
        }

        // Build the groundcover world once and switch its node mask at runtime.
        // This allows enabling/disabling grass and changing density without a restart.
        mGroundcoverRoot = new osg::Group;
        mGroundcoverRoot->setNodeMask(Settings::Manager::getBool("enabled", "Groundcover") ? Mask_Groundcover : 0u);
        mGroundcoverRoot->setName("Groundcover Root");
        sceneRoot->addChild(mGroundcoverRoot);

        mGroundcoverUpdater = new GroundcoverUpdater;
        mGroundcoverRoot->addUpdateCallback(mGroundcoverUpdater);

        float chunkSize = Settings::Manager::getFloat("min chunk size", "Groundcover");
        if (chunkSize >= 1.0f)
            chunkSize = 1.0f;
        else if (chunkSize >= 0.5f)
            chunkSize = 0.5f;
        else if (chunkSize >= 0.25f)
            chunkSize = 0.25f;
        else if (chunkSize != 0.125f)
            chunkSize = 0.125f;

        float density = std::clamp(Settings::Manager::getFloat("density", "Groundcover"), 0.f, 1.f);
        mGroundcoverWorld.reset(new Terrain::QuadTreeWorld(mGroundcoverRoot, mTerrainStorage.get(), Mask_Groundcover, lodFactor, chunkSize));
        mGroundcover.reset(new Groundcover(mResourceSystem->getSceneManager(), density));
        static_cast<Terrain::QuadTreeWorld*>(mGroundcoverWorld.get())->addChunkManager(mGroundcover.get());
        mResourceSystem->addResourceManager(mGroundcover.get());

        // Groundcover is handled identically for active and distant cells.
        mGroundcoverWorld->setActiveGrid(osg::Vec4i(0, 0, 0, 0));
        // water goes after terrain for correct waterculling order
        mWater.reset(new Water(sceneRoot->getParent(0), sceneRoot, mResourceSystem, mViewer->getIncrementalCompileOperation(), resourcePath));

        mCamera.reset(new Camera(mViewer->getCamera()));
        if (Settings::Manager::getBool("view over shoulder", "Camera"))
            mViewOverShoulderController.reset(new ViewOverShoulderController(mCamera.get()));

        mScreenshotManager.reset(new ScreenshotManager(viewer, mRootNode, sceneRoot, mResourceSystem, mWater.get()));

        mViewer->setLightingMode(osgViewer::View::NO_LIGHT);

        osg::ref_ptr<osg::LightSource> source = new osg::LightSource;
        source->setNodeMask(Mask_Lighting);
        mSunLight = new osg::Light;
        source->setLight(mSunLight);
        mSunLight->setDiffuse(osg::Vec4f(0,0,0,1));
        mSunLight->setAmbient(osg::Vec4f(0,0,0,1));
        mSunLight->setSpecular(osg::Vec4f(0,0,0,0));
        mSunLight->setConstantAttenuation(1.f);
        sceneRoot->setSunlight(mSunLight);
        sceneRoot->addChild(source);

        sceneRoot->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::ON);
        sceneRoot->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        sceneRoot->getOrCreateStateSet()->setMode(GL_NORMALIZE, osg::StateAttribute::ON);
        osg::ref_ptr<osg::Material> defaultMat (new osg::Material);
        defaultMat->setColorMode(osg::Material::OFF);
        defaultMat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4f(1,1,1,1));
        defaultMat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4f(1,1,1,1));
        defaultMat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        sceneRoot->getOrCreateStateSet()->setAttribute(defaultMat);

        mFog.reset(new FogManager());

        mSky.reset(new SkyManager(sceneRoot, resourceSystem->getSceneManager()));
        mSky->setCamera(mViewer->getCamera());

        source->setStateSetModes(*mRootNode->getOrCreateStateSet(), osg::StateAttribute::ON);

        mStateUpdater = new StateUpdater;
        sceneRoot->addUpdateCallback(mStateUpdater);

        osg::Camera::CullingMode cullingMode = osg::Camera::DEFAULT_CULLING|osg::Camera::FAR_PLANE_CULLING;

        if (!Settings::Manager::getBool("small feature culling", "Camera"))
            cullingMode &= ~(osg::CullStack::SMALL_FEATURE_CULLING);
        else
        {
            mViewer->getCamera()->setSmallFeatureCullingPixelSize(Settings::Manager::getFloat("small feature culling pixel size", "Camera"));
            cullingMode |= osg::CullStack::SMALL_FEATURE_CULLING;
        }

        mViewer->getCamera()->setCullingMode( cullingMode );

        mViewer->getCamera()->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);
        mViewer->getCamera()->setCullingMode(cullingMode);

        mViewer->getCamera()->setCullMask(~(Mask_UpdateVisitor|Mask_SimpleWater));
        NifOsg::Loader::setHiddenNodeMask(Mask_UpdateVisitor);
        NifOsg::Loader::setIntersectionDisabledNodeMask(Mask_Effect);
        Nif::NIFFile::setLoadUnsupportedFiles(Settings::Manager::getBool("load unsupported nif files", "Models"));

        mNearClip = Settings::Manager::getFloat("near clip", "Camera");
        mConfiguredViewDistance = Settings::Manager::getFloat("viewing distance", "Camera");
        mViewDistance = mConfiguredViewDistance;
        mLandOptimizationDistance = mConfiguredViewDistance;
        updateLandOptimizationProfile();
        float fov = Settings::Manager::getFloat("field of view", "Camera");
        mFieldOfView = std::min(std::max(1.f, fov), 179.f);
        float firstPersonFov = Settings::Manager::getFloat("first person field of view", "Camera");
        mFirstPersonFieldOfView = std::min(std::max(1.f, firstPersonFov), 179.f);
        mStateUpdater->setFogEnd(mViewDistance);

        mRootNode->getOrCreateStateSet()->addUniform(new osg::Uniform("near", mNearClip));
        mRootNode->getOrCreateStateSet()->addUniform(new osg::Uniform("far", mViewDistance));
        mRootNode->getOrCreateStateSet()->addUniform(new osg::Uniform("simpleWater", false));

        // Legacy ArenaMP HDR uniforms were removed. HDR render targets and tonemapping
        // are now reserved for the OpenMW 0.52 PostProcessor migration path.

        // Hopefully, anything genuinely requiring the default alpha func of GL_ALWAYS explicitly sets it
        mRootNode->getOrCreateStateSet()->setAttribute(Shader::RemovedAlphaFunc::getInstance(GL_ALWAYS));
        // The transparent renderbin sets alpha testing on because that was faster on old GPUs. It's now slower and breaks things.
        mRootNode->getOrCreateStateSet()->setMode(GL_ALPHA_TEST, osg::StateAttribute::OFF);

        mUniformNear = mRootNode->getOrCreateStateSet()->getUniform("near");
        mUniformFar = mRootNode->getOrCreateStateSet()->getUniform("far");
        updateProjectionMatrix();

        mPostProcessor.reset(new PostProcessor(mViewer->getCamera(), mRootNode, mResourceSystem));
    }

    RenderingManager::~RenderingManager()
    {
        // let background loading thread finish before we delete anything else
        mWorkQueue = nullptr;
    }

    osgUtil::IncrementalCompileOperation* RenderingManager::getIncrementalCompileOperation()
    {
        return mViewer->getIncrementalCompileOperation();
    }

    MWRender::Objects& RenderingManager::getObjects()
    {
        return *mObjects.get();
    }

    Resource::ResourceSystem* RenderingManager::getResourceSystem()
    {
        return mResourceSystem;
    }

    SceneUtil::WorkQueue* RenderingManager::getWorkQueue()
    {
        return mWorkQueue.get();
    }

    SceneUtil::UnrefQueue* RenderingManager::getUnrefQueue()
    {
        return mUnrefQueue.get();
    }

    Terrain::World* RenderingManager::getTerrain()
    {
        return mTerrain.get();
    }

    void RenderingManager::preloadCommonAssets()
    {
        osg::ref_ptr<PreloadCommonAssetsWorkItem> workItem (new PreloadCommonAssetsWorkItem(mResourceSystem));
        mSky->listAssetsToPreload(workItem->mModels, workItem->mTextures);
        mWater->listAssetsToPreload(workItem->mTextures);

        workItem->mModels.push_back(Settings::Manager::getString("xbaseanim", "Models"));
        workItem->mModels.push_back(Settings::Manager::getString("xbaseanim1st", "Models"));
        workItem->mModels.push_back(Settings::Manager::getString("xbaseanimfemale", "Models"));
        workItem->mModels.push_back(Settings::Manager::getString("xargonianswimkna", "Models"));

        workItem->mKeyframes.push_back(Settings::Manager::getString("xbaseanimkf", "Models"));
        workItem->mKeyframes.push_back(Settings::Manager::getString("xbaseanim1stkf", "Models"));
        workItem->mKeyframes.push_back(Settings::Manager::getString("xbaseanimfemalekf", "Models"));
        workItem->mKeyframes.push_back(Settings::Manager::getString("xargonianswimknakf", "Models"));

        workItem->mTextures.emplace_back("textures/_land_default.dds");

        mWorkQueue->addWorkItem(workItem);
    }

    double RenderingManager::getReferenceTime() const
    {
        return mViewer->getFrameStamp()->getReferenceTime();
    }

    osg::Group* RenderingManager::getLightRoot()
    {
        return mSceneRoot.get();
    }

    void RenderingManager::setNightEyeFactor(float factor)
    {
        if (factor != mNightEyeFactor)
        {
            mNightEyeFactor = factor;
            updateAmbient();
        }
    }

    void RenderingManager::setAmbientColour(const osg::Vec4f &colour)
    {
        mAmbientColor = colour;
        updateAmbient();

        if (mPostProcessor)
            mPostProcessor->setAmbientColor(colour);
    }

    void RenderingManager::skySetDate(int day, int month)
    {
        mSky->setDate(day, month);
    }

    int RenderingManager::skyGetMasserPhase() const
    {
        return mSky->getMasserPhase();
    }

    int RenderingManager::skyGetSecundaPhase() const
    {
        return mSky->getSecundaPhase();
    }

    void RenderingManager::skySetMoonColour(bool red)
    {
        mSky->setMoonColour(red);
    }

    void RenderingManager::setCellInterior(bool interior)
    {
        mStateUpdater->setInterior(interior);
    }

    void RenderingManager::configureAmbient(const ESM::Cell *cell)
    {
        // Authoritative cell flag; do not infer interiors from sun direction in shaders.
        mStateUpdater->setInterior(!cell->isExterior() && !(cell->mData.mFlags & ESM::Cell::QuasiEx));
        bool needsAdjusting = false;
        if (mResourceSystem->getSceneManager()->getLightingMethod() != SceneUtil::LightingMethod::FFP)
            needsAdjusting = !cell->isExterior() && !(cell->mData.mFlags & ESM::Cell::QuasiEx);

        auto ambient = SceneUtil::colourFromRGB(cell->mAmbi.mAmbient);

        if (needsAdjusting)
        {
            constexpr float pR = 0.2126;
            constexpr float pG = 0.7152;
            constexpr float pB = 0.0722;

            // we already work in linear RGB so no conversions are needed for the luminosity function
            float relativeLuminance = pR*ambient.r() + pG*ambient.g() + pB*ambient.b();
            if (relativeLuminance < mMinimumAmbientLuminance)
            {
                // brighten ambient so it reaches the minimum threshold but no more, we want to mess with content data as least we can
                float targetBrightnessIncreaseFactor = mMinimumAmbientLuminance / relativeLuminance;
                if (ambient.r() == 0.f && ambient.g() == 0.f && ambient.b() == 0.f)
                    ambient = osg::Vec4(mMinimumAmbientLuminance, mMinimumAmbientLuminance, mMinimumAmbientLuminance, ambient.a());
                else
                    ambient *= targetBrightnessIncreaseFactor;
            }
        }

        setAmbientColour(ambient);

        osg::Vec4f diffuse = SceneUtil::colourFromRGB(cell->mAmbi.mSunlight);
        mSunLight->setDiffuse(diffuse);
        mSunLight->setSpecular(diffuse);
        mSunLight->setPosition(osg::Vec4f(-0.15f, 0.15f, 1.f, 0.f));

        if (mPostProcessor)
        {
            // Post-processing sun for interiors, as OpenMW 0.51 sets it ("what Morrowind uses"). In exteriors the
            // weather overrides it every frame through setSunDirection / setSunColour.
            static const osg::Vec4f interiorSunPos
                = osg::Vec4f(-1.f, osg::DegreesToRadians(45.f), osg::DegreesToRadians(45.f), 0.f);
            mPostProcessor->setSunPos(interiorSunPos, false);
            mPostProcessor->setSunVec(-interiorSunPos);
            mPostProcessor->setSunColor(diffuse, 1.f);
        }
    }

    void RenderingManager::setSunColour(const osg::Vec4f& diffuse, const osg::Vec4f& specular, float sunVis)
    {
        // need to wrap this in a StateUpdater?
        mSunLight->setDiffuse(diffuse);
        mSunLight->setSpecular(specular);

        if (mPostProcessor)
            mPostProcessor->setSunColor(diffuse, sunVis);
    }

    void RenderingManager::setSunDirection(const osg::Vec3f &direction)
    {
        osg::Vec3 position = direction * -1;
        // need to wrap this in a StateUpdater?
        mSunLight->setPosition(osg::Vec4(position.x(), position.y(), position.z(), 0));

        mSky->setSunDirection(position);

        if (mPostProcessor)
        {
            // As OpenMW 0.51: omw.sunPos is the sun disc on its orbit (below the horizon at night),
            // omw.sunVec the light direction.
            osg::Vec3f sunPos = position;
            sunPos.z() = 400.f - std::abs(sunPos.x());
            mPostProcessor->setSunPos(osg::Vec4f(sunPos, 0.f), mNight);
            mPostProcessor->setSunVec(osg::Vec4f(-position, 0.f));
        }
    }

    void RenderingManager::addCell(const MWWorld::CellStore *store)
    {
        mPathgrid->addCell(store);

        mWater->changeCell(store);

        if (store->getCell()->isExterior())
        {
            mTerrain->loadCell(store->getCell()->getGridX(), store->getCell()->getGridY());
            if (mGroundcoverWorld)
                mGroundcoverWorld->loadCell(store->getCell()->getGridX(), store->getCell()->getGridY());
        }
    }
    void RenderingManager::removeCell(const MWWorld::CellStore *store)
    {
        mPathgrid->removeCell(store);
        mActorsPaths->removeCell(store);
        mObjects->removeCell(store);

        if (store->getCell()->isExterior())
        {
            mTerrain->unloadCell(store->getCell()->getGridX(), store->getCell()->getGridY());
            if (mGroundcoverWorld)
                mGroundcoverWorld->unloadCell(store->getCell()->getGridX(), store->getCell()->getGridY());
        }

        mWater->removeCell(store);
    }

    void RenderingManager::enableTerrain(bool enable)
    {
        if (!enable)
            mWater->setCullCallback(nullptr);
        mTerrain->enable(enable);
        if (mGroundcoverWorld)
            mGroundcoverWorld->enable(enable);
    }

    void RenderingManager::setSkyEnabled(bool enabled)
    {
        mSky->setEnabled(enabled);
        if (enabled)
            mShadowManager->enableOutdoorMode();
        else
            mShadowManager->enableIndoorMode();
    }

    bool RenderingManager::toggleBorders()
    {
        bool borders = !mTerrain->getBordersVisible();
        mTerrain->setBordersVisible(borders);
        return borders;
    }

    bool RenderingManager::toggleRenderMode(RenderMode mode)
    {
        if (mode == Render_CollisionDebug || mode == Render_Pathgrid)
            return mPathgrid->toggleRenderMode(mode);
        else if (mode == Render_Wireframe)
        {
            bool wireframe = !mStateUpdater->getWireframe();
            mStateUpdater->setWireframe(wireframe);
            return wireframe;
        }
        else if (mode == Render_Water)
        {
            return mWater->toggle();
        }
        else if (mode == Render_Scene)
        {
            unsigned int mask = mViewer->getCamera()->getCullMask();
            bool enabled = mask&Mask_Scene;
            enabled = !enabled;
            if (enabled)
                mask |= Mask_Scene;
            else
                mask &= ~Mask_Scene;
            mViewer->getCamera()->setCullMask(mask);
            return enabled;
        }
        else if (mode == Render_NavMesh)
        {
            return mNavMesh->toggle();
        }
        else if (mode == Render_ActorsPaths)
        {
            return mActorsPaths->toggle();
        }
        else if (mode == Render_RecastMesh)
        {
            return mRecastMesh->toggle();
        }
        return false;
    }

    void RenderingManager::configureFog(const ESM::Cell *cell)
    {
        mFog->configure(mViewDistance, cell);
    }

    void RenderingManager::configureFog(float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const osg::Vec4f &color)
    {
        mFog->configure(mViewDistance, fogDepth, underwaterFog, dlFactor, dlOffset, color);
    }

    SkyManager* RenderingManager::getSkyManager()
    {
        return mSky.get();
    }

    void RenderingManager::update(float dt, bool paused)
    {
        reportStats();

        updateLandOptimization(dt, paused);

        mUnrefQueue->flush(mWorkQueue.get());

        float rainIntensity = mSky->getPrecipitationAlpha();
        mWater->setRainIntensity(rainIntensity);

        if (!paused)
        {
            mEffectManager->update(dt);
            mSky->update(dt);
            mWater->update(dt);

            if (mGroundcoverUpdater)
            {
                const MWWorld::Ptr& player = mPlayerAnimation->getPtr();
                osg::Vec3f playerPos(player.getRefData().getPosition().asVec3());

                float windSpeed = mSky->getBaseWindSpeed();
                mGroundcoverUpdater->setWindSpeed(windSpeed);
                mGroundcoverUpdater->setPlayerPos(playerPos);
            }
        }


        updateNavMesh();
        updateRecastMesh();

        if (mViewOverShoulderController)
            mViewOverShoulderController->update();
        mCamera->update(dt, paused);

        osg::Vec3d focal, cameraPos;
        mCamera->getPosition(focal, cameraPos);
        mCurrentCameraPos = cameraPos;

        // Switch underwater colouring immediately. A very small hysteresis band
        // prevents head bob and animated water from toggling the state every frame,
        // but there is deliberately no timed debounce or colour/fog interpolation.
        bool underwaterNow = false;
        if (mWater->isActive())
        {
            constexpr float underwaterEnterDepth = 2.f;
            constexpr float underwaterLeaveHeight = 2.f;
            const float depth = mWater->getHeight() - static_cast<float>(cameraPos.z());
            underwaterNow = mUnderwaterFogActive
                ? depth > -underwaterLeaveHeight
                : depth > underwaterEnterDepth;
        }
        mUnderwaterFogActive = underwaterNow;

        mStateUpdater->setFogStart(mFog->getFogStart(mUnderwaterFogActive));
        mStateUpdater->setFogEnd(mFog->getFogEnd(mUnderwaterFogActive));
        setFogColor(mFog->getFogColor(mUnderwaterFogActive));
        mStateUpdater->setUnderwaterBlend(mUnderwaterFogActive ? 1.f : 0.f);

        // Feed the final camera/cell state to the 0.52 post-processing bridge.
        // This lets .omwfx context flags (interior/exterior and underwater/
        // abovewater) switch without a frame of stale camera matrices.
        if (mPostProcessor)
        {
            mPostProcessor->setSceneContext(mStateUpdater->isInterior(), mUnderwaterFogActive);

            // The rest of the "omw" state, as OpenMW 0.51's RenderingManager::update sets it
            mPostProcessor->setFogRange(mFog->getFogStart(mUnderwaterFogActive), mFog->getFogEnd(mUnderwaterFogActive));
            mPostProcessor->setFogColor(mFog->getFogColor(mUnderwaterFogActive));
            mPostProcessor->setSkyColor(mSky->getSkyColor());
            if (MWBase::World* world = MWBase::Environment::get().getWorld())
            {
                mPostProcessor->setGameHour(world->getTimeStamp().getHour());
                mPostProcessor->setWeather(world->getCurrentWeather(), world->getNextWeather(),
                    world->getWeatherTransition(), world->getWindSpeed());
            }

            // Point lights for effects with pass_lights (VAIO's lamp shafts), nearest to the camera first. OpenMW
            // 0.51 fills the same buffer from the lights it draws, skipping negative lights, and stops at 39
            // (PPLightBuffer::setLight there).
            if (mPostProcessor->wantsPointLights())
            {
                const auto& lights = static_cast<SceneUtil::LightManager*>(getLightRoot())->getLights();
                std::vector<std::pair<float, const SceneUtil::LightManager::LightSourceTransform*>> nearest;
                nearest.reserve(lights.size());
                for (const auto& light : lights)
                {
                    if (!light.mLightSource || light.mLightSource->getLight(0)->getDiffuse().x() < 0.f)
                        continue;
                    const osg::Vec3f offset = light.mWorldMatrix.getTrans() - osg::Vec3f(cameraPos);
                    nearest.emplace_back(offset.length2(), &light);
                }
                const std::size_t count = std::min<std::size_t>(nearest.size(), PostProcessor::sMaxPointLights - 1);
                std::partial_sort(nearest.begin(), nearest.begin() + count, nearest.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                std::vector<osg::Vec4f> packed;
                packed.reserve(count * 3);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const SceneUtil::LightManager::LightSourceTransform& light = *nearest[i].second;
                    const osg::Light* osgLight = light.mLightSource->getLight(0);
                    packed.push_back(osg::Vec4f(light.mWorldMatrix.getTrans(), 1.f));
                    packed.push_back(osgLight->getDiffuse());
                    packed.push_back(osg::Vec4f(osgLight->getConstantAttenuation(), osgLight->getLinearAttenuation(),
                        osgLight->getQuadraticAttenuation(), light.mLightSource->getRadius()));
                }
                mPostProcessor->setPointLights(packed);
            }

            mPostProcessor->update(dt);

            // effects that draw their own sun (godrays, VAIO) hide the vanilla glare, as in OpenMW 0.51
            mSky->setSunglare(!mPostProcessor->sunglareDisabled());
        }

    }

    void RenderingManager::updatePlayerPtr(const MWWorld::Ptr &ptr)
    {
        if(mPlayerAnimation.get())
        {
            setupPlayer(ptr);
            mPlayerAnimation->updatePtr(ptr);
        }
        mCamera->attachTo(ptr);
    }

    void RenderingManager::removePlayer(const MWWorld::Ptr &player)
    {
        mWater->removeEmitter(player);
    }

    void RenderingManager::rotateObject(const MWWorld::Ptr &ptr, const osg::Quat& rot)
    {
        if(ptr == mCamera->getTrackingPtr() &&
           !mCamera->isVanityOrPreviewModeEnabled())
        {
            mCamera->rotateCameraToTrackingPtr();
        }

        ptr.getRefData().getBaseNode()->setAttitude(rot);
    }

    void RenderingManager::moveObject(const MWWorld::Ptr &ptr, const osg::Vec3f &pos)
    {
        ptr.getRefData().getBaseNode()->setPosition(pos);
    }

    void RenderingManager::scaleObject(const MWWorld::Ptr &ptr, const osg::Vec3f &scale)
    {
        ptr.getRefData().getBaseNode()->setScale(scale);

        if (ptr == mCamera->getTrackingPtr()) // update height of camera
            mCamera->processViewChange();
    }

    void RenderingManager::removeObject(const MWWorld::Ptr &ptr)
    {
        mActorsPaths->remove(ptr);
        mObjects->removeObject(ptr);
        mWater->removeEmitter(ptr);
    }

    void RenderingManager::setWaterEnabled(bool enabled)
    {
        mWater->setEnabled(enabled);
        mSky->setWaterEnabled(enabled);

        if (mPostProcessor)
            mPostProcessor->setWaterEnabled(enabled);
    }

    void RenderingManager::setWaterHeight(float height)
    {
        mWater->setCullCallback(mTerrain->getHeightCullCallback(height, Mask_Water));
        mWater->setHeight(height);
        mSky->setWaterHeight(height);

        if (mPostProcessor)
            mPostProcessor->setWaterHeight(height);
    }

    void RenderingManager::screenshot(osg::Image* image, int w, int h)
    {
        mScreenshotManager->screenshot(image, w, h);
    }

    bool RenderingManager::screenshot360(osg::Image* image)
    {
        if (mCamera->isVanityOrPreviewModeEnabled())
        {
            Log(Debug::Warning) << "Spherical screenshots are not allowed in preview mode.";
            return false;
        }

        unsigned int maskBackup = mPlayerAnimation->getObjectRoot()->getNodeMask();

        if (mCamera->isFirstPerson())
            mPlayerAnimation->getObjectRoot()->setNodeMask(0);

        mScreenshotManager->screenshot360(image);

        mPlayerAnimation->getObjectRoot()->setNodeMask(maskBackup);

        return true;
    }

    osg::Vec4f RenderingManager::getScreenBounds(const osg::BoundingBox &worldbb)
    {
        if (!worldbb.valid()) return osg::Vec4f();
        osg::Matrix viewProj = mViewer->getCamera()->getViewMatrix() * mViewer->getCamera()->getProjectionMatrix();
        float min_x = 1.0f, max_x = 0.0f, min_y = 1.0f, max_y = 0.0f;
        for (int i=0; i<8; ++i)
        {
            osg::Vec3f corner = worldbb.corner(i);
            corner = corner * viewProj;

            float x = (corner.x() + 1.f) * 0.5f;
            float y = (corner.y() - 1.f) * (-0.5f);

            if (x < min_x)
            min_x = x;

            if (x > max_x)
            max_x = x;

            if (y < min_y)
            min_y = y;

            if (y > max_y)
            max_y = y;
        }

        return osg::Vec4f(min_x, min_y, max_x, max_y);
    }

    RenderingManager::RayResult getIntersectionResult (osgUtil::LineSegmentIntersector* intersector)
    {
        RenderingManager::RayResult result;
        result.mHit = false;
        result.mHitRefnum.unset();
        result.mRatio = 0;
        if (intersector->containsIntersections())
        {
            result.mHit = true;
            osgUtil::LineSegmentIntersector::Intersection intersection = intersector->getFirstIntersection();

            result.mHitPointWorld = intersection.getWorldIntersectPoint();
            result.mHitNormalWorld = intersection.getWorldIntersectNormal();
            result.mRatio = intersection.ratio;

            PtrHolder* ptrHolder = nullptr;
            std::vector<RefnumMarker*> refnumMarkers;
            for (osg::NodePath::const_iterator it = intersection.nodePath.begin(); it != intersection.nodePath.end(); ++it)
            {
                osg::UserDataContainer* userDataContainer = (*it)->getUserDataContainer();
                if (!userDataContainer)
                    continue;
                for (unsigned int i=0; i<userDataContainer->getNumUserObjects(); ++i)
                {
                    if (PtrHolder* p = dynamic_cast<PtrHolder*>(userDataContainer->getUserObject(i)))
                        ptrHolder = p;
                    if (RefnumMarker* r = dynamic_cast<RefnumMarker*>(userDataContainer->getUserObject(i)))
                        refnumMarkers.push_back(r);
                }
            }

            if (ptrHolder)
                result.mHitObject = ptrHolder->mPtr;

            unsigned int vertexCounter = 0;
            for (unsigned int i=0; i<refnumMarkers.size(); ++i)
            {
                unsigned int intersectionIndex = intersection.indexList.empty() ? 0 : intersection.indexList[0];
                if (!refnumMarkers[i]->mNumVertices || (intersectionIndex >= vertexCounter && intersectionIndex < vertexCounter + refnumMarkers[i]->mNumVertices))
                {
                    result.mHitRefnum = refnumMarkers[i]->mRefnum;
                    break;
                }
                vertexCounter += refnumMarkers[i]->mNumVertices;
            }
        }

        return result;

    }

    osg::ref_ptr<osgUtil::IntersectionVisitor> RenderingManager::getIntersectionVisitor(osgUtil::Intersector *intersector, bool ignorePlayer, bool ignoreActors)
    {
        if (!mIntersectionVisitor)
            mIntersectionVisitor = new osgUtil::IntersectionVisitor;

        mIntersectionVisitor->setTraversalNumber(mViewer->getFrameStamp()->getFrameNumber());
        mIntersectionVisitor->setFrameStamp(mViewer->getFrameStamp());
        mIntersectionVisitor->setIntersector(intersector);

        unsigned int mask = ~0u;
        mask &= ~(Mask_RenderToTexture|Mask_Sky|Mask_Debug|Mask_Effect|Mask_Water|Mask_SimpleWater|Mask_Groundcover);
        if (ignorePlayer)
            mask &= ~(Mask_Player);
        if (ignoreActors)
            mask &= ~(Mask_Actor|Mask_Player);

        mIntersectionVisitor->setTraversalMask(mask);
        return mIntersectionVisitor;
    }

    RenderingManager::RayResult RenderingManager::castRay(const osg::Vec3f& origin, const osg::Vec3f& dest, bool ignorePlayer, bool ignoreActors)
    {
        osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector (new osgUtil::LineSegmentIntersector(osgUtil::LineSegmentIntersector::MODEL,
            origin, dest));
        intersector->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);

        mRootNode->accept(*getIntersectionVisitor(intersector, ignorePlayer, ignoreActors));

        return getIntersectionResult(intersector);
    }

    RenderingManager::RayResult RenderingManager::castCameraToViewportRay(const float nX, const float nY, float maxDistance, bool ignorePlayer, bool ignoreActors)
    {
        osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector (new osgUtil::LineSegmentIntersector(osgUtil::LineSegmentIntersector::PROJECTION,
                                                                                                       nX * 2.f - 1.f, nY * (-2.f) + 1.f));

        osg::Vec3d dist (0.f, 0.f, -maxDistance);

        dist = dist * mViewer->getCamera()->getProjectionMatrix();

        osg::Vec3d end = intersector->getEnd();
        end.z() = dist.z();
        intersector->setEnd(end);
        intersector->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);

        mViewer->getCamera()->accept(*getIntersectionVisitor(intersector, ignorePlayer, ignoreActors));

        return getIntersectionResult(intersector);
    }

    void RenderingManager::updatePtr(const MWWorld::Ptr &old, const MWWorld::Ptr &updated)
    {
        mObjects->updatePtr(old, updated);
        mActorsPaths->updatePtr(old, updated);
    }

    void RenderingManager::spawnEffect(const std::string &model, const std::string &texture, const osg::Vec3f &worldPosition, float scale, bool isMagicVFX)
    {
        mEffectManager->addEffect(model, texture, worldPosition, scale, isMagicVFX);
    }

    void RenderingManager::notifyWorldSpaceChanged()
    {
        mEffectManager->clear();
        mWater->clearRipples();
    }

    void RenderingManager::clear()
    {
        mSky->setMoonColour(false);

        mLandOptimizationDistance = mConfiguredViewDistance;
        resetLandOptimization(true);

        notifyWorldSpaceChanged();
        if (mObjectPaging)
            mObjectPaging->clear();
    }

    MWRender::Animation* RenderingManager::getAnimation(const MWWorld::Ptr &ptr)
    {
        if (mPlayerAnimation.get() && ptr == mPlayerAnimation->getPtr())
            return mPlayerAnimation.get();

        return mObjects->getAnimation(ptr);
    }

    const MWRender::Animation* RenderingManager::getAnimation(const MWWorld::ConstPtr &ptr) const
    {
        if (mPlayerAnimation.get() && ptr == mPlayerAnimation->getPtr())
            return mPlayerAnimation.get();

        return mObjects->getAnimation(ptr);
    }

    void RenderingManager::setupPlayer(const MWWorld::Ptr &player)
    {
        if (!mPlayerNode)
        {
            mPlayerNode = new SceneUtil::PositionAttitudeTransform;
            mPlayerNode->setNodeMask(Mask_Player);
            mPlayerNode->setName("Player Root");
            mSceneRoot->addChild(mPlayerNode);
        }

        mPlayerNode->setUserDataContainer(new osg::DefaultUserDataContainer);
        mPlayerNode->getUserDataContainer()->addUserObject(new PtrHolder(player));

        player.getRefData().setBaseNode(mPlayerNode);

        mWater->removeEmitter(player);
        mWater->addEmitter(player);
    }

    void RenderingManager::renderPlayer(const MWWorld::Ptr &player)
    {
        mPlayerAnimation = new NpcAnimation(player, player.getRefData().getBaseNode(), mResourceSystem, 0, NpcAnimation::VM_Normal,
                                                mFirstPersonFieldOfView);

        mCamera->setAnimation(mPlayerAnimation.get());
        mCamera->attachTo(player);
    }

    void RenderingManager::rebuildPtr(const MWWorld::Ptr &ptr)
    {
        NpcAnimation *anim = nullptr;
        if(ptr == mPlayerAnimation->getPtr())
            anim = mPlayerAnimation.get();
        else
            anim = dynamic_cast<NpcAnimation*>(mObjects->getAnimation(ptr));
        if(anim)
        {
            anim->rebuild();
            if(mCamera->getTrackingPtr() == ptr)
            {
                mCamera->attachTo(ptr);
                mCamera->setAnimation(anim);
            }
        }
    }

    void RenderingManager::addWaterRippleEmitter(const MWWorld::Ptr &ptr)
    {
        mWater->addEmitter(ptr);
    }

    void RenderingManager::removeWaterRippleEmitter(const MWWorld::Ptr &ptr)
    {
        mWater->removeEmitter(ptr);
    }

    void RenderingManager::emitWaterRipple(const osg::Vec3f &pos)
    {
        mWater->emitRipple(pos);
    }

    void RenderingManager::updateProjectionMatrix()
    {
        double aspect = mViewer->getCamera()->getViewport()->aspectRatio();
        float fov = mFieldOfView;
        if (mFieldOfViewOverridden)
            fov = mFieldOfViewOverride;
        mViewer->getCamera()->setProjectionMatrixAsPerspective(fov, aspect, mNearClip, mViewDistance);

        mUniformNear->set(mNearClip);
        mUniformFar->set(mViewDistance);

        // Since our fog is not radial yet, we should take FOV in account, otherwise terrain near viewing distance may disappear.
        // Limit FOV here just for sure, otherwise viewing distance can be too high.
        fov = std::min(mFieldOfView, 140.f);
        float distanceMult = std::cos(osg::DegreesToRadians(fov)/2.f);
        // Distant land is always available. The user controls its distance and
        // terrain-detail preset instead of switching the terrain backend off.
        const float terrainDistance = mViewDistance * (distanceMult ? 1.f/distanceMult : 1.f);
        mTerrain->setViewDistance(terrainDistance);

        float adaptiveDistanceScale = 1.f;
        if (mLandOptimizationEnabled && mConfiguredViewDistance > 0.f)
        {
            adaptiveDistanceScale = std::clamp(
                mViewDistance / mConfiguredViewDistance, sLandOptimizationMinimumScale, 1.f);
        }

        if (mGroundcoverWorld)
        {
            float groundcoverDistance = 0.f;
            if (Settings::Manager::getBool("enabled", "Groundcover"))
            {
                groundcoverDistance = mLandOptimizationEnabled
                    ? mConfiguredViewDistance * sLandOptimizationGroundcoverDistanceRatio
                    : std::max(0.f, Settings::Manager::getFloat("rendering distance", "Groundcover"));
                groundcoverDistance *= adaptiveDistanceScale;
            }
            mGroundcoverWorld->setViewDistance(groundcoverDistance * (distanceMult ? 1.f/distanceMult : 1.f));
        }

        if (mShadowManager)
        {
            const float shadowDistance = mLandOptimizationEnabled
                ? mConfiguredViewDistance * sLandOptimizationShadowDistanceRatio
                : std::max(0.f, Settings::Manager::getFloat("maximum shadow map distance", "Shadows"));
            mShadowManager->setMaximumShadowMapDistance(shadowDistance * adaptiveDistanceScale);
        }
    }

    void RenderingManager::applyViewDistance(float distance)
    {
        if (!std::isfinite(distance))
            return;

        distance = std::max(1.f, distance);
        if (std::abs(mViewDistance - distance) < 0.5f)
            return;

        mViewDistance = distance;
        // Keep the current weather/cell fog envelope attached to the moving far plane.
        // Without this, adaptive distance changes can expose a hard terrain cut before fog.
        if (mFog)
            mFog->setViewDistance(mViewDistance);
        updateProjectionMatrix();
    }

    void RenderingManager::updateLandOptimizationProfile()
    {
        std::string mode = Settings::Manager::getString("optimization land", "Camera");
        Misc::StringUtils::lowerCaseInPlace(mode);

        // Preserve compatibility with the earlier boolean setting.
        if (mode == "true" || mode == "1" || mode == "on")
            mode = "balance";
        else if (mode == "false" || mode == "0" || mode == "disabled")
            mode = "off";

        mLandOptimizationEnabled = mode != "off";
        if (mode == "performance")
        {
            mLandOptimizationTargetFps = 45.f;
        }
        else if (mode == "aggressive")
        {
            mLandOptimizationTargetFps = 60.f;
        }
        else if (mLandOptimizationEnabled)
        {
            mLandOptimizationTargetFps = 30.f;
        }
        else
        {
            mLandOptimizationTargetFps = 0.f;
        }

        mLandOptimizationMinDistance = mLandOptimizationEnabled
            ? mConfiguredViewDistance * sLandOptimizationMinimumScale
            : mConfiguredViewDistance;
    }

    void RenderingManager::resetLandOptimization(bool restoreConfiguredDistance)
    {
        mLandOptimizationTimer = 0.f;
        mLandOptimizationFrameTime = 0.f;
        mLandOptimizationFrameCount = 0;
        mLandOptimizationWasExterior = false;
        mLandOptimizationDistance = std::clamp(mLandOptimizationDistance,
            std::min(mLandOptimizationMinDistance, mConfiguredViewDistance), mConfiguredViewDistance);

        if (restoreConfiguredDistance)
            applyViewDistance(mConfiguredViewDistance);
    }

    void RenderingManager::updateLandOptimization(float frameDuration, bool paused)
    {
        if (!mLandOptimizationEnabled)
        {
            if (mViewDistance != mConfiguredViewDistance)
                applyViewDistance(mConfiguredViewDistance);
            return;
        }

        const bool isExterior = MWMechanics::getPlayer().isInCell()
            && (MWMechanics::getPlayer().getCell()->isExterior()
                || MWBase::Environment::get().getWorld()->isCellQuasiExterior());

        if (!isExterior)
        {
            mLandOptimizationWasExterior = false;
            mLandOptimizationTimer = 0.f;
            mLandOptimizationFrameTime = 0.f;
            mLandOptimizationFrameCount = 0;
            applyViewDistance(mConfiguredViewDistance);
            return;
        }

        const float minDistance = std::min(mLandOptimizationMinDistance, mConfiguredViewDistance);
        mLandOptimizationDistance = std::clamp(mLandOptimizationDistance, minDistance, mConfiguredViewDistance);

        if (!mLandOptimizationWasExterior)
            mLandOptimizationWasExterior = true;

        if (frameDuration <= 0.f || !std::isfinite(frameDuration))
            return;

        // Smoothly move the actual camera/terrain distance toward the optimizer target.
        // Exponential response is frame-rate independent and naturally makes large corrections
        // fast while easing the last part, eliminating the visible concentric "steps".
        const float distanceDelta = mLandOptimizationDistance - mViewDistance;
        if (std::abs(distanceDelta) >= 0.5f)
        {
            const float smoothingDt = std::min(frameDuration, sLandOptimizationSmoothingMaxFrameTime);
            const float blend = 1.f - std::exp(-sLandOptimizationDistanceResponse * smoothingDt);
            float smoothedDistance = mViewDistance + distanceDelta * blend;
            if (std::abs(mLandOptimizationDistance - smoothedDistance) < 0.5f)
                smoothedDistance = mLandOptimizationDistance;
            applyViewDistance(smoothedDistance);
        }

        // Continue smoothing while a GUI is open, but do not use menu/loading frame times
        // for adaptive FPS decisions.
        if (paused || MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            mLandOptimizationTimer = 0.f;
            mLandOptimizationFrameTime = 0.f;
            mLandOptimizationFrameCount = 0;
            return;
        }

        mLandOptimizationTimer += frameDuration;
        mLandOptimizationFrameTime += frameDuration;
        ++mLandOptimizationFrameCount;

        if (mLandOptimizationTimer < sLandOptimizationUpdateInterval)
            return;

        const float averageFrameTime = mLandOptimizationFrameTime
            / static_cast<float>(std::max(1u, mLandOptimizationFrameCount));

        mLandOptimizationTimer = std::fmod(mLandOptimizationTimer, sLandOptimizationUpdateInterval);
        mLandOptimizationFrameTime = 0.f;
        mLandOptimizationFrameCount = 0;

        if (averageFrameTime <= 0.f || !std::isfinite(averageFrameTime)
            || mLandOptimizationTargetFps <= 0.f)
            return;

        const float averageFps = 1.f / averageFrameTime;
        float adjustment = 0.f;
        if (averageFps < mLandOptimizationTargetFps)
        {
            const float deficit = std::clamp(
                (mLandOptimizationTargetFps - averageFps) / mLandOptimizationTargetFps, 0.f, 1.f);
            adjustment = -(sLandOptimizationMinimumStep
                + deficit * (sLandOptimizationMaximumStep - sLandOptimizationMinimumStep));
        }
        else if (averageFps >= mLandOptimizationTargetFps + sLandOptimizationRecoveryMarginFps)
            adjustment = sLandOptimizationRecoveryStep;

        if (adjustment == 0.f)
            return;

        const float newDistance = std::clamp(mLandOptimizationDistance + adjustment,
            minDistance, mConfiguredViewDistance);
        if (std::abs(newDistance - mLandOptimizationDistance) < 0.5f)
            return;

        // Store a target only. The actual view distance is eased toward it every frame above.
        mLandOptimizationDistance = newDistance;
    }

    void RenderingManager::updateTextureFiltering()
    {
        mViewer->stopThreading();

        mResourceSystem->getSceneManager()->setFilterSettings(
            Settings::Manager::getString("texture mag filter", "General"),
            Settings::Manager::getString("texture min filter", "General"),
            Settings::Manager::getString("texture mipmap", "General"),
            Settings::Manager::getInt("anisotropy", "General")
        );

        mTerrain->updateTextureFiltering();

        mViewer->startThreading();
    }

    void RenderingManager::updateAmbient()
    {
        osg::Vec4f color = mAmbientColor;

        if (mNightEyeFactor > 0.f)
            color += osg::Vec4f(0.7, 0.7, 0.7, 0.0) * mNightEyeFactor;

        mStateUpdater->setAmbientColor(color);
    }

    void RenderingManager::setFogColor(const osg::Vec4f &color)
    {
        mViewer->getCamera()->setClearColor(color);

        mStateUpdater->setFogColor(color);
    }

    void RenderingManager::reportStats() const
    {
        osg::Stats* stats = mViewer->getViewerStats();
        unsigned int frameNumber = mViewer->getFrameStamp()->getFrameNumber();
        if (stats->collectStats("resource"))
        {
            stats->setAttribute(frameNumber, "UnrefQueue", mUnrefQueue->getNumItems());

            mTerrain->reportStats(frameNumber, stats);
        }
    }

    void RenderingManager::processChangedSettings(const Settings::CategorySettingVector &changed)
    {
        bool refreshShaderDefines = false;
        bool refreshMaterialQuality = false;
        bool refreshShadowSettings = false;
        bool refreshTerrainLodSettings = false;
        bool rebuildTerrainViews = false;
        bool rebuildGroundcoverViews = false;

        for (const auto& setting : changed)
        {
            if (setting.first == "Camera" && setting.second == "field of view")
            {
                mFieldOfView = Settings::Manager::getFloat("field of view", "Camera");
                updateProjectionMatrix();
                rebuildTerrainViews = true;
                rebuildGroundcoverViews = true;
            }
            else if (setting.first == "Camera" && setting.second == "viewing distance")
            {
                mConfiguredViewDistance = Settings::Manager::getFloat("viewing distance", "Camera");
                updateLandOptimizationProfile();
                mLandOptimizationDistance = mConfiguredViewDistance;
                resetLandOptimization(false);

                const bool isExterior = MWMechanics::getPlayer().isInCell()
                    && (MWMechanics::getPlayer().getCell()->isExterior()
                        || MWBase::Environment::get().getWorld()->isCellQuasiExterior());
                mViewDistance = mLandOptimizationEnabled && isExterior
                    ? mLandOptimizationDistance : mConfiguredViewDistance;
                mStateUpdater->setFogEnd(mViewDistance);
                updateProjectionMatrix();
                rebuildTerrainViews = true;
            }
            else if (setting.first == "Camera" && setting.second == "optimization land")
            {
                updateLandOptimizationProfile();
                if (mLandOptimizationEnabled)
                {
                    mLandOptimizationDistance = mConfiguredViewDistance;
                    resetLandOptimization(false);
                }
                else
                    resetLandOptimization(true);
            }
            else if (setting.first == "Camera" && setting.second == "view over shoulder")
            {
                if (Settings::Manager::getBool("view over shoulder", "Camera"))
                {
                    // Recreate the controller so all offsets/crosshair/dynamic-distance state is refreshed.
                    mViewOverShoulderController.reset(new ViewOverShoulderController(mCamera.get()));
                }
                else
                    mViewOverShoulderController.reset();
            }
            else if (setting.first == "Camera" && (setting.second == "auto switch shoulder"
                || setting.second == "view over shoulder offset"))
            {
                if (Settings::Manager::getBool("view over shoulder", "Camera"))
                    mViewOverShoulderController.reset(new ViewOverShoulderController(mCamera.get()));
            }
            else if (setting.first == "Camera" && (setting.second == "dynamic camera"
                || setting.second == "dynamic camera strafe roll"
                || setting.second == "dynamic camera look roll"
                || setting.second == "dynamic camera jump pitch"
                || setting.second == "dynamic camera landing pitch"
                || setting.second == "dynamic camera smoothing"
                || setting.second == "head bobbing"
                || setting.second == "immersive first person"))
            {
                mCamera->reloadSettings();
            }
            else if (setting.first == "Terrain")
            {
                if (setting.second == "distant terrain")
                {
                    // Kept for compatibility with old settings.cfg files. Distant land
                    // is no longer disabled; refresh the projection and terrain views.
                    updateProjectionMatrix();
                    rebuildTerrainViews = true;
                }
                else if (setting.second == "lod factor"
                    || setting.second == "vertex lod mod"
                    || setting.second == "composite map level"
                    || setting.second == "composite map resolution"
                    || setting.second == "max composite geometry size"
                    || setting.second == "object paging"
                    || setting.second == "object paging active grid"
                    || setting.second == "object paging merge factor"
                    || setting.second == "object paging min size"
                    || setting.second == "object paging min size merge factor"
                    || setting.second == "object paging min size cost multiplier")
                {
                    refreshTerrainLodSettings = true;
                    rebuildTerrainViews = true;

                    if (setting.second == "lod factor")
                    {
                        // Re-evaluate the effective PBR level when crossing the
                        // High-terrain threshold, including externally edited configs.
                        const int materialQuality = getMaterialQualityLevel();
                        const bool normalMaps = materialUsesNormalMaps(materialQuality);
                        const bool specularMaps = materialUsesSpecularMaps(materialQuality);
                        mResourceSystem->getSceneManager()->setAutoUseNormalMaps(normalMaps);
                        mResourceSystem->getSceneManager()->setAutoUseSpecularMaps(specularMaps);
                        if (mTerrainStorage)
                            mTerrainStorage->setAutoUseMaterialMaps(normalMaps, specularMaps);
                        refreshMaterialQuality = true;
                        refreshShaderDefines = true;
                    }
                }
            }
            else if (setting.first == "Groundcover")
            {
                if (setting.second == "enabled")
                {
                    if (mGroundcoverRoot)
                        mGroundcoverRoot->setNodeMask(Settings::Manager::getBool("enabled", "Groundcover") ? Mask_Groundcover : 0u);
                    updateProjectionMatrix();
                    rebuildGroundcoverViews = true;
                }
                else if (setting.second == "density")
                {
                    if (mGroundcover)
                        mGroundcover->setDensity(Settings::Manager::getFloat("density", "Groundcover"));
                    rebuildGroundcoverViews = true;
                }
                else if (setting.second == "rendering distance")
                {
                    updateProjectionMatrix();
                    refreshShaderDefines = true;
                    rebuildGroundcoverViews = true;
                }
                else if (setting.second == "stomp mode" || setting.second == "stomp intensity")
                    refreshShaderDefines = true;
            }
            else if (setting.first == "General" && (setting.second == "texture filter" ||
                                                     setting.second == "texture mipmap" ||
                                                     setting.second == "anisotropy"))
            {
                updateTextureFiltering();
            }
            else if (setting.first == "Water")
            {
                mWater->processChangedSettings(changed);
            }
            else if (setting.first == "Shaders" && setting.second == "material quality")
            {
                const int materialQuality = getMaterialQualityLevel();
                const bool normalMaps = materialUsesNormalMaps(materialQuality);
                const bool specularMaps = materialUsesSpecularMaps(materialQuality);
                mResourceSystem->getSceneManager()->setAutoUseNormalMaps(normalMaps);
                mResourceSystem->getSceneManager()->setAutoUseSpecularMaps(specularMaps);
                if (mTerrainStorage)
                    mTerrainStorage->setAutoUseMaterialMaps(normalMaps, specularMaps);
                refreshMaterialQuality = true;
                refreshShaderDefines = true;
                rebuildTerrainViews = true;
            }
            else if (setting.first == "Shaders" && setting.second == "minimum interior brightness")
            {
                mMinimumAmbientLuminance = std::clamp(Settings::Manager::getFloat("minimum interior brightness", "Shaders"), 0.f, 1.f);
                if (MWMechanics::getPlayer().isInCell())
                    configureAmbient(MWMechanics::getPlayer().getCell()->getCell());
            }
            else if (setting.first == "Shaders" && (setting.second == "light bounds multiplier" ||
                                                     setting.second == "maximum light distance" ||
                                                     setting.second == "light fade start" ||
                                                     setting.second == "max lights"))
            {
                auto* lightManager = static_cast<SceneUtil::LightManager*>(getLightRoot());
                lightManager->processChangedSettings(changed);

                if (setting.second == "max lights" && !lightManager->usingFFP())
                {
                    mViewer->stopThreading();

                    lightManager->updateMaxLights();

                    auto defines = mResourceSystem->getSceneManager()->getShaderManager().getGlobalDefines();
                    for (const auto& [name, key] : lightManager->getLightDefines())
                        defines[name] = key;
                    mResourceSystem->getSceneManager()->getShaderManager().setGlobalDefines(defines);

                    mSceneRoot->removeUpdateCallback(mStateUpdater);
                    mStateUpdater = new StateUpdater;
                    mSceneRoot->addUpdateCallback(mStateUpdater);
                    mStateUpdater->setFogEnd(mViewDistance);
                    updateAmbient();

                    mViewer->startThreading();
                }
            }
            else if (setting.first == "Shadows")
            {
                // Sampling-only controls are uniforms updated by StateUpdater.
                // Do not rebuild cascades/shadow cameras or shader defines while
                // the user drags these sliders.
                if (setting.second != "enhanced filtering"
                    && setting.second != "filter softness"
                    && setting.second != "adaptive softness")
                {
                    refreshShadowSettings = true;
                    refreshShaderDefines = true;
                }
            }
        }

        if (mPostProcessor)
            mPostProcessor->processChangedSettings(changed);

        if (refreshTerrainLodSettings && mTerrain)
        {
            mViewer->stopThreading();

            auto* terrain = dynamic_cast<Terrain::QuadTreeWorld*>(mTerrain.get());
            if (terrain)
            {
                if (Settings::Manager::getBool("object paging", "Terrain") && !mObjectPaging)
                {
                    mObjectPaging.reset(new ObjectPaging(
                        mResourceSystem->getSceneManager(), mOcclusionCuller.get()));
                    terrain->addChunkManager(mObjectPaging.get());
                    mResourceSystem->addResourceManager(mObjectPaging.get());
                }

                const int compMapResolution = Settings::Manager::getInt(
                    "composite map resolution", "Terrain");
                const int compMapPower = std::max(-3, Settings::Manager::getInt(
                    "composite map level", "Terrain"));
                terrain->setLodSettings(compMapResolution,
                    std::pow(2.f, static_cast<float>(compMapPower)),
                    Settings::Manager::getFloat("lod factor", "Terrain"),
                    Settings::Manager::getInt("vertex lod mod", "Terrain"),
                    Settings::Manager::getFloat("max composite geometry size", "Terrain"));

                if (mObjectPaging)
                    mObjectPaging->reloadSettings();

                terrain->rebuildViews();
                rebuildTerrainViews = false;
            }

            updateProjectionMatrix();
            mViewer->startThreading();
        }

        if (refreshShadowSettings || refreshShaderDefines)
        {
            mViewer->stopThreading();

            if (refreshShadowSettings && mShadowManager)
            {
                int outdoorShadowCastingMask = Mask_Scene;
                if (Settings::Manager::getBool("actor shadows", "Shadows"))
                    outdoorShadowCastingMask |= Mask_Actor;
                if (Settings::Manager::getBool("player shadows", "Shadows"))
                    outdoorShadowCastingMask |= Mask_Player;
                if (Settings::Manager::getBool("terrain shadows", "Shadows"))
                    outdoorShadowCastingMask |= Mask_Terrain;

                // Keep the established indoor rule: actors/player can cast indoors,
                // while world objects remain an outdoor-only category.
                const int indoorShadowCastingMask = outdoorShadowCastingMask;
                if (Settings::Manager::getBool("object shadows", "Shadows"))
                    outdoorShadowCastingMask |= (Mask_Object | Mask_Static);

                mShadowManager->setShadowCastingMasks(outdoorShadowCastingMask, indoorShadowCastingMask);
                mShadowManager->setupShadowSettings();
                if (mSky->isEnabled())
                    mShadowManager->enableOutdoorMode();
                else
                    mShadowManager->enableIndoorMode();

                // Reapply the current adaptive scale after rebuilding shadow settings.
                updateProjectionMatrix();
            }

            auto defines = mResourceSystem->getSceneManager()->getShaderManager().getGlobalDefines();

            if (mShadowManager)
            {
                const auto shadowDefines = mShadowManager->getShadowDefines();
                for (const auto& [name, value] : shadowDefines)
                    defines[name] = value;
            }

            defines["materialQuality"] = std::to_string(getMaterialQualityLevel());
            const float groundcoverDistance = std::max(0.f, Settings::Manager::getFloat("rendering distance", "Groundcover"));
            defines["groundcoverFadeStart"] = std::to_string(groundcoverDistance * 0.9f);
            defines["groundcoverFadeEnd"] = std::to_string(groundcoverDistance);
            defines["groundcoverStompMode"] = std::to_string(std::clamp(Settings::Manager::getInt("stomp mode", "Groundcover"), 0, 2));
            defines["groundcoverStompIntensity"] = std::to_string(std::clamp(Settings::Manager::getInt("stomp intensity", "Groundcover"), 0, 2));

            mResourceSystem->getSceneManager()->getShaderManager().setGlobalDefines(defines);

            if (refreshMaterialQuality && mObjects)
                mObjects->recreateShaders();

            mViewer->startThreading();
        }

        if (rebuildTerrainViews && mTerrain)
            mTerrain->rebuildViews();
        if (rebuildGroundcoverViews && mGroundcoverWorld)
            mGroundcoverWorld->rebuildViews();
    }

    float RenderingManager::getNearClipDistance() const
    {
        return mNearClip;
    }

    float RenderingManager::getTerrainHeightAt(const osg::Vec3f &pos)
    {
        return mTerrain->getHeightAt(pos);
    }

    void RenderingManager::overrideFieldOfView(float val)
    {
        if (mFieldOfViewOverridden != true || mFieldOfViewOverride != val)
        {
            mFieldOfViewOverridden = true;
            mFieldOfViewOverride = val;
            updateProjectionMatrix();
        }
    }

    osg::Vec3f RenderingManager::getHalfExtents(const MWWorld::ConstPtr& object) const
    {
        osg::Vec3f halfExtents(0, 0, 0);
        std::string modelName = object.getClass().getModel(object);
        if (modelName.empty())
            return halfExtents;

        osg::ref_ptr<const osg::Node> node = mResourceSystem->getSceneManager()->getTemplate(modelName);
        osg::ComputeBoundsVisitor computeBoundsVisitor;
        computeBoundsVisitor.setTraversalMask(~(MWRender::Mask_ParticleSystem|MWRender::Mask_Effect));
        const_cast<osg::Node*>(node.get())->accept(computeBoundsVisitor);
        osg::BoundingBox bounds = computeBoundsVisitor.getBoundingBox();

        if (bounds.valid())
        {
            halfExtents[0] = std::abs(bounds.xMax() - bounds.xMin()) / 2.f;
            halfExtents[1] = std::abs(bounds.yMax() - bounds.yMin()) / 2.f;
            halfExtents[2] = std::abs(bounds.zMax() - bounds.zMin()) / 2.f;
        }

        return halfExtents;
    }

    void RenderingManager::resetFieldOfView()
    {
        if (mFieldOfViewOverridden == true)
        {
            mFieldOfViewOverridden = false;

            updateProjectionMatrix();
        }
    }
    void RenderingManager::exportSceneGraph(const MWWorld::Ptr &ptr, const std::string &filename, const std::string &format)
    {
        osg::Node* node = mViewer->getSceneData();
        if (!ptr.isEmpty())
            node = ptr.getRefData().getBaseNode();

        SceneUtil::writeScene(node, filename, format);
    }

    LandManager *RenderingManager::getLandManager() const
    {
        return mTerrainStorage->getLandManager();
    }

    void RenderingManager::updateActorPath(const MWWorld::ConstPtr& actor, const std::deque<osg::Vec3f>& path,
            const osg::Vec3f& halfExtents, const osg::Vec3f& start, const osg::Vec3f& end) const
    {
        mActorsPaths->update(actor, path, halfExtents, start, end, mNavigator.getSettings());
    }

    void RenderingManager::removeActorPath(const MWWorld::ConstPtr& actor) const
    {
        mActorsPaths->remove(actor);
    }

    void RenderingManager::setNavMeshNumber(const std::size_t value)
    {
        mNavMeshNumber = value;
    }

    void RenderingManager::updateNavMesh()
    {
        if (!mNavMesh->isEnabled())
            return;

        const auto navMeshes = mNavigator.getNavMeshes();

        auto it = navMeshes.begin();
        for (std::size_t i = 0; it != navMeshes.end() && i < mNavMeshNumber; ++i)
            ++it;
        if (it == navMeshes.end())
        {
            mNavMesh->reset();
        }
        else
        {
            try
            {
                const auto locked = it->second->lockConst();
                mNavMesh->update(locked->getImpl(), mNavMeshNumber, locked->getGeneration(),
                                 locked->getNavMeshRevision(), mNavigator.getSettings());
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "NavMesh render update exception: " << e.what();
            }
        }
    }

    void RenderingManager::updateRecastMesh()
    {
        if (!mRecastMesh->isEnabled())
            return;

        mRecastMesh->update(mNavigator.getRecastMeshTiles(), mNavigator.getSettings());
    }

    bool RenderingManager::occlusionVisible(const MWWorld::ConstPtr& ptr) const
    {
        (void)ptr;
        return true;
    }

    void RenderingManager::rebuildOcclusionBuffer(const osg::Vec3f& eyePoint)
    {
        (void)eyePoint;
    }

    void RenderingManager::setActiveGrid(const osg::Vec4i &grid)
    {
        mTerrain->setActiveGrid(grid);
    }
    bool RenderingManager::pagingEnableObject(int type, const MWWorld::ConstPtr& ptr, bool enabled)
    {
        if (!ptr.isInCell() || !ptr.getCell()->isExterior() || !mObjectPaging)
            return false;

        if (enabled && !occlusionVisible(ptr))
            enabled = false;

        if (mObjectPaging->enableObject(type, ptr.getCellRef().getRefNum(), ptr.getCellRef().getPosition().asVec3(), osg::Vec2i(ptr.getCell()->getCell()->getGridX(), ptr.getCell()->getCell()->getGridY()), enabled))
        {
            mTerrain->rebuildViews();
            return true;
        }
        return false;
    }
    void RenderingManager::pagingBlacklistObject(int type, const MWWorld::ConstPtr &ptr)
    {
        if (!ptr.isInCell() || !ptr.getCell()->isExterior() || !mObjectPaging)
            return;
        const ESM::RefNum & refnum = ptr.getCellRef().getRefNum();
        if (!refnum.hasContentFile()) return;
        if (mObjectPaging->blacklistObject(type, refnum, ptr.getCellRef().getPosition().asVec3(), osg::Vec2i(ptr.getCell()->getCell()->getGridX(), ptr.getCell()->getCell()->getGridY())))
            mTerrain->rebuildViews();
    }
    bool RenderingManager::pagingUnlockCache()
    {
        if (mObjectPaging && mObjectPaging->unlockCache())
        {
            mTerrain->rebuildViews();
            return true;
        }
        return false;
    }
    void RenderingManager::getPagedRefnums(const osg::Vec4i &activeGrid, std::set<ESM::RefNum> &out)
    {
        if (mObjectPaging)
            mObjectPaging->getPagedRefnums(activeGrid, out);
    }
}
