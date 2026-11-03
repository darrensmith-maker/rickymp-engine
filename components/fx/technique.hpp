#ifndef OPENMW_COMPONENTS_FX_TECHNIQUE_HPP
#define OPENMW_COMPONENTS_FX_TECHNIQUE_HPP

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fx
{
    using FlagsType = std::size_t;

    enum class UniformType
    {
        Bool,
        Int,
        Float,
        Vec2,
        Vec3,
        Vec4
    };

    struct UniformChoice
    {
        std::string mLabel;
        std::array<float, 4> mValue{{0.f, 0.f, 0.f, 0.f}};
    };

    struct UniformSpec
    {
        std::string mName;
        std::string mDisplayName;
        std::string mHeader;
        std::string mDescription;
        UniformType mType = UniformType::Float;
        std::array<float, 4> mDefault{{0.f, 0.f, 0.f, 0.f}};
        std::array<float, 4> mMin{{0.f, 0.f, 0.f, 0.f}};
        std::array<float, 4> mMax{{0.f, 0.f, 0.f, 0.f}};
        float mStep = 1.f;
        bool mStatic = true;
        std::vector<UniformChoice> mChoices;

        int components() const;
        std::string glslDeclaration() const;
    };

    struct RenderTargetSpec
    {
        std::string mName;
        float mWidthRatio = 1.f;
        float mHeightRatio = 1.f;
        int mWidth = 0;
        int mHeight = 0;
        std::string mInternalFormat = "rgba";
        std::string mSourceFormat = "rgba";
        std::string mSourceType = "unsigned_byte";
        std::string mMinFilter = "linear";
        std::string mMagFilter = "linear";
        std::string mWrapS = "clamp_to_edge";
        std::string mWrapT = "clamp_to_edge";
        bool mMipMaps = false;
        std::array<float, 4> mClearColor{{0.f, 0.f, 0.f, 1.f}};

        std::pair<int, int> sizeFor(int width, int height) const;
    };

    struct PassSpec
    {
        std::string mName;
        std::string mSource;        // fragment code
        std::string mVertexSource;  // vertex code from a "vertex" block of the same name; empty = full-screen default
        std::string mTarget;
        std::array<std::string, 3> mInputs;
    };

    // A texture an effect loads from the data files: a sampler_1d / sampler_2d / sampler_3d block. Defaults are
    // OpenMW 0.51's (components/fx/technique.cpp, ProxyTextureData). Empty formats mean "as the image is stored".
    struct SamplerSpec
    {
        std::string mName;
        int mDimensions = 2;
        std::string mSource;
        std::string mMinFilter = "linear_mipmap_linear";
        std::string mMagFilter = "linear";
        std::string mWrapS = "clamp_to_edge";
        std::string mWrapT = "clamp_to_edge";
        std::string mWrapR = "clamp_to_edge";
        std::string mCompression;
        std::string mSourceFormat;
        std::string mSourceType;
        std::string mInternalFormat;
    };

    class Technique
    {
    public:
        static constexpr FlagsType Flag_Disable_Interiors = (1 << 0);
        static constexpr FlagsType Flag_Disable_Exteriors = (1 << 1);
        static constexpr FlagsType Flag_Disable_Underwater = (1 << 2);
        static constexpr FlagsType Flag_Disable_Abovewater = (1 << 3);
        static constexpr FlagsType Flag_Disable_SunGlare = (1 << 4);
        static constexpr FlagsType Flag_Hidden = (1 << 5);

        bool parse(const std::string& source, std::string* error = nullptr);

        const std::string& sharedSource() const { return mShared; }
        const std::vector<UniformSpec>& uniforms() const { return mUniforms; }
        const std::vector<RenderTargetSpec>& renderTargets() const { return mRenderTargets; }
        const std::vector<SamplerSpec>& samplers() const { return mSamplers; }
        const std::vector<PassSpec>& passes() const { return mOrderedPasses; }

        // GLSL the technique asks for (technique block glsl_version / glsl_profile / glsl_extensions);
        // 0 = no particular version, the engine picks its own
        int glslVersion() const { return mGLSLVersion; }
        const std::string& glslProfile() const { return mGLSLProfile; }
        const std::vector<std::string>& glslExtensions() const { return mGLSLExtensions; }

        const std::string& author() const { return mAuthor; }
        const std::string& description() const { return mDescription; }
        const std::string& version() const { return mVersion; }
        FlagsType flags() const { return mFlags; }
        bool wantsNormals() const { return mPassNormals; }
        bool wantsHDR() const { return mHDR; }
        bool wantsLights() const { return mPassLights; }

        const RenderTargetSpec* findRenderTarget(const std::string& name) const;

    private:
        void clear();

        std::string mShared;
        std::vector<UniformSpec> mUniforms;
        std::vector<RenderTargetSpec> mRenderTargets;
        std::vector<SamplerSpec> mSamplers;
        std::vector<PassSpec> mPasses;
        std::vector<PassSpec> mOrderedPasses;
        std::vector<std::string> mPassOrder;

        std::string mAuthor;
        std::string mDescription;
        std::string mVersion;
        FlagsType mFlags = 0;
        bool mPassNormals = false;
        bool mHDR = false;
        bool mPassLights = false;
        int mGLSLVersion = 0;
        std::string mGLSLProfile;
        std::vector<std::string> mGLSLExtensions;
    };
}

#endif
