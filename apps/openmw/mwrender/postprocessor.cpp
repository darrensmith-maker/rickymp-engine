#include "postprocessor.hpp"

#include <components/debug/debuglog.hpp>
#include <components/fx/technique.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>

#include "vismask.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <sstream>
#include <utility>

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Matrix>
#include <osg/Program>
#include <osg/RenderInfo>
#include <osg/Shader>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Vec2>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/Viewport>

namespace
{
    std::string trim(std::string value)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    bool endsWith(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size()
            && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    int getInternalFormat(const std::string& value)
    {
#if defined(__ANDROID__)
        // R4 intentionally prefers safe formats on NG-GL4ES. Desktop keeps the
        // floating-point targets used by the donor .omwfx files. Android HDR is
        // enabled in the next capability-driven render-target checkpoint.
        if (value == "red" || value == "r16f" || value == "r32f")
            return GL_LUMINANCE;
        if (value == "rg" || value == "rg16f" || value == "rg32f")
            return GL_LUMINANCE_ALPHA;
        if (value == "rgb" || value == "rgb16f" || value == "rgb32f")
            return GL_RGB;
        return GL_RGBA;
#else
        if (value == "red")
        {
#ifdef GL_RED
            return GL_RED;
#else
            return GL_LUMINANCE;
#endif
        }
        if (value == "r16f")
        {
#ifdef GL_R16F
            return GL_R16F;
#elif defined(GL_LUMINANCE16F_ARB)
            return GL_LUMINANCE16F_ARB;
#else
            return GL_LUMINANCE;
#endif
        }
        if (value == "r32f")
        {
#ifdef GL_R32F
            return GL_R32F;
#elif defined(GL_LUMINANCE32F_ARB)
            return GL_LUMINANCE32F_ARB;
#else
            return GL_LUMINANCE;
#endif
        }
        if (value == "rg")
        {
#ifdef GL_RG
            return GL_RG;
#else
            return GL_LUMINANCE_ALPHA;
#endif
        }
        if (value == "rg16f")
        {
#ifdef GL_RG16F
            return GL_RG16F;
#else
            return GL_LUMINANCE_ALPHA;
#endif
        }
        if (value == "rg32f")
        {
#ifdef GL_RG32F
            return GL_RG32F;
#else
            return GL_LUMINANCE_ALPHA;
#endif
        }
        if (value == "rgb16f")
        {
#ifdef GL_RGB16F
            return GL_RGB16F;
#elif defined(GL_RGB16F_ARB)
            return GL_RGB16F_ARB;
#else
            return GL_RGB;
#endif
        }
        if (value == "rgb32f")
        {
#ifdef GL_RGB32F
            return GL_RGB32F;
#elif defined(GL_RGB32F_ARB)
            return GL_RGB32F_ARB;
#else
            return GL_RGB;
#endif
        }
        if (value == "rgba16f")
        {
#ifdef GL_RGBA16F
            return GL_RGBA16F;
#elif defined(GL_RGBA16F_ARB)
            return GL_RGBA16F_ARB;
#else
            return GL_RGBA;
#endif
        }
        if (value == "rgba32f")
        {
#ifdef GL_RGBA32F
            return GL_RGBA32F;
#elif defined(GL_RGBA32F_ARB)
            return GL_RGBA32F_ARB;
#else
            return GL_RGBA;
#endif
        }
        if (value == "rgb")
            return GL_RGB;
        return GL_RGBA;
#endif
    }

    unsigned int getSourceFormat(const std::string& value)
    {
        if (value == "rgb") return GL_RGB;
        if (value == "rgba") return GL_RGBA;
#ifdef GL_BGR
        if (value == "bgr") return GL_BGR;
#endif
#ifdef GL_BGRA
        if (value == "bgra") return GL_BGRA;
#endif
#ifdef GL_RED
        if (value == "red") return GL_RED;
#else
        if (value == "red") return GL_LUMINANCE;
#endif
#ifdef GL_RG
        if (value == "rg") return GL_RG;
#else
        if (value == "rg") return GL_LUMINANCE_ALPHA;
#endif
        return GL_RGBA;
    }

    unsigned int getSourceType(const std::string& value)
    {
        if (value == "byte") return GL_BYTE;
        if (value == "unsigned_byte") return GL_UNSIGNED_BYTE;
        if (value == "short") return GL_SHORT;
        if (value == "unsigned_short") return GL_UNSIGNED_SHORT;
        if (value == "int") return GL_INT;
        if (value == "unsigned_int") return GL_UNSIGNED_INT;
        if (value == "float") return GL_FLOAT;
#if defined(GL_HALF_FLOAT) && !defined(__ANDROID__)
        if (value == "half_float") return GL_HALF_FLOAT;
#elif defined(GL_HALF_FLOAT_ARB) && !defined(__ANDROID__)
        if (value == "half_float") return GL_HALF_FLOAT_ARB;
#endif
        return GL_UNSIGNED_BYTE;
    }

    osg::Texture::FilterMode getFilter(const std::string& value)
    {
        if (value == "nearest") return osg::Texture::NEAREST;
        if (value == "linear_mipmap_linear") return osg::Texture::LINEAR_MIPMAP_LINEAR;
        if (value == "linear_mipmap_nearest") return osg::Texture::LINEAR_MIPMAP_NEAREST;
        if (value == "nearest_mipmap_linear") return osg::Texture::NEAREST_MIPMAP_LINEAR;
        if (value == "nearest_mipmap_nearest") return osg::Texture::NEAREST_MIPMAP_NEAREST;
        return osg::Texture::LINEAR;
    }

    osg::Texture::WrapMode getWrap(const std::string& value)
    {
        if (value == "repeat") return osg::Texture::REPEAT;
        if (value == "mirror") return osg::Texture::MIRROR;
#ifdef GL_CLAMP_TO_BORDER
        if (value == "clamp_to_border") return osg::Texture::CLAMP_TO_BORDER;
#endif
        return osg::Texture::CLAMP_TO_EDGE;
    }

    std::array<float, 4> configuredUniformValue(const std::string& effectName, const Fx::UniformSpec& uniform)
    {
        std::array<float, 4> value = uniform.mDefault;
        const std::string category = std::string("Post Processing ") + effectName;
        const auto key = std::make_pair(category, uniform.mName);

        std::string text;
        const auto userIt = Settings::Manager::mUserSettings.find(key);
        if (userIt != Settings::Manager::mUserSettings.end())
            text = userIt->second;
        else
        {
            const auto defaultIt = Settings::Manager::mDefaultSettings.find(key);
            if (defaultIt == Settings::Manager::mDefaultSettings.end())
                return value;
            text = defaultIt->second;
        }

        std::stringstream stream(text);
        const int count = uniform.components();
        if (uniform.mType == Fx::UniformType::Bool)
        {
            std::string token;
            stream >> token;
            std::transform(token.begin(), token.end(), token.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            value[0] = (token == "true" || token == "1" || token == "on") ? 1.f : 0.f;
            return value;
        }
        for (int i = 0; i < count; ++i)
        {
            float component = value[static_cast<std::size_t>(i)];
            if (!(stream >> component))
                break;
            value[static_cast<std::size_t>(i)] = component;
        }
        return value;
    }

    osg::ref_ptr<osg::Uniform> makeEffectUniform(
        const Fx::UniformSpec& uniform, const std::array<float, 4>& value)
    {
        switch (uniform.mType)
        {
            case Fx::UniformType::Bool:
                return new osg::Uniform(uniform.mName.c_str(), value[0] != 0.f);
            case Fx::UniformType::Int:
                return new osg::Uniform(uniform.mName.c_str(), static_cast<int>(std::lround(value[0])));
            case Fx::UniformType::Float:
                return new osg::Uniform(uniform.mName.c_str(), value[0]);
            case Fx::UniformType::Vec2:
                return new osg::Uniform(uniform.mName.c_str(), osg::Vec2f(value[0], value[1]));
            case Fx::UniformType::Vec3:
                return new osg::Uniform(uniform.mName.c_str(), osg::Vec3f(value[0], value[1], value[2]));
            case Fx::UniformType::Vec4:
                return new osg::Uniform(uniform.mName.c_str(), osg::Vec4f(value[0], value[1], value[2], value[3]));
        }
        return nullptr;
    }

    void setEffectUniformValue(osg::Uniform& osgUniform, const Fx::UniformSpec& uniform,
        const std::array<float, 4>& value)
    {
        switch (uniform.mType)
        {
            case Fx::UniformType::Bool: osgUniform.set(value[0] != 0.f); break;
            case Fx::UniformType::Int: osgUniform.set(static_cast<int>(std::lround(value[0]))); break;
            case Fx::UniformType::Float: osgUniform.set(value[0]); break;
            case Fx::UniformType::Vec2: osgUniform.set(osg::Vec2f(value[0], value[1])); break;
            case Fx::UniformType::Vec3: osgUniform.set(osg::Vec3f(value[0], value[1], value[2])); break;
            case Fx::UniformType::Vec4: osgUniform.set(osg::Vec4f(value[0], value[1], value[2], value[3])); break;
        }
    }

    const char* kFullscreenVertex = R"GLSL(#version 120
varying vec2 omw_TexCoord;
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
    omw_TexCoord = gl_Vertex.xy * 0.5 + vec2(0.5);
}
)GLSL";

