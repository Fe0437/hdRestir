#include "image_texture_sampler.h"

#include "debug.h"

#include "pxr/imaging/hio/image.h"

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

const detail::Texture*
ImageTextureSamplerFactory::_getOrLoad(std::string_view resolvedPath, bool linear) const
{
    if (resolvedPath.empty()) return nullptr;

    std::string cacheKey{resolvedPath};
    cacheKey += linear ? "_lin" : "_srgb";

    {
        std::lock_guard<std::mutex> lock{_mutex};
        auto it = _cache.find(cacheKey);
        if (it != _cache.end()) return &it->second;
    }

    // Not in cache — hold lock for loading to prevent redundant loads.
    std::lock_guard<std::mutex> lock{_mutex};
    auto it = _cache.find(cacheKey);
    if (it != _cache.end()) return &it->second;

    DBG_LOG("  Loading texture: %.*s", static_cast<int>(resolvedPath.size()), resolvedPath.data());
    HioImageSharedPtr image = HioImage::OpenForReading(std::string{resolvedPath});
    if (!image) {
        DBG_LOG("  Failed to open texture: %.*s",
                static_cast<int>(resolvedPath.size()), resolvedPath.data());
        _cache[cacheKey] = detail::Texture{};
        return &_cache[cacheKey];
    }

    detail::Texture data;
    data.width  = image->GetWidth();
    data.height = image->GetHeight();
    DBG_LOG("  Texture loaded: %dx%d", data.width, data.height);
    data.pixels.assign(data.width * data.height * 3, 0.0f);

    HioFormat format   = image->GetFormat();
    int       channels = HioGetComponentCount(format);
    bool isFloat = (format == HioFormatFloat32     || format == HioFormatFloat32Vec2 ||
                    format == HioFormatFloat32Vec3  || format == HioFormatFloat32Vec4 ||
                    format == HioFormatFloat16      || format == HioFormatFloat16Vec2 ||
                    format == HioFormatFloat16Vec3  || format == HioFormatFloat16Vec4);
    bool isOriginalSrgb = (format == HioFormatUNorm8srgb     || format == HioFormatUNorm8Vec2srgb ||
                           format == HioFormatUNorm8Vec3srgb  || format == HioFormatUNorm8Vec4srgb);
    bool applySrgb = isOriginalSrgb && !linear;

    HioImage::StorageSpec spec;
    spec.format = format;
    spec.width  = data.width;
    spec.height = data.height;

    if (isFloat) {
        spec.format = channels == 4 ? HioFormatFloat32Vec4 :
                     (channels == 3 ? HioFormatFloat32Vec3 :
                     (channels == 2 ? HioFormatFloat32Vec2 : HioFormatFloat32));
        std::vector<float> rawData(data.width * data.height * channels);
        spec.data = rawData.data();
        if (!image->Read(spec)) {
            DBG_LOG("  Failed to read texture pixels (float): %.*s",
                    static_cast<int>(resolvedPath.size()), resolvedPath.data());
        } else {
            for (int i = 0; i < data.width * data.height; ++i) {
                data.pixels[i*3+0] = rawData[i*channels+0];
                data.pixels[i*3+1] = channels > 1 ? rawData[i*channels+1] : rawData[i*channels+0];
                data.pixels[i*3+2] = channels > 2 ? rawData[i*channels+2] : rawData[i*channels+0];
            }
        }
    } else {
        spec.format = channels == 4 ? (isOriginalSrgb ? HioFormatUNorm8Vec4srgb : HioFormatUNorm8Vec4) :
                     (channels == 3 ? (isOriginalSrgb ? HioFormatUNorm8Vec3srgb : HioFormatUNorm8Vec3) :
                     (channels == 2 ? (isOriginalSrgb ? HioFormatUNorm8Vec2srgb : HioFormatUNorm8Vec2) :
                     (isOriginalSrgb ? HioFormatUNorm8srgb : HioFormatUNorm8)));
        std::vector<unsigned char> rawData(data.width * data.height * channels);
        spec.data = rawData.data();
        if (!image->Read(spec)) {
            DBG_LOG("  Failed to read texture pixels (unorm8): %.*s",
                    static_cast<int>(resolvedPath.size()), resolvedPath.data());
        } else {
            for (int i = 0; i < data.width * data.height; ++i) {
                float r = rawData[i*channels+0] / 255.0f;
                float g = channels > 1 ? rawData[i*channels+1] / 255.0f : r;
                float b = channels > 2 ? rawData[i*channels+2] / 255.0f : r;
                if (applySrgb) {
                    r = std::pow(r, 2.2f);
                    g = std::pow(g, 2.2f);
                    b = std::pow(b, 2.2f);
                }
                data.pixels[i*3+0] = r;
                data.pixels[i*3+1] = g;
                data.pixels[i*3+2] = b;
            }
        }
    }

    _cache[cacheKey] = std::move(data);
    return &_cache[cacheKey];
}

}  // namespace Restir
