#ifndef OPENMW_COMPONENTS_RESOURCE_KTX2LOADER_H
#define OPENMW_COMPONENTS_RESOURCE_KTX2LOADER_H

#include <iosfwd>
#include <string>

#include <osg/ref_ptr>

namespace osg
{
    class Image;
}

namespace Resource
{
    // Loads a 2D KTX2 texture into an osg::Image. Basis Universal payloads are
    // transcoded to BC1/BC3 on S3TC desktop GPUs, with RGBA8 fallback.
    osg::ref_ptr<osg::Image> loadKtx2Image(std::istream& stream, const std::string& filename, std::string& error);
}

#endif