    const char* kBlitFragment = R"GLSL(#version 120
varying vec2 omw_TexCoord;
uniform sampler2D sourceTexture;
void main()
{
    gl_FragColor = texture2D(sourceTexture, omw_TexCoord);
}
)GLSL";

    const char* kEyeAdaptFragment = R"GLSL(#version 120
varying vec2 omw_TexCoord;
uniform sampler2D sceneTexture;
uniform sampler2D previousExposure;
uniform float deltaTime;
uniform float adaptationSpeed;

float luminance(vec3 color)
{
    return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.004);
}

void main()
{
    // Compact C++17/GLSL120 compatibility version of the 0.52 eye-adaptation
    // stage. A 4x4 logarithmic sample grid is stable on desktop GL and on the
    // NG-GL4ES path without compute shaders or textureLod requirements.
    float logSum = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 uv = (vec2(float(x), float(y)) + vec2(0.5)) / 4.0;
            logSum += log2(luminance(texture2D(sceneTexture, uv).rgb));
        }
    }
    float current = exp2(logSum / 16.0);
    float previous = max(texture2D(previousExposure, vec2(0.5)).r, 0.004);
    float blend = 1.0 - exp(-max(deltaTime, 0.0) * max(adaptationSpeed, 0.0));
    float adapted = mix(previous, current, clamp(blend, 0.0, 1.0));
    gl_FragColor = vec4(adapted, adapted, adapted, 1.0);
}
)GLSL";
}

namespace MWRender
{
    std::atomic<PostProcessor*> PostProcessor::sActiveProcessor{nullptr};

    struct PostProcessor::RuntimeTechnique
    {
        std::string mName;
        std::shared_ptr<Fx::Technique> mTechnique;
        std::unordered_map<std::string, osg::ref_ptr<osg::Texture2D>> mTargets;
    };

    struct PostProcessor::RuntimePass
    {
        RuntimeTechnique* mOwner = nullptr;
        Fx::PassSpec mSpec;
        osg::ref_ptr<osg::Texture2D> mEffectInput;
        osg::ref_ptr<osg::Texture2D> mLastPassInput;
        osg::ref_ptr<osg::Texture2D> mOutput;
        const Fx::RenderTargetSpec* mOutputSpec = nullptr;
        osg::ref_ptr<osg::Camera> mCamera;
        osg::ref_ptr<osg::StateSet> mState;
        std::unordered_map<std::string, osg::ref_ptr<osg::Uniform>> mEffectUniforms;
        osg::ref_ptr<osg::Uniform> mResolutionUniform;
        osg::ref_ptr<osg::Uniform> mSimulationTimeUniform;
        osg::ref_ptr<osg::Uniform> mNearUniform;
        osg::ref_ptr<osg::Uniform> mFarUniform;
        osg::ref_ptr<osg::Uniform> mInvProjectionScaleUniform;
        osg::ref_ptr<osg::Uniform> mEyePosUniform;
        osg::ref_ptr<osg::Uniform> mViewRightUniform;
        osg::ref_ptr<osg::Uniform> mViewUpUniform;
        osg::ref_ptr<osg::Uniform> mViewForwardUniform;
    };

