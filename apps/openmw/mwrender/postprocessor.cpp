#include "postprocessor.hpp"

#include <components/debug/debuglog.hpp>
#include <components/fx/technique.hpp>
#include <components/misc/stringops.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>

#include "vismask.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>
#include <utility>

#include <osg/BlendFunc>
#include <osg/ColorMask>
#include <osg/Depth>
#include <osg/FrameBufferObject>
#include <osg/FrameStamp>
#include <osg/GLExtensions>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Material>
#include <osg/Matrix>
#include <osg/Program>
#include <osg/RenderInfo>
#include <osg/Shader>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture1D>
#include <osg/Texture2D>
#include <osg/Texture3D>
#include <osg/Uniform>
#include <osg/Vec2>
#include <osg/Vec3>
#include <osg/Vec4>
#include <osg/Viewport>

#include <osg/NodeCallback>

#include <osgUtil/CullVisitor>
#include <osgUtil/RenderBin>
#include <osgUtil/RenderLeaf>
#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

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

    // Logs, once per pass, an OpenGL error raised while the pass's state was applied (program, uniforms, textures,
    // target) or while it drew. OSG alone only reports "after RenderBin::draw(..)", which names neither the pass
    // nor whether post-processing caused it at all.
    class GLErrorProbe : public osg::Drawable::DrawCallback
    {
    public:
        explicit GLErrorProbe(const std::string& name) : mName(name) {}

        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const override
        {
            const GLenum before = takeError();
            drawable->drawImplementation(renderInfo);
            const GLenum after = takeError();
            if ((before != GL_NO_ERROR || after != GL_NO_ERROR) && !mReported.exchange(true))
            {
                std::ostringstream code;
                code << std::hex << (before != GL_NO_ERROR ? before : after);
                Log(Debug::Warning) << "Post-processing pass '" << mName << "': OpenGL error 0x" << code.str()
                                    << (before != GL_NO_ERROR ? " before its draw (program, uniforms, textures or target)"
                                                              : " in its draw")
                                    << " (reported once)";
            }
        }

    private:
        static GLenum takeError()
        {
            const GLenum first = glGetError();
            for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {}   // clear any further flags
            return first;
        }

        std::string mName;
        mutable std::atomic<bool> mReported{false};
    };

    // Points the main render stage at the world framebuffer every frame, as OpenMW 0.51's PingPongCull does. R5.1
    // instead switched the main camera to render-to-texture once, after the game had already drawn frames with it;
    // that never took effect (Ricky's log: the world texture stayed 0 0 0, the world went to the window, and the
    // final blit then covered it with that empty texture).
    class SceneFboCullCallback : public osg::NodeCallback
    {
    public:
        void operator()(osg::Node* node, osg::NodeVisitor* nv) override
        {
            if (nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR)
            {
                MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
                if (osgUtil::RenderStage* stage = static_cast<osgUtil::CullVisitor*>(nv)->getCurrentRenderStage())
                    stage->setFrameBufferObject(processor ? processor->sceneFbo() : nullptr);
            }
            traverse(node, nv);
        }
    };

    // First thing in every effect pass: make sure this frame's depth copy exists (PostProcessor::ensureWorldDepth)
    class WorldDepthCallback : public osg::Camera::DrawCallback
    {
    public:
        void operator()(osg::RenderInfo& renderInfo) const override
        {
            if (MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor())
                processor->ensureWorldDepth(renderInfo);
        }
    };

    // After the final blit, once (PostProcessor::checkOutputOnce)
    class OutputCheckCallback : public osg::Camera::DrawCallback
    {
    public:
        void operator()(osg::RenderInfo& renderInfo) const override
        {
            if (MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor())
                processor->checkOutputOnce(renderInfo);
        }
    };

    // The transparent render bin ("DepthSortedBin"), drawn through PostProcessor::drawTransparent
    class TransparentDepthCallback : public osgUtil::RenderBin::DrawCallback
    {
    public:
        void drawImplementation(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo,
            osgUtil::RenderLeaf*& previous) override
        {
            if (MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor())
                processor->drawTransparent(bin, renderInfo, previous);
            else
                bin->drawImplementation(renderInfo, previous);
        }
    };

    // OpenMW 0.51's depthclipped shaders (files/shaders/compatibility/depthclipped.*): a transparent object's depth
    // only where its texture is at least half opaque. Colour modes 2 and 4 take the alpha from the vertex colours
    // (files/shaders/vertexcolors.glsl), the others from the material.
    const char* kTransparentDepthVertex = R"GLSL(#version 120
uniform int colorMode;
varying vec2 diffuseMapUV;
varying float alphaPassthrough;
void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
    alphaPassthrough = (colorMode == 2 || colorMode == 4) ? gl_Color.a : gl_FrontMaterial.diffuse.a;
    diffuseMapUV = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
}
)GLSL";

    const char* kTransparentDepthFragment = R"GLSL(#version 120
uniform sampler2D diffuseMap;
varying vec2 diffuseMapUV;
varying float alphaPassthrough;
void main()
{
    if (texture2D(diffuseMap, diffuseMapUV).a * alphaPassthrough < 0.499)
        discard;
}
)GLSL";

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

    // OpenMW 0.51's default vertex shader for passes without a vertex block (components/fx/pass.cpp there,
    // s_DefaultVertex); it is compiled with the same header as the effect's own stages
    const char* kDefaultPassVertex = R"GLSL(
#if OMW_USE_BINDINGS
    omw_In vec2 omw_Vertex;
#endif
omw_Out vec2 omw_TexCoord;

void main()
{
    omw_Position = vec4(omw_Vertex.xy, 0.0, 1.0);
    omw_TexCoord = omw_Position.xy * 0.5 + 0.5;
})GLSL";

    // OpenMW 0.51's post-processing API revision (OPENMW_POSTPROCESSING_API_REVISION in its CMakeLists.txt).
    // Effects branch on it (Rafael's SMAA and wetworld check for 5).
    constexpr int kPostProcessingApiRevision = 5;

    // Everything that follows the #defines in OpenMW 0.51's pass header (components/fx/pass.cpp), in its
    // GLSL 1.20 form. The "omw" fields have 0.51's names and types (components/fx/stateupdater.hpp) and are set
    // in PostProcessor::createStateUniforms / updateDynamicUniforms and by RenderingManager. Point lights: 40, as
    // 0.51's SceneUtil::PPLightBuffer::sMaxPPLights (three vec4 each), filled by RenderingManager for effects
    // with pass_lights (PostProcessor::setPointLights).
    const char* kPassHeader = R"GLSL(
