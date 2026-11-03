#include "ktx2loader.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <istream>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include <osg/GLExtensions>
#include <osg/Image>

#include <ktx.h>

#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_SRGB8
#define GL_SRGB8 0x8C41
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif


namespace
{
    struct KtxDeleter
    {
        void operator()(ktxTexture* texture) const
        {
            if (texture)
                ktxTexture_Destroy(texture);
        }
    };

    struct GlFormat
    {
        GLint internalFormat = 0;
        GLenum pixelFormat = 0;
        GLenum dataType = GL_UNSIGNED_BYTE;
        bool compressed = false;
    };

    bool hasS3tc()
    {
        osg::GLExtensions* exts = osg::GLExtensions::Get(0, false);
        return (exts && exts->isTextureCompressionS3TCSupported)
            || osg::isGLExtensionSupported(0, "GL_EXT_texture_compression_s3tc")
            || osg::isGLExtensionSupported(0, "GL_S3_s3tc");
    }


    // Vulkan format values are stable API values. We intentionally support the
    // common desktop formats used by texture conversion tools. Basis Universal
    // KTX2 is handled separately and is the recommended ArenaMP asset format.
    bool mapVkFormat(ktx_uint32_t vkFormat, GlFormat& format)
    {
        switch (vkFormat)
        {
            case 9: // VK_FORMAT_R8_UNORM
                format = { GL_R8, GL_RED, GL_UNSIGNED_BYTE, false };
                return true;
            case 16: // VK_FORMAT_R8G8_UNORM
                format = { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false };
                return true;
            case 23: // VK_FORMAT_R8G8B8_UNORM
                format = { GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, false };
                return true;
            case 29: // VK_FORMAT_R8G8B8_SRGB
                format = { GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, false };
                return true;
            case 37: // VK_FORMAT_R8G8B8A8_UNORM
                format = { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false };
                return true;
            case 43: // VK_FORMAT_R8G8B8A8_SRGB
                format = { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false };
                return true;
            case 44: // VK_FORMAT_B8G8R8A8_UNORM
                format = { GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, false };
                return true;
            case 50: // VK_FORMAT_B8G8R8A8_SRGB
                format = { GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, false };
                return true;
            case 131: // VK_FORMAT_BC1_RGB_UNORM_BLOCK
                format = { GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 132: // VK_FORMAT_BC1_RGB_SRGB_BLOCK
                format = { GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 133: // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
                format = { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 134: // VK_FORMAT_BC1_RGBA_SRGB_BLOCK
                format = { GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 135: // VK_FORMAT_BC2_UNORM_BLOCK
                format = { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 136: // VK_FORMAT_BC2_SRGB_BLOCK
                format = { GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 137: // VK_FORMAT_BC3_UNORM_BLOCK
                format = { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            case 138: // VK_FORMAT_BC3_SRGB_BLOCK
                format = { GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE, true };
                return hasS3tc();
            default:
                return false;
        }
    }

    bool checkedAdd(std::size_t& total, std::size_t value)
    {
        if (value > std::numeric_limits<std::size_t>::max() - total)
            return false;
        total += value;
        return true;
    }
}

namespace Resource
{
    osg::ref_ptr<osg::Image> loadKtx2Image(std::istream& stream, const std::string& filename, std::string& error)
    {
        std::vector<ktx_uint8_t> fileData(
            (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (fileData.empty())
        {
            error = "empty KTX2 stream";
            return nullptr;
        }

        // A single texture large enough to exhaust address space is never a
        // legitimate Morrowind asset. Keep malformed containers away from KTX
        // allocations and OSG image construction.
        constexpr std::size_t maxKtxFileSize = 1024ull * 1024ull * 1024ull;
        if (fileData.size() > maxKtxFileSize)
        {
            error = "KTX2 file exceeds the 1 GiB safety limit";
            return nullptr;
        }

        ktxTexture* rawTexture = nullptr;
        KTX_error_code result = ktxTexture_CreateFromMemory(
            fileData.data(), fileData.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &rawTexture);
        std::unique_ptr<ktxTexture, KtxDeleter> texture(rawTexture);
        if (result != KTX_SUCCESS || !texture)
        {
            error = std::string("libktx: ") + ktxErrorString(result);
            return nullptr;
        }

        if (texture->classId != ktxTexture2_c)
        {
            error = "KTX1 is not accepted by the native KTX2 loader";
            return nullptr;
        }
        if (texture->numDimensions != 2 || texture->baseDepth > 1 || texture->isArray || texture->isCubemap
            || texture->numFaces != 1 || texture->numLayers > 1)
        {
            error = "only ordinary 2D KTX2 textures are supported for NIF materials";
            return nullptr;
        }
        if (texture->baseWidth == 0 || texture->baseHeight == 0 || texture->numLevels == 0)
        {
            error = "invalid KTX2 dimensions or mip level count";
            return nullptr;
        }

        GlFormat glFormat;
        ktxTexture2* texture2 = reinterpret_cast<ktxTexture2*>(texture.get());

        if (ktxTexture_NeedsTranscoding(texture.get()))
        {
            // The OpenMW OSG fork used by this ArenaMP branch has complete
            // size/mipmap/vertical-flip handling for S3TC/DXT, but not for
            // BPTC/BC7 in osg::Image. On desktop transcode universal Basis
            // automatically to BC1 for opaque textures and BC3 for textures
            // with alpha. This preserves the same 4/8-bpp VRAM classes as the
            // usual DXT1/DXT5 DDS path. Fall back to RGBA8 if S3TC is absent.
            const ktx_transcode_fmt_e target = hasS3tc() ? KTX_TTF_BC1_OR_3 : KTX_TTF_RGBA32;

            result = ktxTexture2_TranscodeBasis(texture2, target, KTX_TF_HIGH_QUALITY);
            if (result != KTX_SUCCESS)
            {
                error = std::string("Basis transcode failed: ") + ktxErrorString(result);
                return nullptr;
            }

            // libktx updates vkFormat to the concrete transcode result,
            // including UNORM vs sRGB and BC1 vs BC3. Map that result rather
            // than guessing from the source metadata.
            if (!mapVkFormat(texture2->vkFormat, glFormat))
            {
                error = "libktx produced a texture format unsupported by this OpenMW/OSG build";
                return nullptr;
            }
        }
        else if (!mapVkFormat(texture2->vkFormat, glFormat))
        {
            error = "unsupported KTX2 VkFormat for this OpenGL GPU; use Basis/UASTC KTX2 for portable assets";
            return nullptr;
        }

        const ktx_uint8_t* source = ktxTexture_GetData(texture.get());
        if (!source)
        {
            error = "KTX2 image data was not loaded";
            return nullptr;
        }

        constexpr std::size_t maxDecodedSize = 1536ull * 1024ull * 1024ull;
        std::size_t totalSize = 0;
        std::vector<osg::Image::MipmapDataType::value_type> mipOffsets;
        mipOffsets.reserve(texture->numLevels > 0 ? texture->numLevels - 1 : 0);

        for (ktx_uint32_t level = 0; level < texture->numLevels; ++level)
        {
            const std::size_t levelSize = static_cast<std::size_t>(ktxTexture_GetImageSize(texture.get(), level));
            if (level > 0)
            {
                if (totalSize > std::numeric_limits<unsigned int>::max())
                {
                    error = "KTX2 mip chain is too large for OpenSceneGraph";
                    return nullptr;
                }
                mipOffsets.push_back(static_cast<unsigned int>(totalSize));
            }
            if (!checkedAdd(totalSize, levelSize) || totalSize > maxDecodedSize)
            {
                error = "decoded KTX2 data exceeds the safety limit";
                return nullptr;
            }
        }

        unsigned char* packedData = new (std::nothrow) unsigned char[totalSize];
        if (!packedData)
        {
            error = "not enough memory for decoded KTX2 texture";
            return nullptr;
        }

        std::size_t destinationOffset = 0;
        for (ktx_uint32_t level = 0; level < texture->numLevels; ++level)
        {
            ktx_size_t sourceOffset = 0;
            result = ktxTexture_GetImageOffset(texture.get(), level, 0, 0, &sourceOffset);
            if (result != KTX_SUCCESS)
            {
                delete[] packedData;
                error = std::string("invalid KTX2 mip offset: ") + ktxErrorString(result);
                return nullptr;
            }
            const std::size_t levelSize = static_cast<std::size_t>(ktxTexture_GetImageSize(texture.get(), level));
            const std::size_t dataSize = static_cast<std::size_t>(ktxTexture_GetDataSize(texture.get()));
            if (sourceOffset > dataSize || levelSize > dataSize - sourceOffset)
            {
                delete[] packedData;
                error = "KTX2 mip range points outside decoded image data";
                return nullptr;
            }
            std::memcpy(packedData + destinationOffset, source + sourceOffset, levelSize);
            destinationOffset += levelSize;
        }

        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->setImage(static_cast<int>(texture->baseWidth), static_cast<int>(texture->baseHeight), 1,
            glFormat.internalFormat, glFormat.pixelFormat, glFormat.dataType, packedData, osg::Image::USE_NEW_DELETE, 1);
        if (!mipOffsets.empty())
            image->setMipmapLevels(mipOffsets);

        // KTX2 defaults to top-left/Y-down row order while OpenMW's DDS path
        // uses dds_flip and presents textures in OpenGL bottom-left order.
        // Physically flip the pixels/blocks here, including every DXT mip level,
        // so existing NIF UVs need no changes. OSG's dxtc_tool handles BC1/2/3.
        if (texture->orientation.y == KTX_ORIENT_Y_DOWN)
            image->flipVertical();
        image->setOrigin(osg::Image::BOTTOM_LEFT);
        image->setFileName(filename);
        return image;
    }
}