    PostProcessor::PostProcessor(osg::Camera* mainCamera, osg::Group* rootNode, Resource::ResourceSystem* resourceSystem)
        : mVfs(resourceSystem ? resourceSystem->getVFS() : nullptr)
        , mMainCamera(mainCamera)
        , mRootNode(rootNode)
    {
        if (!mainCamera || !rootNode || !mVfs)
        {
            Log(Debug::Error) << "OpenMW 0.52 PostProcessor R5 cannot initialize: missing camera/root/VFS";
            return;
        }

        mSceneTexture = createSceneTexture();
        mDepthTexture = createDepthTexture();
        mPingTextureA = createSceneTexture();
        mPingTextureB = createSceneTexture();
        mPingTextureC = createSceneTexture();
        mDistortionTexture = createDistortionTexture();
        mEyeAdaptTextureA = createEyeAdaptationTexture();
        mEyeAdaptTextureB = createEyeAdaptationTexture();

        // Render the world directly into textures. This is the important R5.1
        // correction: R3/R4 copied the framebuffer from a post-draw callback,
        // which runs too late relative to child POST_RENDER cameras on OSG 3.6.
        // The .omwfx passes therefore did not reliably see the current world frame.
        // Direct FBO ownership follows the OpenMW 0.52 renderer model and makes
        // live slider/uniform changes observable in the same frame.
        mainCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
        mainCamera->setImplicitBufferAttachmentMask(0, 0);
        mainCamera->attach(osg::Camera::COLOR_BUFFER0, mSceneTexture);
        mainCamera->attach(osg::Camera::DEPTH_BUFFER, mDepthTexture);

        sActiveProcessor.store(this, std::memory_order_release);

        discoverEffects();
        reloadSettings();
        mReady = true;

        if (osg::Viewport* viewport = mainCamera->getViewport())
            resizeTargets(std::max(1, static_cast<int>(viewport->width())),
                          std::max(1, static_cast<int>(viewport->height())));

        // Build the mandatory final presenter before the first rendered frame.
        // Even if every .omwfx effect fails or F3 starts disabled, the scene FBO
        // is still blitted to the window instead of producing a black screen.
        if (mChainDirty)
            rebuildRuntimeChain();

        Log(Debug::Info) << "OpenMW 0.52 PostProcessor R5.1 initialized: direct scene FBO + live FX runtime";
    }