#define omw_In varying
#define omw_Out varying
#define omw_Position gl_Position
#define omw_Vertex gl_Vertex
#define omw_FragColor gl_FragColor
#define omw_Texture1D texture1D
#define omw_Texture2D texture2D
#define omw_Texture3D texture3D
#define omw_Texture2DArray texture2DArray

uniform sampler2D omw_SamplerLastShader;
uniform sampler2D omw_SamplerLastPass;
uniform sampler2D omw_SamplerDepth;
uniform sampler2D omw_SamplerNormals;
uniform sampler2D omw_SamplerDistortion;
uniform sampler2D omw_EyeAdaptation;

uniform vec4 omw_PointLights[120];
uniform int omw_PointLightsCount;

struct _omw_data
{
    mat4 projectionMatrix;
    mat4 invProjectionMatrix;
    mat4 viewMatrix;
    mat4 prevViewMatrix;
    mat4 invViewMatrix;
    vec4 eyePos;
    vec4 eyeVec;
    vec4 ambientColor;
    vec4 skyColor;
    vec4 fogColor;
    vec4 sunColor;
    vec4 sunPos;
    vec4 sunVec;
    vec2 resolution;
    vec2 rcpResolution;
    float fogNear;
    float fogFar;
    float near;
    float far;
    float fov;
    float gameHour;
    float sunVis;
    float waterHeight;
    bool isWaterEnabled;
    float simulationTime;
    float deltaSimulationTime;
    int frameNumber;
    float windSpeed;
    float weatherTransition;
    int weatherID;
    int nextWeatherID;
    bool isUnderwater;
    bool isInterior;
};
uniform _omw_data omw;

mat4 omw_ProjectionMatrix() { return omw.projectionMatrix; }
mat4 omw_InvProjectionMatrix() { return omw.invProjectionMatrix; }

int omw_GetPointLightCount() { return omw_PointLightsCount; }
vec3 omw_GetPointLightWorldPos(int index) { return omw_PointLights[index * 3].xyz; }
vec3 omw_GetPointLightDiffuse(int index) { return omw_PointLights[index * 3 + 1].xyz; }
vec3 omw_GetPointLightAttenuation(int index) { return omw_PointLights[index * 3 + 2].xyz; }
float omw_GetPointLightRadius(int index) { return omw_PointLights[index * 3 + 2].w; }

float omw_GetDepth(vec2 uv) { return texture2D(omw_SamplerDepth, uv).r; }
vec4 omw_GetDistortion(vec2 uv) { return texture2D(omw_SamplerDistortion, uv); }
vec4 omw_GetLastShader(vec2 uv) { return texture2D(omw_SamplerLastShader, uv); }
vec4 omw_GetLastPass(vec2 uv) { return texture2D(omw_SamplerLastPass, uv); }

vec3 _omw_GetViewPosFromUV(vec2 uv)
{
    vec4 viewPos = omw.invProjectionMatrix * vec4(uv * 2.0 - 1.0, omw_GetDepth(uv) * 2.0 - 1.0, 1.0);
    return viewPos.xyz / viewPos.w;
}