    PostProcessor::~PostProcessor()
    {
        PostProcessor* expected = this;
        sActiveProcessor.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

        clearRuntimeChain();
        if (mMainCamera.valid())
            mMainCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER);
    }

    void PostProcessor::discoverEffects()
    {
        mAvailableEffects.clear();
        mInternalDistortionAvailable = false;
        if (!mVfs)
            return;

        for (const auto& entry : mVfs->getIndex())
        {
            const std::string& path = entry.first;
            if (!endsWith(path, ".omwfx"))
                continue;

            std::string name = path;
            const std::string prefix = "shaders/";
            if (name.compare(0, prefix.size(), prefix) == 0)
                name.erase(0, prefix.size());
            name.erase(name.size() - 6);

            try
            {
                auto stream = mVfs->get(path);
                std::ostringstream buffer;
                buffer << stream->rdbuf();
                Fx::Technique technique;
                std::string parseError;
                if (!technique.parse(buffer.str(), &parseError))
                {
                    Log(Debug::Warning) << "Post-processing effect '" << path
                                        << "' is not executable by R5: " << parseError;
                    continue;
                }

                // OpenMW 0.52 keeps hidden/internal techniques out of the user
                // effect list. internal_distortion is inserted automatically by
                // the runtime and must never appear as a user-selectable effect.
                if (name == "internal_distortion")
                    mInternalDistortionAvailable = true;
                if ((technique.flags() & Fx::Technique::Flag_Hidden) == 0)
                    mAvailableEffects.push_back(name);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Post-processing effect '" << path << "' could not be read: " << e.what();
            }
        }

        std::sort(mAvailableEffects.begin(), mAvailableEffects.end());
        mAvailableEffects.erase(std::unique(mAvailableEffects.begin(), mAvailableEffects.end()), mAvailableEffects.end());
        Log(Debug::Info) << "OpenMW 0.52 PostProcessor R5: discovered "
                         << mAvailableEffects.size() << " executable .omwfx effect(s)";
    }

    bool PostProcessor::effectExists(const std::string& name) const
    {
        return std::find(mAvailableEffects.begin(), mAvailableEffects.end(), name) != mAvailableEffects.end();
    }

    void PostProcessor::rebuildChainNames()
    {
        std::vector<std::string> next;
        std::stringstream stream(Settings::Manager::getString("chain", "Post Processing"));
        std::string effect;
        while (std::getline(stream, effect, ','))
        {
            effect = trim(effect);
            if (effect.empty())
                continue;
            if (effectExists(effect))
                next.push_back(effect);
            else
                Log(Debug::Warning) << "Post-processing chain references missing/unsupported effect '" << effect << "'";
        }

        if (next != mChain)
        {
            mChain = std::move(next);
            mChainDirty = true;
        }
    }

    void PostProcessor::reloadSettings()
    {
        const bool wasEnabled = mEnabled;
        mEnabled = Settings::Manager::getBool("enabled", "Post Processing");
        mTransparentPostpass = Settings::Manager::getBool("transparent postpass", "Post Processing");
        mAutoExposureSpeed = std::max(0.f, Settings::Manager::getFloat("auto exposure speed", "Post Processing"));
        rebuildChainNames();

        (void)wasEnabled;
        updateFinalSource();
        applyPassVisibility();
    }

    void PostProcessor::processChangedSettings(const Settings::CategorySettingVector& settings)
    {
        bool reloadCore = false;
        bool reloadUniforms = false;
        for (const auto& setting : settings)
        {
            if (setting.first == "Post Processing")
                reloadCore = true;
            else if (setting.first.compare(0, 16, "Post Processing ") == 0)
                reloadUniforms = true;
        }

        if (reloadCore)
            reloadSettings();
        if (reloadUniforms)
        {
            updateEffectUniforms();
            Log(Debug::Info) << "Post-processing live uniforms updated from settings";
        }
    }

    PostProcessor* PostProcessor::getActiveProcessor()
    {
        return sActiveProcessor.load(std::memory_order_acquire);
    }

    std::shared_ptr<Fx::Technique> PostProcessor::getTechniqueInfo(const std::string& name)
    {
        if (name == "internal_distortion" || effectExists(name))
            return loadTechnique(name);
        return nullptr;
    }

    std::shared_ptr<Fx::Technique> PostProcessor::loadTechnique(const std::string& name)
    {
        if (!mVfs)
            return nullptr;

        const std::string path = std::string("shaders/") + name + ".omwfx";
        try
        {
            auto stream = mVfs->get(path);
            std::ostringstream buffer;
            buffer << stream->rdbuf();

            std::shared_ptr<Fx::Technique> technique = std::make_shared<Fx::Technique>();
            std::string parseError;
            if (!technique->parse(buffer.str(), &parseError))
            {
                Log(Debug::Error) << "Failed to parse .omwfx '" << path << "': " << parseError;
                return nullptr;
            }
            return technique;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load .omwfx '" << path << "': " << e.what();
            return nullptr;
        }
    }

    void PostProcessor::clearRuntimeChain()
    {
        clearDistortionResources();
        clearEyeAdaptationPasses();
        if (mRootNode.valid())
        {
            for (const osg::ref_ptr<osg::Camera>& camera : mPassCameras)
            {
                if (camera)
                    mRootNode->removeChild(camera);
            }
            if (mFinalCamera)
                mRootNode->removeChild(mFinalCamera);
        }

        mPassCameras.clear();
        mRuntimePasses.clear();
        mRuntimeTechniques.clear();
        mLoadedTechniques.clear();
        mFinalCamera = nullptr;
        mFinalState = nullptr;
        mFinalSampler = nullptr;
        mProcessedOutput = nullptr;
    }

    void PostProcessor::rebuildRuntimeChain()
    {
        clearRuntimeChain();
        mChainDirty = false;

        if (!mReady || !mRootNode.valid())
            return;

        // The donor renderer always dispatches internal_distortion before the
        // user chain. R5 mirrors that ordering while the actual distortion
        // geometry render-bin is being migrated separately. The distortion
        // target is currently cleared to neutral each frame, so this stage is
        // a strict no-op until refraction geometry starts writing to it.
        std::vector<std::string> executionChain;
        if (mInternalDistortionAvailable)
            executionChain.push_back("internal_distortion");
        executionChain.insert(executionChain.end(), mChain.begin(), mChain.end());

        int resourceOrder = -1300;
        createDistortionResources(resourceOrder);
        int eyeOrder = -1200;
        createEyeAdaptationPasses(eyeOrder);

        osg::ref_ptr<osg::Texture2D> previousEffectOutput = mSceneTexture;
        std::size_t pingCursor = 0;
        int order = -1000;

        for (const std::string& effectName : executionChain)
        {
            std::shared_ptr<Fx::Technique> technique = loadTechnique(effectName);
            if (!technique)
                continue;

            mLoadedTechniques.push_back(technique);
            std::unique_ptr<RuntimeTechnique> runtimeTechnique(new RuntimeTechnique);
            runtimeTechnique->mName = effectName;
            runtimeTechnique->mTechnique = technique;

            // Context flags are compile-time dispatch decisions in OpenMW 0.52.
            // Rebuild the lightweight runtime chain when the player crosses an
            // interior/water boundary so disabled techniques are bypassed rather
            // than leaving stale render-target contents in the chain.
            if (!techniqueVisible(*runtimeTechnique))
                continue;

            for (const Fx::RenderTargetSpec& spec : technique->renderTargets())
                runtimeTechnique->mTargets[spec.mName] = createTargetTexture(spec);

            RuntimeTechnique* runtimeOwner = runtimeTechnique.get();
            mRuntimeTechniques.push_back(std::move(runtimeTechnique));

            osg::ref_ptr<osg::Texture2D> effectInput = previousEffectOutput;
            osg::ref_ptr<osg::Texture2D> lastPassOutput = effectInput;

            for (const Fx::PassSpec& spec : technique->passes())
            {
                std::unique_ptr<RuntimePass> pass(new RuntimePass);
                pass->mOwner = runtimeOwner;
                pass->mSpec = spec;
                pass->mEffectInput = effectInput;
                pass->mLastPassInput = lastPassOutput;

                if (!spec.mTarget.empty())
                {
                    const auto targetIt = runtimeOwner->mTargets.find(spec.mTarget);
                    if (targetIt == runtimeOwner->mTargets.end())
                    {
                        Log(Debug::Error) << "Effect '" << effectName << "' pass '" << spec.mName
                                          << "' has no runtime target '" << spec.mTarget << "'";
                        continue;
                    }
                    pass->mOutput = targetIt->second;
                    pass->mOutputSpec = technique->findRenderTarget(spec.mTarget);
                }
                else
                {
                    // OpenMW 0.52 uses three ping targets so a pass can safely read
                    // both LastShader and LastPass while writing a third texture.
                    const osg::ref_ptr<osg::Texture2D> candidates[3]
                        = { mPingTextureA, mPingTextureB, mPingTextureC };
                    for (std::size_t attempt = 0; attempt < 3; ++attempt)
                    {
                        const osg::ref_ptr<osg::Texture2D>& candidate = candidates[(pingCursor + attempt) % 3];
                        if (candidate.get() != effectInput.get() && candidate.get() != lastPassOutput.get())
                        {
                            pass->mOutput = candidate;
                            pingCursor = (pingCursor + attempt + 1) % 3;
                            break;
                        }
                    }
                    if (!pass->mOutput)
                        pass->mOutput = candidates[pingCursor++ % 3];
                }

                std::vector<std::string> sampledTargets;
                for (const std::string& input : spec.mInputs)
                {
                    if (!input.empty())
                        sampledTargets.push_back(input);
                }

                const std::string fragmentSource
                    = makeFragmentSource(*technique, spec.mSource, sampledTargets);
                osg::ref_ptr<osg::Program> program
                    = createProgram(effectName + ":" + spec.mName, fragmentSource);
                if (!program)
                {
                    Log(Debug::Error) << "Could not create program for effect '" << effectName
                                      << "' pass '" << spec.mName << "'";
                    continue;
                }

                pass->mCamera = createCamera(order++, true);
                pass->mCamera->attach(osg::Camera::COLOR_BUFFER0, pass->mOutput);
                osg::ref_ptr<osg::Geode> geode = createFullscreenPass(program);
                pass->mState = geode->getOrCreateStateSet();

                pass->mState->setTextureAttributeAndModes(0, effectInput, osg::StateAttribute::ON);
                pass->mState->setTextureAttributeAndModes(1, lastPassOutput, osg::StateAttribute::ON);
                pass->mState->setTextureAttributeAndModes(2, mDepthTexture, osg::StateAttribute::ON);
                pass->mState->setTextureAttributeAndModes(3, mEyeAdaptTextureA, osg::StateAttribute::ON);
                pass->mState->setTextureAttributeAndModes(4, mDistortionTexture, osg::StateAttribute::ON);
                pass->mState->addUniform(new osg::Uniform("omw_LastShader", 0));
                pass->mState->addUniform(new osg::Uniform("omw_LastPass", 1));
                pass->mState->addUniform(new osg::Uniform("omw_Depth", 2));
                pass->mState->addUniform(new osg::Uniform("omw_EyeAdaptation", 3));
                pass->mState->addUniform(new osg::Uniform("omw_Distortion", 4));

                int unit = 5;
                for (const std::string& input : spec.mInputs)
                {
                    if (input.empty())
                        continue;
                    const auto inputIt = runtimeOwner->mTargets.find(input);
                    if (inputIt == runtimeOwner->mTargets.end())
                        continue;
                    pass->mState->setTextureAttributeAndModes(unit, inputIt->second, osg::StateAttribute::ON);
                    pass->mState->addUniform(new osg::Uniform(input.c_str(), unit));
                    ++unit;
                }

                for (const Fx::UniformSpec& uniform : technique->uniforms())
                {
                    osg::ref_ptr<osg::Uniform> osgUniform
                        = makeEffectUniform(uniform, configuredUniformValue(effectName, uniform));
                    if (osgUniform)
                    {
                        pass->mState->addUniform(osgUniform);
                        pass->mEffectUniforms[uniform.mName] = osgUniform;
                    }
                }

                pass->mResolutionUniform = new osg::Uniform("omw.resolution", osg::Vec2f(1.f, 1.f));
                pass->mSimulationTimeUniform = new osg::Uniform("omw.simulationTime", 0.f);
                pass->mNearUniform = new osg::Uniform("omw.near", mNear);
                pass->mFarUniform = new osg::Uniform("omw.far", mFar);
                pass->mInvProjectionScaleUniform
                    = new osg::Uniform("omw.invProjectionScale", osg::Vec2f(1.f, 1.f));
                pass->mEyePosUniform = new osg::Uniform("omw.eyePos", osg::Vec3f(0.f, 0.f, 0.f));
                pass->mViewRightUniform = new osg::Uniform("omw.viewRight", osg::Vec3f(1.f, 0.f, 0.f));
                pass->mViewUpUniform = new osg::Uniform("omw.viewUp", osg::Vec3f(0.f, 1.f, 0.f));
                pass->mViewForwardUniform = new osg::Uniform("omw.viewForward", osg::Vec3f(0.f, 0.f, 1.f));
                pass->mState->addUniform(pass->mResolutionUniform);
                pass->mState->addUniform(pass->mSimulationTimeUniform);
                pass->mState->addUniform(pass->mNearUniform);
                pass->mState->addUniform(pass->mFarUniform);
                pass->mState->addUniform(pass->mInvProjectionScaleUniform);
                pass->mState->addUniform(pass->mEyePosUniform);
                pass->mState->addUniform(pass->mViewRightUniform);
                pass->mState->addUniform(pass->mViewUpUniform);
                pass->mState->addUniform(pass->mViewForwardUniform);

                pass->mCamera->addChild(geode);
                pass->mCamera->setNodeMask(0u);
                mRootNode->addChild(pass->mCamera);
                mPassCameras.push_back(pass->mCamera);

                lastPassOutput = pass->mOutput;
                mRuntimePasses.push_back(std::move(pass));
            }

            previousEffectOutput = lastPassOutput;
        }

        // Always present the off-screen scene. F3 disables only the effect passes;
        // it must never hide the final blit now that the main camera owns an FBO.
        mProcessedOutput = mRuntimePasses.empty() ? mSceneTexture : previousEffectOutput;
        mFinalCamera = createCamera(order++, false);
        osg::ref_ptr<osg::Program> blitProgram = createBlitProgram();
        osg::ref_ptr<osg::Geode> finalGeode = createFullscreenPass(blitProgram);
        mFinalState = finalGeode->getOrCreateStateSet();
        mFinalSampler = new osg::Uniform("sourceTexture", 0);
        mFinalState->addUniform(mFinalSampler);
        mFinalCamera->addChild(finalGeode);
        mRootNode->addChild(mFinalCamera);
        updateFinalSource();

        if (mWidth > 0 && mHeight > 0)
            resizeTargets(mWidth, mHeight);
        updateDynamicUniforms();
        applyPassVisibility();

        Log(Debug::Info) << "OpenMW 0.52 PostProcessor R5.1: runtime chain rebuilt, effects="
                         << mRuntimeTechniques.size() << ", passes=" << mRuntimePasses.size();
    }

    osg::ref_ptr<osg::Texture2D> PostProcessor::createSceneTexture() const
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
#if defined(__ANDROID__)
        texture->setInternalFormat(GL_RGBA);
        texture->setSourceType(GL_UNSIGNED_BYTE);
#else
#if defined(GL_RGBA16F)
        texture->setInternalFormat(GL_RGBA16F);
        texture->setSourceType(GL_FLOAT);
#elif defined(GL_RGBA16F_ARB)
        texture->setInternalFormat(GL_RGBA16F_ARB);
        texture->setSourceType(GL_FLOAT);
#else
        texture->setInternalFormat(GL_RGBA);
        texture->setSourceType(GL_UNSIGNED_BYTE);
#endif
#endif
        texture->setSourceFormat(GL_RGBA);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Texture2D> PostProcessor::createDepthTexture()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setInternalFormat(GL_DEPTH_COMPONENT24);
        texture->setSourceFormat(GL_DEPTH_COMPONENT);
        texture->setSourceType(GL_FLOAT);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Texture2D> PostProcessor::createDistortionTexture()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setInternalFormat(GL_RGBA);
        texture->setSourceFormat(GL_RGBA);
        texture->setSourceType(GL_UNSIGNED_BYTE);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Texture2D> PostProcessor::createEyeAdaptationTexture()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setTextureSize(1, 1);
#if defined(__ANDROID__)
        texture->setInternalFormat(GL_LUMINANCE);
        texture->setSourceFormat(GL_LUMINANCE);
        texture->setSourceType(GL_UNSIGNED_BYTE);
#else
#if defined(GL_R16F) && defined(GL_RED)
        texture->setInternalFormat(GL_R16F);
        texture->setSourceFormat(GL_RED);
        texture->setSourceType(GL_FLOAT);
#elif defined(GL_LUMINANCE16F_ARB)
        texture->setInternalFormat(GL_LUMINANCE16F_ARB);
        texture->setSourceFormat(GL_LUMINANCE);
        texture->setSourceType(GL_FLOAT);
#else
        texture->setInternalFormat(GL_LUMINANCE);
        texture->setSourceFormat(GL_LUMINANCE);
        texture->setSourceType(GL_UNSIGNED_BYTE);