vec3 omw_GetNormals(vec2 uv)
{
    // Every effect gets the scene's normals, as in OpenMW 0.51, whether or not it set pass_normals.
#if _OMW_NORMALS_BUFFER
    // The normals buffer the scene shaders write (view space, stored as n * 0.5 + 0.5), as 0.51 reads it
    return texture2D(omw_SamplerNormals, uv).rgb * 2.0 - 1.0;
#else
    // Without that buffer, view-space normals rebuilt from depth (flat per triangle): of the two neighbours on
    // each axis, use the one nearer in depth so silhouette edges do not smear.
    vec2 texel = omw.rcpResolution;
    vec3 c = _omw_GetViewPosFromUV(uv);
    vec3 xr = _omw_GetViewPosFromUV(clamp(uv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 xl = _omw_GetViewPosFromUV(clamp(uv - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)));
    vec3 yu = _omw_GetViewPosFromUV(clamp(uv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 yd = _omw_GetViewPosFromUV(clamp(uv - vec2(0.0, texel.y), vec2(0.0), vec2(1.0)));
    vec3 dx = (abs(xr.z - c.z) < abs(c.z - xl.z)) ? (xr - c) : (c - xl);
    vec3 dy = (abs(yu.z - c.z) < abs(c.z - yd.z)) ? (yu - c) : (c - yd);
    return normalize(cross(dx, dy));
#endif
}

vec3 omw_GetNormalsWorldSpace(vec2 uv) { return (vec4(omw_GetNormals(uv), 0.0) * omw.viewMatrix).rgb; }

vec3 omw_GetWorldPosFromUV(vec2 uv)
{
    float flippedDepth = omw_GetDepth(uv) * 2.0 - 1.0;
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, flippedDepth, 1.0);
    vec4 worldSpace = omw.invViewMatrix * (omw.invProjectionMatrix * clipSpace);
    return worldSpace.xyz / worldSpace.w;
}

float omw_GetLinearDepth(vec2 uv)
{
    float depth = omw_GetDepth(uv) * 2.0 - 1.0;
    return 2.0 * omw.near * omw.far / (omw.far + omw.near - depth * (omw.far - omw.near));
}

float omw_EstimateFogCoverageFromUV(vec2 uv)
{
#if OMW_RADIAL_FOG
    float dist = length(omw_GetWorldPosFromUV(uv) - omw.eyePos.xyz);
#else
    float dist = omw_GetLinearDepth(uv);
#endif
#if OMW_EXPONENTIAL_FOG
    return 1.0 - exp(-2.0 * max(0.0, dist - omw.fogNear / 2.0) / (omw.fogFar - omw.fogNear / 2.0));
#else
    return clamp((dist - omw.fogNear) / (omw.fogFar - omw.fogNear), 0.0, 1.0);
#endif
}

float omw_GetEyeAdaptation()
{
#if OMW_HDR
    return texture2D(omw_EyeAdaptation, vec2(0.5, 0.5)).r;
#else
    return 1.0;
#endif
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
        // sampler_1d/2d/3d textures, loaded once per effect
        std::unordered_map<std::string, osg::ref_ptr<osg::Texture>> mSamplerTextures;
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
    };

    PostProcessor::PostProcessor(osg::Camera* mainCamera, osg::Group* rootNode, Resource::ResourceSystem* resourceSystem)
        : mVfs(resourceSystem ? resourceSystem->getVFS() : nullptr)
        , mResourceSystem(resourceSystem)
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
        mWorldDepthTexture = createDepthTexture();
        if (sceneNormalsSupported())
        {
            // As OpenMW 0.51's Tex_Normal: RGB, the view-space normal stored as n * 0.5 + 0.5
            mNormalsTexture = new osg::Texture2D;
            mNormalsTexture->setInternalFormat(GL_RGB8);
            mNormalsTexture->setSourceFormat(GL_RGB);
            mNormalsTexture->setSourceType(GL_UNSIGNED_BYTE);
            mNormalsTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mNormalsTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mNormalsTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mNormalsTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mNormalsTexture->setResizeNonPowerOfTwoHint(false);
        }
        Log(Debug::Info) << "Post-processing: scene normals "
                         << (mNormalsTexture ? "buffer (written by the scene shaders)" : "rebuilt from depth");
        mDepthOnlyState = new osg::StateSet;
        mDepthOnlyState->setAttributeAndModes(new osg::ColorMask(false, false, false, false), osg::StateAttribute::ON);
        createTransparentDepthState();

        // Transparent objects go through drawTransparent, as OpenMW 0.51 replaces the same bin
        osg::ref_ptr<osgUtil::RenderBin> depthSortedBin = new osgUtil::RenderBin(osgUtil::RenderBin::SORT_BACK_TO_FRONT);
        depthSortedBin->setDrawCallback(new TransparentDepthCallback);
        osgUtil::RenderBin::addRenderBinPrototype("DepthSortedBin", depthSortedBin);
        mPingTextureA = createSceneTexture();
        mPingTextureB = createSceneTexture();
        mPingTextureC = createSceneTexture();
        mDistortionTexture = createDistortionTexture();
        mEyeAdaptTextureA = createEyeAdaptationTexture();
        mEyeAdaptTextureB = createEyeAdaptationTexture();

        // Render the world directly into mSceneTexture / mDepthTexture: the main render stage gets the world
        // framebuffer every frame from SceneFboCullCallback, as in OpenMW 0.51 (see there for why the camera
        // itself is no longer switched to render-to-texture).
        mSceneFboCallback = new SceneFboCullCallback;
        rootNode->addCullCallback(mSceneFboCallback.get());

        sActiveProcessor.store(this, std::memory_order_release);

        // The GLSL the driver offers decides the effects' shader version (see makeShaderSource). The window is
        // realised before the renderer is built, as SceneUtil::LightManager also relies on.
        if (osg::GLExtensions* exts = osg::GLExtensions::Get(0, false))
        {
            if (exts->glslLanguageVersion > 0.f)
                mDriverGLSLVersion = static_cast<int>(std::lround(exts->glslLanguageVersion * 100.f));
        }
        Log(Debug::Info) << "Post-processing: driver GLSL " << mDriverGLSLVersion << ", effects compile as GLSL "
                         << (mDriverGLSLVersion >= 330 ? "330 compatibility" : "120");

        createStateUniforms();
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
        if (mRootNode.valid() && mSceneFboCallback)
            mRootNode->removeCullCallback(mSceneFboCallback.get());
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

    std::string PostProcessor::availableEffectName(const std::string& name) const
    {
        // The data files are indexed in lower case, while settings and effect authors write "HBAO" or "SMAA";
        // as in OpenMW 0.51, any letter case finds the effect
        for (const std::string& effect : mAvailableEffects)
        {
            if (Misc::StringUtils::ciEqual(effect, name))
                return effect;
        }
        return std::string();
    }

    bool PostProcessor::effectExists(const std::string& name) const
    {
        return !availableEffectName(name).empty();
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
            const std::string available = availableEffectName(effect);
            if (!available.empty())
                next.push_back(available);
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
        mSunglareDisabled = false;
        mPassLights = false;

        if (!mReady || !mRootNode.valid())
            return;
        ++mChainBuilds;

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

            if (technique->flags() & Fx::Technique::Flag_Disable_SunGlare)
                mSunglareDisabled = true;
            if (technique->wantsLights())
                mPassLights = true;

            for (const Fx::RenderTargetSpec& spec : technique->renderTargets())
                runtimeTechnique->mTargets[spec.mName] = createTargetTexture(spec);

            for (const Fx::SamplerSpec& spec : technique->samplers())
            {
                CachedTexture& cachedTexture = mSamplerTextureCache[effectName + "|" + spec.mName + "|" + spec.mSource];
                if (!cachedTexture.mTexture)
                    cachedTexture.mTexture = createSamplerTexture(spec);
                cachedTexture.mLastUsed = mChainBuilds;
                osg::ref_ptr<osg::Texture> texture = cachedTexture.mTexture;
                if (texture)
                    runtimeTechnique->mSamplerTextures[spec.mName] = texture;
                else
                    Log(Debug::Warning) << "Effect '" << effectName << "': could not load texture '" << spec.mSource
                                        << "' for sampler '" << spec.mName << "'";
            }

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

                // Both stages get the same header, as in OpenMW 0.51; a pass without a vertex block uses 0.51's
                // default full-screen vertex shader.
                const std::string vertexSource = makeShaderSource(*technique, true,
                    spec.mVertexSource.empty() ? std::string(kDefaultPassVertex) : spec.mVertexSource, sampledTargets);
                const std::string fragmentSource = makeShaderSource(*technique, false, spec.mSource, sampledTargets);
                // The same sources as an earlier build: the same, already compiled program
                CachedProgram& cachedProgram = mProgramCache[vertexSource + '\n' + fragmentSource];
                if (!cachedProgram.mProgram)
                    cachedProgram.mProgram = createProgram(effectName + ":" + spec.mName, vertexSource, fragmentSource);
                cachedProgram.mLastUsed = mChainBuilds;
                osg::ref_ptr<osg::Program> program = cachedProgram.mProgram;
                if (!program)
                {
                    Log(Debug::Error) << "Could not create program for effect '" << effectName
                                      << "' pass '" << spec.mName << "'";
                    continue;
                }

                pass->mCamera = createCamera(order++, true);
                // A target with mipmaps gets them rebuilt after every draw (wetworld's RT_H samples its lower
                // levels); its clear colour is used on the frames it is cleared (see mClearTargetFrames)
                pass->mCamera->attach(osg::Camera::COLOR_BUFFER0, pass->mOutput.get(), 0, 0,
                    pass->mOutputSpec && pass->mOutputSpec->mMipMaps);
                if (pass->mOutputSpec)
                {
                    const std::array<float, 4>& clear = pass->mOutputSpec->mClearColor;
                    pass->mCamera->setClearColor(osg::Vec4f(clear[0], clear[1], clear[2], clear[3]));
                }
                pass->mCamera->setInitialDrawCallback(new WorldDepthCallback);
                osg::ref_ptr<osg::Geode> geode = createFullscreenPass(program, effectName + ":" + spec.mName);
                pass->mState = geode->getOrCreateStateSet();

                pass->mState->setTextureAttribute(0, effectInput);
                pass->mState->setTextureAttribute(1, lastPassOutput);
                pass->mState->setTextureAttribute(2, mWorldDepthTexture);
                pass->mState->setTextureAttribute(3, mEyeAdaptTextureA);
                pass->mState->setTextureAttribute(4, mDistortionTexture);
                // OpenMW 0.51 sampler names (components/fx/pass.cpp there)
                pass->mState->addUniform(new osg::Uniform("omw_SamplerLastShader", 0));
                pass->mState->addUniform(new osg::Uniform("omw_SamplerLastPass", 1));
                pass->mState->addUniform(new osg::Uniform("omw_SamplerDepth", 2));
                pass->mState->addUniform(new osg::Uniform("omw_EyeAdaptation", 3));
                pass->mState->addUniform(new osg::Uniform("omw_SamplerDistortion", 4));
                // unit 5: the scene's normals buffer (read only when the header's _OMW_NORMALS_BUFFER is 1)
                if (mNormalsTexture)
                    pass->mState->setTextureAttribute(5, mNormalsTexture);
                pass->mState->addUniform(new osg::Uniform("omw_SamplerNormals", 5));

                int unit = 6;
                for (const std::string& input : spec.mInputs)
                {
                    if (input.empty())
                        continue;
                    const auto inputIt = runtimeOwner->mTargets.find(input);
                    if (inputIt == runtimeOwner->mTargets.end())
                        continue;
                    pass->mState->setTextureAttribute(unit, inputIt->second);
                    pass->mState->addUniform(new osg::Uniform(input.c_str(), unit));
                    ++unit;
                }

                // The effect's own textures (sampler blocks), declared for it in the shader header
                for (const auto& sampler : runtimeOwner->mSamplerTextures)
                {
                    pass->mState->setTextureAttribute(unit, sampler.second);
                    pass->mState->addUniform(new osg::Uniform(sampler.first.c_str(), unit));
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

                // The shared "omw" state block and point lights: one set of uniforms for every pass
                for (const auto& entry : mStateUniforms)
                    pass->mState->addUniform(entry.second);

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
        mFinalCamera->setPostDrawCallback(new OutputCheckCallback);
        osg::ref_ptr<osg::Program> blitProgram = createBlitProgram();
        osg::ref_ptr<osg::Geode> finalGeode = createFullscreenPass(blitProgram, "final blit");
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

        // Keep what the last few builds used (above and below water, indoors and out); drop the rest, such as
        // programs for a static uniform value the player has since changed
        const unsigned int keepBuilds = 4;
        for (auto it = mProgramCache.begin(); it != mProgramCache.end();)
            it = mChainBuilds - it->second.mLastUsed > keepBuilds ? mProgramCache.erase(it) : std::next(it);
        for (auto it = mSamplerTextureCache.begin(); it != mSamplerTextureCache.end();)
            it = mChainBuilds - it->second.mLastUsed > keepBuilds ? mSamplerTextureCache.erase(it) : std::next(it);

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
        osg::ref_ptr<osg::Geode> geode = createFullscreenPass(program, "eye adaptation");
        mEyeAdaptState = geode->getOrCreateStateSet();
        mEyeAdaptState->setTextureAttribute(0, mSceneTexture);
        mEyeAdaptState->setTextureAttribute(1, mEyeAdaptTextureB);
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
        osg::ref_ptr<osg::Geode> copyGeode = createFullscreenPass(createBlitProgram(), "eye adaptation copy");
        osg::StateSet* copyState = copyGeode->getOrCreateStateSet();
        copyState->setTextureAttribute(0, mEyeAdaptTextureA);
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

    osg::ref_ptr<osg::Geode> PostProcessor::createFullscreenPass(osg::Program* program, const std::string& name)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setDrawCallback(new GLErrorProbe(name));
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

    osg::ref_ptr<osg::Program> PostProcessor::createProgram(
        const std::string& name, const std::string& vertexSource, const std::string& fragmentSource)
    {
        osg::ref_ptr<osg::Shader> vertex = new osg::Shader(osg::Shader::VERTEX);
        vertex->setName(name + ":vertex");
        vertex->setShaderSource(vertexSource);

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

    std::string PostProcessor::makeShaderSource(const Fx::Technique& technique, bool vertexStage,
        const std::string& passSource, const std::vector<std::string>& sampledTargets) const
    {
        // The header every pass stage gets, following OpenMW 0.51 (components/fx/pass.cpp and stateupdater.hpp
        // there): the same defines, macros, sampler names, "omw" fields and helper functions, so effects written
        // for OpenMW 0.49+ (post-processing API revision 5) compile unchanged. Standard depth (no reverse-Z);
        // normals are rebuilt from depth until the scene writes a normals buffer.
        //
        // Version: GLSL 3.30 in the compatibility profile when the driver offers it, so effects can use the 1.30+
        // features 0.51 gives them (texture(), textureLod, texelFetch, inverse(), round()); the header's legacy
        // names (gl_Vertex, gl_FragColor, varying, texture2D) stay valid in that profile. GLSL 1.20 otherwise.
        // An effect may ask for more with glsl_version / glsl_profile / glsl_extensions in its technique block.
        (void)vertexStage;   // same header for both stages, as in 0.51
        const int version = std::max(mDriverGLSLVersion >= 330 ? 330 : 120, technique.glslVersion());
        std::string profile = technique.glslProfile();
        if (profile.empty() && version >= 150)
            profile = "compatibility";

        std::ostringstream source;
        source << "#version " << version << (profile.empty() ? std::string() : " " + profile) << "\n";
        for (const std::string& extension : technique.glslExtensions())
            source << "#ifdef " << extension << "\n#extension " << extension << " : enable\n#endif\n";
        source << "#define OMW_API_VERSION " << kPostProcessingApiRevision << "\n";
        source << "#define OMW_REVERSE_Z 0\n";
        source << "#define OMW_RADIAL_FOG " << (Settings::Manager::getBool("radial fog", "Shaders") ? 1 : 0) << "\n";
        source << "#define OMW_EXPONENTIAL_FOG 0\n";
        source << "#define OMW_HDR " << (technique.wantsHDR() ? 1 : 0) << "\n";
        source << "#define OMW_NORMALS " << (technique.wantsNormals() ? 1 : 0) << "\n";
        source << "#define OMW_USE_BINDINGS 0\n";
        source << "#define OMW_MULTIVIEW 0\n";
        source << "#define _OMW_NORMALS_BUFFER " << (mNormalsTexture ? 1 : 0) << "\n";
        source << kPassHeader;
        for (const std::string& target : sampledTargets)
            source << "uniform sampler2D " << target << ";\n";
        for (const Fx::SamplerSpec& sampler : technique.samplers())
            source << "uniform sampler" << sampler.mDimensions << "D " << sampler.mName << ";\n";
        for (const Fx::UniformSpec& uniform : technique.uniforms())
            source << uniform.glslDeclaration() << "\n";
        source << "\n" << technique.sharedSource() << "\n";
        source << "#line 1\n" << passSource << "\n";
        return source.str();
    }

    osg::ref_ptr<osg::Texture> PostProcessor::createSamplerTexture(const Fx::SamplerSpec& spec) const
    {
        if (!mResourceSystem || spec.mSource.empty())
            return nullptr;
        if (mVfs && !mVfs->exists(spec.mSource))
            Log(Debug::Warning) << "Post-processing texture '" << spec.mSource << "' is not in any data folder";

        osg::ref_ptr<osg::Image> image = mResourceSystem->getImageManager()->getImage(spec.mSource);
        if (!image)
            return nullptr;

        osg::ref_ptr<osg::Texture> texture;
        if (spec.mDimensions == 1)
            texture = new osg::Texture1D(image);
        else if (spec.mDimensions == 3)
            texture = new osg::Texture3D(image);
        else
            texture = new osg::Texture2D(image);

        // As OpenMW 0.51's sampler blocks (Technique::parseSampler there)
        texture->setFilter(osg::Texture::MIN_FILTER, getFilter(spec.mMinFilter));
        texture->setFilter(osg::Texture::MAG_FILTER, getFilter(spec.mMagFilter));
        texture->setWrap(osg::Texture::WRAP_S, getWrap(spec.mWrapS));
        texture->setWrap(osg::Texture::WRAP_T, getWrap(spec.mWrapT));
        texture->setWrap(osg::Texture::WRAP_R, getWrap(spec.mWrapR));
        texture->setResizeNonPowerOfTwoHint(false);
        if (!spec.mInternalFormat.empty())
            texture->setInternalFormat(getInternalFormat(spec.mInternalFormat));
        if (!spec.mSourceFormat.empty())
            texture->setSourceFormat(getSourceFormat(spec.mSourceFormat));
        if (!spec.mSourceType.empty())
            texture->setSourceType(getSourceType(spec.mSourceType));
        return texture;
    }

    void PostProcessor::resizeTargets(int width, int height)
    {
        mWidth = std::max(1, width);
        mHeight = std::max(1, height);

        // resized targets start undefined, so they are cleared once more
        mClearTargetFrames = 2;
        for (const std::unique_ptr<RuntimePass>& pass : mRuntimePasses)
        {
            if (pass->mCamera)
                pass->mCamera->setClearMask(GL_COLOR_BUFFER_BIT);
        }

        mSceneTexture->setTextureSize(mWidth, mHeight);
        mDepthTexture->setTextureSize(mWidth, mHeight);
        mWorldDepthTexture->setTextureSize(mWidth, mHeight);
        if (mNormalsTexture)
            mNormalsTexture->setTextureSize(mWidth, mHeight);
        createCopyFbos();
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

    void PostProcessor::createStateUniforms()
    {
        // One uniform per "omw" field (names and types as OpenMW 0.51's StateUpdater), shared by every pass.
        // RenderingManager overwrites the world values; the defaults below are what an effect sees before then.
        mStateUniforms.clear();
        auto add = [this](const std::string& name, osg::Uniform* uniform) { mStateUniforms[name] = uniform; };
        for (const char* field : { "projectionMatrix", "invProjectionMatrix", "viewMatrix", "prevViewMatrix",
                 "invViewMatrix" })
            add(field, new osg::Uniform((std::string("omw.") + field).c_str(), osg::Matrixf()));
        for (const char* field : { "eyePos", "eyeVec", "ambientColor", "skyColor", "fogColor", "sunColor", "sunPos",
                 "sunVec" })
            add(field, new osg::Uniform((std::string("omw.") + field).c_str(), osg::Vec4f(0.f, 0.f, 0.f, 0.f)));
        add("resolution", new osg::Uniform("omw.resolution", osg::Vec2f(1.f, 1.f)));
        add("rcpResolution", new osg::Uniform("omw.rcpResolution", osg::Vec2f(1.f, 1.f)));
        for (const char* field : { "fogNear", "fogFar", "near", "far", "fov", "gameHour", "sunVis", "waterHeight",
                 "simulationTime", "deltaSimulationTime", "windSpeed", "weatherTransition" })
            add(field, new osg::Uniform((std::string("omw.") + field).c_str(), 0.f));
        for (const char* field : { "frameNumber", "weatherID", "nextWeatherID" })
            add(field, new osg::Uniform((std::string("omw.") + field).c_str(), 0));
        for (const char* field : { "isWaterEnabled", "isUnderwater", "isInterior" })
            add(field, new osg::Uniform((std::string("omw.") + field).c_str(), false));
        stateUniform("nextWeatherID")->set(-1);
        stateUniform("sunVis")->set(1.f);

        osg::ref_ptr<osg::Uniform> pointLights
            = new osg::Uniform(osg::Uniform::FLOAT_VEC4, "omw_PointLights", sMaxPointLights * 3);
        for (unsigned int i = 0; i < sMaxPointLights * 3; ++i)
            pointLights->setElement(i, osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        mStateUniforms["omw_PointLights"] = pointLights;
        mStateUniforms["omw_PointLightsCount"] = new osg::Uniform("omw_PointLightsCount", 0);
    }

    osg::Uniform* PostProcessor::stateUniform(const std::string& field) const
    {
        const auto it = mStateUniforms.find(field);
        return it == mStateUniforms.end() ? nullptr : it->second.get();
    }

    void PostProcessor::setFogRange(float fogNear, float fogFar)
    {
        if (osg::Uniform* uniform = stateUniform("fogNear")) uniform->set(fogNear);
        if (osg::Uniform* uniform = stateUniform("fogFar")) uniform->set(fogFar);
    }

    void PostProcessor::setFogColor(const osg::Vec4f& color)
    {
        if (osg::Uniform* uniform = stateUniform("fogColor")) uniform->set(color);
    }

    void PostProcessor::setAmbientColor(const osg::Vec4f& color)
    {
        if (osg::Uniform* uniform = stateUniform("ambientColor")) uniform->set(color);
    }

    void PostProcessor::setSkyColor(const osg::Vec4f& color)
    {
        if (osg::Uniform* uniform = stateUniform("skyColor")) uniform->set(color);
    }

    void PostProcessor::setSunColor(const osg::Vec4f& color, float sunVis)
    {
        if (osg::Uniform* uniform = stateUniform("sunColor")) uniform->set(color);
        if (osg::Uniform* uniform = stateUniform("sunVis")) uniform->set(sunVis);
    }

    void PostProcessor::setSunPos(const osg::Vec4f& pos, bool night)
    {
        // as OpenMW 0.51: normalised, and below the horizon at night
        osg::Vec4f value = pos;
        value.normalize();
        if (night)
            value.z() *= -1.f;
        if (osg::Uniform* uniform = stateUniform("sunPos")) uniform->set(value);
    }

    void PostProcessor::setSunVec(const osg::Vec4f& vec)
    {
        osg::Vec4f value = vec;
        value.normalize();
        if (osg::Uniform* uniform = stateUniform("sunVec")) uniform->set(value);
    }

    void PostProcessor::setGameHour(float hour)
    {
        if (osg::Uniform* uniform = stateUniform("gameHour")) uniform->set(hour);
    }

    void PostProcessor::setWeather(int weatherId, int nextWeatherId, float transition, float windSpeed)
    {
        if (osg::Uniform* uniform = stateUniform("weatherID")) uniform->set(weatherId);
        if (osg::Uniform* uniform = stateUniform("nextWeatherID")) uniform->set(nextWeatherId);
        // as OpenMW 0.51: 0 when no transition is running, otherwise how far it has come
        if (osg::Uniform* uniform = stateUniform("weatherTransition"))
            uniform->set(transition > 0.f ? 1.f - transition : 0.f);
        if (osg::Uniform* uniform = stateUniform("windSpeed")) uniform->set(windSpeed);
    }

    void PostProcessor::setPointLights(const std::vector<osg::Vec4f>& packed)
    {
        osg::Uniform* lights = stateUniform("omw_PointLights");
        osg::Uniform* count = stateUniform("omw_PointLightsCount");
        if (!lights || !count)
            return;
        const unsigned int size = std::min(static_cast<unsigned int>(packed.size()), lights->getNumElements());
        for (unsigned int i = 0; i < size; ++i)
            lights->setElement(i, packed[i]);
        count->set(static_cast<int>(size / 3));
    }

    void PostProcessor::setWaterHeight(float height)
    {
        if (osg::Uniform* uniform = stateUniform("waterHeight")) uniform->set(height);
    }

    void PostProcessor::setWaterEnabled(bool enabled)
    {
        if (osg::Uniform* uniform = stateUniform("isWaterEnabled")) uniform->set(enabled);
    }

    void PostProcessor::updateDynamicUniforms()
    {
        const osg::Vec2f resolution(
            static_cast<float>(std::max(1, mWidth)), static_cast<float>(std::max(1, mHeight)));
        stateUniform("resolution")->set(resolution);
        stateUniform("rcpResolution")->set(osg::Vec2f(1.f / resolution.x(), 1.f / resolution.y()));
        stateUniform("near")->set(mNear);
        stateUniform("far")->set(mFar);
        stateUniform("fov")->set(mFov);
        stateUniform("simulationTime")->set(mSimulationTime);
        stateUniform("deltaSimulationTime")->set(mDeltaTime);
        stateUniform("frameNumber")->set(mFrameNumber);
        stateUniform("isInterior")->set(mInterior);
        stateUniform("isUnderwater")->set(mUnderwater);

        if (mMainCamera.valid())
        {
            const osg::Matrixf projection(mMainCamera->getProjectionMatrix());
            stateUniform("projectionMatrix")->set(projection);
            stateUniform("invProjectionMatrix")->set(osg::Matrixf::inverse(projection));
            stateUniform("viewMatrix")->set(mViewMatrix);
            stateUniform("invViewMatrix")->set(osg::Matrixf::inverse(mViewMatrix));
            stateUniform("prevViewMatrix")->set(mPrevViewMatrix);

            // eye position and look direction in world space (OpenMW 0.51 uses the cull visitor's eye point and
            // local look vector, which are the same for the main camera)
            osg::Vec3d eye, center, up;
            mMainCamera->getViewMatrixAsLookAt(eye, center, up);
            osg::Vec3f look(center - eye);
            look.normalize();
            stateUniform("eyePos")->set(osg::Vec4f(osg::Vec3f(eye), 0.f));
            stateUniform("eyeVec")->set(osg::Vec4f(look, 0.f));
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
            mFinalState->setTextureAttribute(0, source);
    }

    bool PostProcessor::sceneNormalsSupported()
    {
        // Decided once, when the renderer is built (the window already exists): the same answer goes to the
        // scene shaders (RenderingManager's writeNormals define), the sky, the shader visitor and the effects
        static const bool supported = []
        {
            if (!Settings::Manager::getBool("force shaders", "Shaders"))
                return false;
            const osg::GLExtensions* exts = osg::GLExtensions::Get(0, false);
            return exts && exts->glslLanguageVersion >= 1.3f;
        }();
        return supported;
    }

    void PostProcessor::createCopyFbos()
    {
        // The world framebuffer (SceneFboCullCallback), and framebuffers for copying depth and for the output
        // check; a new size needs new attachments
        mSceneFbo = new osg::FrameBufferObject;
        mSceneFbo->setAttachment(osg::Camera::COLOR_BUFFER0, osg::FrameBufferAttachment(mSceneTexture.get()));
        mSceneFbo->setAttachment(osg::Camera::DEPTH_BUFFER, osg::FrameBufferAttachment(mDepthTexture.get()));
        if (mNormalsTexture)
            mSceneFbo->setAttachment(osg::Camera::COLOR_BUFFER1, osg::FrameBufferAttachment(mNormalsTexture.get()));
        mSceneDepthFbo = new osg::FrameBufferObject;
        mSceneDepthFbo->setAttachment(osg::Camera::DEPTH_BUFFER, osg::FrameBufferAttachment(mDepthTexture.get()));
        mWorldDepthFbo = new osg::FrameBufferObject;
        mWorldDepthFbo->setAttachment(osg::Camera::DEPTH_BUFFER, osg::FrameBufferAttachment(mWorldDepthTexture.get()));
        mSceneColorFbo = new osg::FrameBufferObject;
        mSceneColorFbo->setAttachment(osg::Camera::COLOR_BUFFER0, osg::FrameBufferAttachment(mSceneTexture.get()));
    }

    void PostProcessor::copySceneDepth(osg::State& state)
    {
        const osg::ref_ptr<osg::FrameBufferObject> from = mSceneDepthFbo;
        const osg::ref_ptr<osg::FrameBufferObject> to = mWorldDepthFbo;
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        if (!from || !to || !ext || !ext->isFrameBufferObjectSupported || !ext->glBlitFramebuffer)
            return;

        GLint drawBinding = 0;
        GLint readBinding = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &drawBinding);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &readBinding);
        from->apply(state, osg::FrameBufferObject::READ_FRAMEBUFFER);
        glReadBuffer(GL_NONE);
        to->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        glDrawBuffer(GL_NONE);
        ext->glBlitFramebuffer(0, 0, mWidth, mHeight, 0, 0, mWidth, mHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        ext->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, readBinding);
        ext->glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, drawBinding);
    }

    void PostProcessor::captureWorldDepth(osg::RenderInfo& renderInfo)
    {
        // R5.1 only marked this point; the depth effects then read a buffer the first-person clear had emptied (all
        // "sky" except the arms). The world depth is copied out here instead, unless the transparent bin already
        // made this frame's copy (which must be kept: it has leaves without their see-through card corners).
        ensureWorldDepth(renderInfo);
    }

    void PostProcessor::createTransparentDepthState()
    {
        // As OpenMW 0.51's TransparentDepthBinCallback: the alpha-clip program, no blending, depth test and write.
        // A white texture stands in on unit 0 for objects without one (theirs takes precedence).
        osg::ref_ptr<osg::Image> white = new osg::Image;
        white->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        white->setColor(osg::Vec4(1.f, 1.f, 1.f, 1.f), 0, 0);
        osg::ref_ptr<osg::Texture2D> whiteTexture = new osg::Texture2D(white);

        const osg::StateAttribute::OverrideValue off = osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE;
        const osg::StateAttribute::OverrideValue on = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE;
        mTransparentDepthState = new osg::StateSet;
        mTransparentDepthState->setTextureAttributeAndModes(0, whiteTexture);
        mTransparentDepthState->setAttributeAndModes(new osg::BlendFunc, off);
        mTransparentDepthState->setAttributeAndModes(
            createProgram("RickyMP_TransparentDepth", kTransparentDepthVertex, kTransparentDepthFragment), on);
        mTransparentDepthState->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0.0, 1.0, true), on);
        for (unsigned int unit = 1; unit < 8; ++unit)
            mTransparentDepthState->setTextureMode(unit, GL_TEXTURE_2D, off);
    }

    namespace
    {
        // Additive objects (fire and glow cards: blending onto the destination with factor one) never hide what is
        // behind them and write no depth when drawn. In the transparent post-pass depth they became solid cards,
        // and ambient occlusion (HBAO) shaded flames into black blobs. The nearest blend function in the leaf's
        // state graph decides.
        bool isAdditive(const osgUtil::RenderLeaf* leaf)
        {
            for (const osgUtil::StateGraph* graph = leaf->_parent; graph != nullptr; graph = graph->_parent)
            {
                const osg::StateSet* stateSet = graph->getStateSet();
                if (!stateSet)
                    continue;
                const osg::BlendFunc* blend
                    = static_cast<const osg::BlendFunc*>(stateSet->getAttribute(osg::StateAttribute::BLENDFUNC));
                if (blend)
                    return blend->getDestinationRGB() == GL_ONE;
            }
            return false;
        }

        // The nearest material in the leaf's state graph (a shape's own, or the one it inherits)
        const osg::Material* nearestMaterial(const osgUtil::RenderLeaf* leaf)
        {
            for (const osgUtil::StateGraph* graph = leaf->_parent; graph != nullptr; graph = graph->_parent)
            {
                const osg::StateSet* stateSet = graph->getStateSet();
                if (!stateSet)
                    continue;
                if (const osg::StateAttribute* material = stateSet->getAttribute(osg::StateAttribute::MATERIAL))
                    return static_cast<const osg::Material*>(material);
            }
            return nullptr;
        }

        // Self-lit objects (flame cards, glowing lantern glass and paper: emissive materials, which the NIF loader
        // also gives to vertex-colour emission) are light sources. Many modded flames blend normally rather than
        // additively (Enlightened Flames candles and lanterns, the Improved Lights campfire card), so as solid
        // cards in the post-pass depth they were still shaded black by ambient occlusion. Light is not occluded.
        bool isSelfLit(const osg::Material* material)
        {
            const osg::Vec4& emission = material->getEmission(osg::Material::FRONT);
            return std::max(emission.r(), std::max(emission.g(), emission.b())) >= 0.5f;
        }
    }

    void PostProcessor::drawTransparent(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo,
        osgUtil::RenderLeaf*& previous)
    {
        // Other cameras (water reflection, maps, previews) draw their transparent objects as usual
        const osg::ref_ptr<osg::FrameBufferObject> scene = mSceneFbo;
        const osg::ref_ptr<osg::FrameBufferObject> world = mWorldDepthFbo;
        if (!scene || !world || !bin->getStage() || bin->getStage()->getFrameBufferObject() != scene.get())
        {
            bin->drawImplementation(renderInfo, previous);
            return;
        }

        // The depth copy starts from the opaque world, before any transparent object writes its whole card
        osg::State& state = *renderInfo.getState();
        copySceneDepth(state);
        if (const osg::FrameStamp* stamp = state.getFrameStamp())
            mWorldDepthFrame.store(stamp->getFrameNumber(), std::memory_order_release);

        bin->drawImplementation(renderInfo, previous);

        if (!mTransparentPostpass)
            return;
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        if (!ext || !ext->isFrameBufferObjectSupported)
            return;

        // Then the transparent objects again, into the copy only, clipped to their opaque texels (OpenMW 0.51's
        // "transparent post-pass"). Particles, additive and self-lit objects (flames, glows) and objects whose
        // material is mostly see-through are left out.
        GLint drawBinding = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &drawBinding);
        world->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        glDrawBuffer(GL_NONE);

        unsigned int numToPop = previous ? osgUtil::StateGraph::numToPop(previous->_parent) : 0;
        if (numToPop > 1)
            --numToPop;
        const unsigned int insertStateSetPosition = state.getStateSetStackSize() - numToPop;
        state.insertStateSet(insertStateSetPosition, mTransparentDepthState.get());
        for (osgUtil::RenderLeaf* leaf : bin->getRenderLeafList())
        {
            const unsigned int mask = leaf->_drawable->getNodeMask();
            if (mask == Mask_ParticleSystem || mask == Mask_Effect || isAdditive(leaf))
                continue;
            if (const osg::Material* material = nearestMaterial(leaf))
            {
                if (material->getDiffuse(osg::Material::FRONT).a() < 0.5f || isSelfLit(material))
                    continue;
            }
            leaf->render(renderInfo, previous);
            previous = leaf;
        }
        state.removeStateSet(insertStateSetPosition);

        ext->glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, drawBinding);
    }

    void PostProcessor::drawFirstPersonDepth(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo,
        osgUtil::RenderLeaf*& previous)
    {
        // The arms, drawn again into the depth copy with colour writes off, so effects see them in front of the
        // world (OpenMW 0.51's "depth accumulation pass")
        osg::State& state = *renderInfo.getState();
        const osg::ref_ptr<osg::FrameBufferObject> world = mWorldDepthFbo;
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        if (!world || !ext || !ext->isFrameBufferObjectSupported)
            return;

        GLint drawBinding = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &drawBinding);
        world->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
        glDrawBuffer(GL_NONE);
        const osg::ref_ptr<osg::StateSet> restore = bin->getStateSet();
        bin->setStateSet(mDepthOnlyState.get());
        bin->drawImplementation(renderInfo, previous);
        bin->setStateSet(restore.get());
        ext->glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, drawBinding);
    }

    void PostProcessor::ensureWorldDepth(osg::RenderInfo& renderInfo)
    {
        const osg::FrameStamp* stamp = renderInfo.getState()->getFrameStamp();
        if (!stamp || mWorldDepthFrame.load(std::memory_order_acquire) == stamp->getFrameNumber())
            return;
        copySceneDepth(*renderInfo.getState());
        mWorldDepthFrame.store(stamp->getFrameNumber(), std::memory_order_release);
    }

    void PostProcessor::checkOutputOnce(osg::RenderInfo& renderInfo)
    {
        osg::State& state = *renderInfo.getState();
        static const unsigned int sampleFrames[] = { 600, 3000, 9000 };
        const osg::FrameStamp* stamp = state.getFrameStamp();
        const unsigned int done = mOutputChecks.load(std::memory_order_acquire);
        if (!stamp || done >= 3 || stamp->getFrameNumber() < sampleFrames[done])
            return;
        mOutputChecks.store(done + 1, std::memory_order_release);
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        const osg::ref_ptr<osg::FrameBufferObject> sceneColor = mSceneColorFbo;
        if (!ext || !ext->isFrameBufferObjectSupported || !sceneColor)
            return;

        GLint readBinding = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &readBinding);
        float screen[4] = { -1.f, -1.f, -1.f, -1.f };
        float scene[4] = { -1.f, -1.f, -1.f, -1.f };
        ext->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, 0);
        glReadBuffer(GL_BACK);
        glReadPixels(mWidth / 2, mHeight / 2, 1, 1, GL_RGBA, GL_FLOAT, screen);
        sceneColor->apply(state, osg::FrameBufferObject::READ_FRAMEBUFFER);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels(mWidth / 2, mHeight / 2, 1, 1, GL_RGBA, GL_FLOAT, scene);
        ext->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, readBinding);

        Log(Debug::Info) << "Post-processing check (frame " << stamp->getFrameNumber() << ", effects "
                         << (mEnabled && !mRuntimePasses.empty() ? "on" : "off") << "): rendered world centre "
                         << scene[0] << " " << scene[1] << " " << scene[2] << ", screen centre after the final blit "
                         << screen[0] << " " << screen[1] << " " << screen[2] << " (OpenGL error 0x" << std::hex
                         << glGetError() << std::dec << ")";
    }

    void PostProcessor::captureWorldDepthBeforeFirstPersonClear(osg::RenderInfo& renderInfo)
    {
        PostProcessor* processor = sActiveProcessor.load(std::memory_order_acquire);
        if (processor)
            processor->captureWorldDepth(renderInfo);
    }

    void PostProcessor::addFirstPersonDepth(osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo,
        osgUtil::RenderLeaf*& previous)
    {
        PostProcessor* processor = sActiveProcessor.load(std::memory_order_acquire);
        if (processor)
            processor->drawFirstPersonDepth(bin, renderInfo, previous);
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
                mFov = static_cast<float>(fovy);
            }

            // omw.prevViewMatrix is last frame's view, as OpenMW 0.51 keeps it (pingpongcull.cpp there)
            mPrevViewMatrix = mFrameNumber == 0 ? osg::Matrixf(mMainCamera->getViewMatrix()) : mViewMatrix;
            mViewMatrix = osg::Matrixf(mMainCamera->getViewMatrix());
        }
        ++mFrameNumber;

        updateDynamicUniforms();
        applyPassVisibility();

        // Effect targets are cleared on the first frame after they are created or resized, and then keep what
        // they hold from frame to frame, as in OpenMW 0.51 (pingpongcanvas.cpp there). R5.1 cleared every pass
        // target every frame, which erased the history SMB reads (its "no signal" static).
        if (mClearTargetFrames > 0)
        {
            --mClearTargetFrames;
            for (const std::unique_ptr<RuntimePass>& pass : mRuntimePasses)
            {
                if (pass->mCamera)
                    pass->mCamera->setClearMask(mClearTargetFrames > 0 ? GL_COLOR_BUFFER_BIT : 0);
            }
        }
    }
}