#endif
#endif
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    void PostProcessor::clearDistortionResources()
    {
        if (mRootNode.valid() && mDistortionClearCamera)
            mRootNode->removeChild(mDistortionClearCamera);
        mDistortionClearCamera = nullptr;
    }

    void PostProcessor::createDistortionResources(int& order)
    {
        clearDistortionResources();
        if (!mRootNode.valid() || !mDistortionTexture)
            return;

        // OpenMW 0.52 uses a quarter-resolution RGB distortion target. Keep an
        // RGBA8 target here for GL2/NG-GL4ES compatibility; the extra channel is
        // harmless and avoids format-specific FBO failures on older drivers.
        mDistortionClearCamera = createCamera(order++, true);
        mDistortionClearCamera->attach(osg::Camera::COLOR_BUFFER0, mDistortionTexture);
        mDistortionClearCamera->setClearMask(GL_COLOR_BUFFER_BIT);
        mDistortionClearCamera->setClearColor(osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        mDistortionClearCamera->setNodeMask(0u);
        mRootNode->addChild(mDistortionClearCamera);
    }

    void PostProcessor::clearEyeAdaptationPasses()
    {
        if (mRootNode.valid())
        {
            if (mEyeAdaptCamera)
                mRootNode->removeChild(mEyeAdaptCamera);
            if (mEyeAdaptCopyCamera)
                mRootNode->removeChild(mEyeAdaptCopyCamera);
        }
        mEyeAdaptCamera = nullptr;
        mEyeAdaptCopyCamera = nullptr;
        mEyeAdaptState = nullptr;
        mEyeAdaptDeltaUniform = nullptr;
        mEyeAdaptSpeedUniform = nullptr;
    }

    void PostProcessor::createEyeAdaptationPasses(int& order)
    {
        clearEyeAdaptationPasses();
        if (!mRootNode.valid() || !mEyeAdaptTextureA || !mEyeAdaptTextureB)
            return;

        mEyeAdaptCamera = createCamera(order++, true);
        mEyeAdaptCamera->setViewport(0, 0, 1, 1);
        mEyeAdaptCamera->attach(osg::Camera::COLOR_BUFFER0, mEyeAdaptTextureA);
        osg::ref_ptr<osg::Program> program = createProgram("OpenMW52_EyeAdaptation", kEyeAdaptFragment);
        osg::ref_ptr<osg::Geode> geode = createFullscreenPass(program);
        mEyeAdaptState = geode->getOrCreateStateSet();
        mEyeAdaptState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mEyeAdaptState->setTextureAttributeAndModes(1, mEyeAdaptTextureB, osg::StateAttribute::ON);
        mEyeAdaptState->addUniform(new osg::Uniform("sceneTexture", 0));
        mEyeAdaptState->addUniform(new osg::Uniform("previousExposure", 1));
        mEyeAdaptDeltaUniform = new osg::Uniform("deltaTime", 0.f);
        mEyeAdaptSpeedUniform = new osg::Uniform("adaptationSpeed", mAutoExposureSpeed);
        mEyeAdaptState->addUniform(mEyeAdaptDeltaUniform);
        mEyeAdaptState->addUniform(mEyeAdaptSpeedUniform);
        mEyeAdaptCamera->addChild(geode);
        mEyeAdaptCamera->setNodeMask(0u);
        mRootNode->addChild(mEyeAdaptCamera);

        mEyeAdaptCopyCamera = createCamera(order++, true);
        mEyeAdaptCopyCamera->setViewport(0, 0, 1, 1);
        mEyeAdaptCopyCamera->attach(osg::Camera::COLOR_BUFFER0, mEyeAdaptTextureB);
        osg::ref_ptr<osg::Geode> copyGeode = createFullscreenPass(createBlitProgram());
        osg::StateSet* copyState = copyGeode->getOrCreateStateSet();
        copyState->setTextureAttributeAndModes(0, mEyeAdaptTextureA, osg::StateAttribute::ON);
        copyState->addUniform(new osg::Uniform("sourceTexture", 0));
        mEyeAdaptCopyCamera->addChild(copyGeode);
        mEyeAdaptCopyCamera->setNodeMask(0u);
        mRootNode->addChild(mEyeAdaptCopyCamera);
    }

    osg::ref_ptr<osg::Texture2D> PostProcessor::createTargetTexture(const Fx::RenderTargetSpec& spec)
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setInternalFormat(getInternalFormat(spec.mInternalFormat));
        texture->setSourceFormat(getSourceFormat(spec.mSourceFormat));
        texture->setSourceType(getSourceType(spec.mSourceType));
        texture->setFilter(osg::Texture::MIN_FILTER, getFilter(spec.mMinFilter));
        texture->setFilter(osg::Texture::MAG_FILTER, getFilter(spec.mMagFilter));
        texture->setWrap(osg::Texture::WRAP_S, getWrap(spec.mWrapS));
        texture->setWrap(osg::Texture::WRAP_T, getWrap(spec.mWrapT));
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Camera> PostProcessor::createCamera(int orderNum, bool renderToTexture)
    {
        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        camera->setProjectionResizePolicy(osg::Camera::FIXED);
        camera->setProjectionMatrix(osg::Matrix::identity());
        camera->setViewMatrix(osg::Matrix::identity());
        camera->setRenderOrder(osg::Camera::POST_RENDER, orderNum);
        camera->setRenderTargetImplementation(
            renderToTexture ? osg::Camera::FRAME_BUFFER_OBJECT : osg::Camera::FRAME_BUFFER);
        camera->setClearMask(renderToTexture ? GL_COLOR_BUFFER_BIT : 0);
        camera->setClearColor(osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        camera->setImplicitBufferAttachmentMask(0, 0);
        camera->setAllowEventFocus(false);
        camera->setCullingActive(false);
        return camera;
    }

    osg::ref_ptr<osg::Geode> PostProcessor::createFullscreenPass(osg::Program* program)
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

    osg::ref_ptr<osg::Program> PostProcessor::createProgram(
        const std::string& name, const std::string& fragmentSource)
    {
        osg::ref_ptr<osg::Shader> vertex = new osg::Shader(osg::Shader::VERTEX);
        vertex->setName(name + ":vertex");
        vertex->setShaderSource(kFullscreenVertex);

        osg::ref_ptr<osg::Shader> fragment = new osg::Shader(osg::Shader::FRAGMENT);
        fragment->setName(name + ":fragment");
        fragment->setShaderSource(fragmentSource);

        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->setName(name);
        program->addShader(vertex);
        program->addShader(fragment);
        return program;
    }

    osg::ref_ptr<osg::Program> PostProcessor::createBlitProgram()
    {
        return createProgram("OpenMW52_PostProcess_FinalBlit", kBlitFragment);
    }

    std::string PostProcessor::makeFragmentSource(const Fx::Technique& technique, const std::string& passSource,
        const std::vector<std::string>& sampledTargets)
    {
        std::ostringstream source;
        source << "#version 120\n";
        source << "#define OMW_NORMALS " << (technique.wantsNormals() ? 1 : 0) << "\n";
        source << "#define OMW_HDR " << (technique.wantsHDR() ? 1 : 0) << "\n";
        source << "#define omw_In varying\n";
        source << "#define omw_FragColor gl_FragColor\n";
        source << "#define omw_Texture2D texture2D\n";
        source << "uniform sampler2D omw_LastShader;\n";
        source << "uniform sampler2D omw_LastPass;\n";
        source << "uniform sampler2D omw_Depth;\n";
        source << "uniform sampler2D omw_EyeAdaptation;\n";
        source << "uniform sampler2D omw_Distortion;\n";
        for (const std::string& target : sampledTargets)
            source << "uniform sampler2D " << target << ";\n";
        for (const Fx::UniformSpec& uniform : technique.uniforms())
            source << uniform.glslDeclaration() << "\n";
        source << "struct OMW_State { vec2 resolution; float simulationTime; float near; float far; "
                  "vec2 invProjectionScale; vec3 eyePos; vec3 viewRight; vec3 viewUp; vec3 viewForward; };\n";
        source << "uniform OMW_State omw;\n";
        source << "vec4 omw_GetLastShader(vec2 uv) { return texture2D(omw_LastShader, uv); }\n";
        source << "vec4 omw_GetLastPass(vec2 uv) { return texture2D(omw_LastPass, uv); }\n";
        source << "float omw_GetDepth(vec2 uv) { return texture2D(omw_Depth, uv).r; }\n";
        source << "float omw_GetLinearDepth(vec2 uv) { float d=omw_GetDepth(uv)*2.0-1.0; "
                  "return (2.0*omw.near*omw.far)/max(0.0001,omw.far+omw.near-d*(omw.far-omw.near)); }\n";
        source << "vec3 omw_GetViewPosFromUV(vec2 uv) { float dist=omw_GetLinearDepth(uv); "
                  "vec2 ndc=uv*2.0-1.0; float z=-dist; return vec3(ndc.x*dist*omw.invProjectionScale.x, "
                  "ndc.y*dist*omw.invProjectionScale.y, z); }\n";
        source << "vec3 omw_GetWorldPosFromUV(vec2 uv) { vec3 p=omw_GetViewPosFromUV(uv); "
                  "return omw.eyePos + omw.viewRight*p.x + omw.viewUp*p.y + omw.viewForward*p.z; }\n";
        source << "vec3 omw_GetNormals(vec2 uv) {\n"
                  "#if OMW_NORMALS\n"
                  " vec2 t=1.0/max(omw.resolution,vec2(1.0)); vec3 c=omw_GetViewPosFromUV(uv); "
                  "vec3 xr=omw_GetViewPosFromUV(clamp(uv+vec2(t.x,0.0),vec2(0.0),vec2(1.0))); "
                  "vec3 xl=omw_GetViewPosFromUV(clamp(uv-vec2(t.x,0.0),vec2(0.0),vec2(1.0))); "
                  "vec3 yu=omw_GetViewPosFromUV(clamp(uv+vec2(0.0,t.y),vec2(0.0),vec2(1.0))); "
                  "vec3 yd=omw_GetViewPosFromUV(clamp(uv-vec2(0.0,t.y),vec2(0.0),vec2(1.0))); "
                  "vec3 dx=(abs(xr.z-c.z)<abs(c.z-xl.z))?(xr-c):(c-xl); "
                  "vec3 dy=(abs(yu.z-c.z)<abs(c.z-yd.z))?(yu-c):(c-yd); "
                  "return normalize(cross(dx,dy));\n"
                  "#else\n return vec3(0.0,0.0,1.0);\n#endif\n}\n";
        source << "vec3 omw_GetNormalsWorldSpace(vec2 uv) { vec3 n=omw_GetNormals(uv); "
                  "return normalize(omw.viewRight*n.x + omw.viewUp*n.y + omw.viewForward*n.z); }\n";
        source << "vec4 omw_GetDistortion(vec2 uv) { return texture2D(omw_Distortion, uv); }\n";
        source << "float omw_GetEyeAdaptation() {\n#if OMW_HDR\n return texture2D(omw_EyeAdaptation,vec2(0.5)).r;"
                  "\n#else\n return 1.0;\n#endif\n}\n";
        source << "\n" << technique.sharedSource() << "\n";
        source << "#line 1\n" << passSource << "\n";
        return source.str();
    }

    void PostProcessor::resizeTargets(int width, int height)
    {
        mWidth = std::max(1, width);
        mHeight = std::max(1, height);

        mSceneTexture->setTextureSize(mWidth, mHeight);
        mDepthTexture->setTextureSize(mWidth, mHeight);
        mPingTextureA->setTextureSize(mWidth, mHeight);
        mPingTextureB->setTextureSize(mWidth, mHeight);
        mPingTextureC->setTextureSize(mWidth, mHeight);
        if (mDistortionTexture)
            mDistortionTexture->setTextureSize(std::max(1, mWidth / 4), std::max(1, mHeight / 4));
        if (mDistortionClearCamera)
            mDistortionClearCamera->setViewport(0, 0, std::max(1, mWidth / 4), std::max(1, mHeight / 4));

        for (const std::unique_ptr<RuntimeTechnique>& runtime : mRuntimeTechniques)
        {
            for (const Fx::RenderTargetSpec& spec : runtime->mTechnique->renderTargets())
            {
                const auto targetIt = runtime->mTargets.find(spec.mName);
                if (targetIt == runtime->mTargets.end())
                    continue;
                const std::pair<int, int> size = spec.sizeFor(mWidth, mHeight);
                targetIt->second->setTextureSize(size.first, size.second);
                if (spec.mMipMaps)
                    targetIt->second->setNumMipmapLevels(osg::Image::computeNumberOfMipmapLevels(size.first, size.second));
            }
        }

        for (const std::unique_ptr<RuntimePass>& pass : mRuntimePasses)
        {
            int passWidth = mWidth;
            int passHeight = mHeight;
            if (pass->mOutputSpec)
            {
                const std::pair<int, int> size = pass->mOutputSpec->sizeFor(mWidth, mHeight);
                passWidth = size.first;
                passHeight = size.second;
            }
            if (pass->mCamera)
                pass->mCamera->setViewport(0, 0, passWidth, passHeight);
        }

        if (mFinalCamera)
            mFinalCamera->setViewport(0, 0, mWidth, mHeight);

        updateDynamicUniforms();
    }

    void PostProcessor::updateDynamicUniforms()
    {
        const osg::Vec2f resolution(
            static_cast<float>(std::max(1, mWidth)), static_cast<float>(std::max(1, mHeight)));

        osg::Vec3f eyePos(0.f, 0.f, 0.f);
        osg::Vec3f viewRight(1.f, 0.f, 0.f);
        osg::Vec3f viewUp(0.f, 1.f, 0.f);
        osg::Vec3f viewForward(0.f, 0.f, 1.f);
        if (mMainCamera.valid())
        {
            const osg::Matrixd invView = mMainCamera->getInverseViewMatrix();
            const osg::Vec3d eye = invView.getTrans();
            eyePos.set(static_cast<float>(eye.x()), static_cast<float>(eye.y()), static_cast<float>(eye.z()));
            viewRight.set(static_cast<float>(invView(0, 0)), static_cast<float>(invView(0, 1)),
                static_cast<float>(invView(0, 2)));
            viewUp.set(static_cast<float>(invView(1, 0)), static_cast<float>(invView(1, 1)),
                static_cast<float>(invView(1, 2)));
            viewForward.set(static_cast<float>(invView(2, 0)), static_cast<float>(invView(2, 1)),
                static_cast<float>(invView(2, 2)));
        }

        const osg::Vec2f invProjectionScale(mInvProjectionScaleX, mInvProjectionScaleY);
        for (const std::unique_ptr<RuntimePass>& pass : mRuntimePasses)
        {
            if (pass->mResolutionUniform) pass->mResolutionUniform->set(resolution);
            if (pass->mSimulationTimeUniform) pass->mSimulationTimeUniform->set(mSimulationTime);
            if (pass->mNearUniform) pass->mNearUniform->set(mNear);
            if (pass->mFarUniform) pass->mFarUniform->set(mFar);
            if (pass->mInvProjectionScaleUniform) pass->mInvProjectionScaleUniform->set(invProjectionScale);
            if (pass->mEyePosUniform) pass->mEyePosUniform->set(eyePos);
            if (pass->mViewRightUniform) pass->mViewRightUniform->set(viewRight);
            if (pass->mViewUpUniform) pass->mViewUpUniform->set(viewUp);
            if (pass->mViewForwardUniform) pass->mViewForwardUniform->set(viewForward);
        }

        if (mEyeAdaptDeltaUniform)
            mEyeAdaptDeltaUniform->set(mDeltaTime);
        if (mEyeAdaptSpeedUniform)
            mEyeAdaptSpeedUniform->set(mAutoExposureSpeed);
    }

    void PostProcessor::updateEffectUniforms()
    {
        for (const std::unique_ptr<RuntimePass>& pass : mRuntimePasses)
        {
            if (!pass || !pass->mOwner || !pass->mOwner->mTechnique)
                continue;
            const std::string& effectName = pass->mOwner->mName;
            for (const Fx::UniformSpec& spec : pass->mOwner->mTechnique->uniforms())
            {
                const auto uniformIt = pass->mEffectUniforms.find(spec.mName);
                if (uniformIt == pass->mEffectUniforms.end() || !uniformIt->second)
                    continue;
                setEffectUniformValue(*uniformIt->second, spec, configuredUniformValue(effectName, spec));
            }
        }
    }

    bool PostProcessor::techniqueVisible(const RuntimeTechnique& technique) const
    {
        if (!technique.mTechnique)
            return false;
        const Fx::FlagsType flags = technique.mTechnique->flags();
        if (mInterior && (flags & Fx::Technique::Flag_Disable_Interiors))
            return false;
        if (!mInterior && (flags & Fx::Technique::Flag_Disable_Exteriors))
            return false;
        if (mUnderwater && (flags & Fx::Technique::Flag_Disable_Underwater))
            return false;
        if (!mUnderwater && (flags & Fx::Technique::Flag_Disable_Abovewater))
            return false;
        return true;
    }

    void PostProcessor::applyPassVisibility()
    {
        const bool effectsVisible = mReady && mEnabled && !mRuntimePasses.empty();
        const unsigned int mask = Mask_RenderToTexture;
        if (mDistortionClearCamera)
            mDistortionClearCamera->setNodeMask(effectsVisible ? mask : 0u);
        if (mEyeAdaptCamera)
            mEyeAdaptCamera->setNodeMask(effectsVisible ? mask : 0u);
        if (mEyeAdaptCopyCamera)
            mEyeAdaptCopyCamera->setNodeMask(effectsVisible ? mask : 0u);
        for (const osg::ref_ptr<osg::Camera>& camera : mPassCameras)
        {
            if (camera)
                camera->setNodeMask(effectsVisible ? mask : 0u);
        }
        // The final presenter is mandatory even with post-processing disabled,
        // because the main world camera renders to mSceneTexture in R5.1.
        if (mFinalCamera)
            mFinalCamera->setNodeMask(mReady ? mask : 0u);
        updateFinalSource();
    }

    void PostProcessor::setSceneContext(bool interior, bool underwater)
    {
        if (mInterior == interior && mUnderwater == underwater)
            return;
        mInterior = interior;
        mUnderwater = underwater;
        mChainDirty = true;
    }

    void PostProcessor::updateFinalSource()
    {
        if (!mFinalState)
            return;
        osg::Texture2D* source = (mEnabled && !mRuntimePasses.empty() && mProcessedOutput)
            ? mProcessedOutput.get() : mSceneTexture.get();
        if (source)
            mFinalState->setTextureAttributeAndModes(0, source, osg::StateAttribute::ON);
    }

    void PostProcessor::captureWorldDepth(osg::RenderInfo& renderInfo)
    {
        // R5.1 attaches mDepthTexture directly to the main scene FBO. Copying a
        // texture from the framebuffer into itself is undefined, so the old R3/R4
        // copy bridge is deliberately retired. The attachment remains directly
        // sampleable by the post-processing passes. A dedicated pre-first-person
        // world-depth attachment will replace this compatibility marker later.
        (void)renderInfo;
        mWorldDepthCapturedBeforeFirstPerson.store(true, std::memory_order_release);
    }

    void PostProcessor::captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo)
    {
        PostProcessor* processor = sActiveProcessor.load(std::memory_order_acquire);
        if (processor)
            processor->captureWorldDepth(renderInfo);
    }

    void PostProcessor::update(float dt)
    {
        if (!mReady)
            return;

        mDeltaTime = std::max(0.f, dt);
        mSimulationTime += mDeltaTime;

        if (mChainDirty)
            rebuildRuntimeChain();

        if (mMainCamera.valid())
        {
            osg::Viewport* viewport = mMainCamera->getViewport();
            if (viewport)
            {
                const int width = std::max(1, static_cast<int>(viewport->width()));
                const int height = std::max(1, static_cast<int>(viewport->height()));
                if (width != mWidth || height != mHeight)
                    resizeTargets(width, height);
            }

            double fovy = 0.0;
            double aspect = 0.0;
            double zNear = mNear;
            double zFar = mFar;
            if (mMainCamera->getProjectionMatrixAsPerspective(fovy, aspect, zNear, zFar))
            {
                mNear = static_cast<float>(std::max(0.001, zNear));
                mFar = static_cast<float>(std::max(zNear + 0.001, zFar));
            }

            const osg::Matrixd& projection = mMainCamera->getProjectionMatrix();
            const double xScale = projection(0, 0);
            const double yScale = projection(1, 1);
            mInvProjectionScaleX = static_cast<float>(std::abs(xScale) > 1e-8 ? 1.0 / xScale : 1.0);
            mInvProjectionScaleY = static_cast<float>(std::abs(yScale) > 1e-8 ? 1.0 / yScale : 1.0);
        }

        updateDynamicUniforms();
        applyPassVisibility();
    }
}
