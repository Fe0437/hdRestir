#include "dome_light.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hio/image.h"
#include "shading_helpers.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    DomeLight::DomeLight(const LightParams &params) noexcept : _params{params} {}

    void DomeLight::Prepare()
    {
        SdfAssetPath texPath = _params.TextureFile;
        if (texPath == _loadedPath)
            return;
        _loadedPath = texPath;
        _pixels.clear();
        _width = _height = 0;
        _totalLuminance  = 0.0f;
        _rowCdf.clear();
        _colCdf.clear();

        if (texPath.GetAssetPath().empty())
            return;
        HioImageSharedPtr image = HioImage::OpenForReading(texPath.GetResolvedPath());
        if (!image)
            return;

        _width  = image->GetWidth();
        _height = image->GetHeight();
        _pixels.assign((size_t)_width * _height * 3, 0.0f);
        HioImage::StorageSpec spec;
        spec.format = HioFormatFloat32Vec3;
        spec.width  = _width;
        spec.height = _height;
        spec.data   = _pixels.data();
        image->Read(spec);

        _rowCdf.assign(_height + 1, 0.0f);
        _colCdf.assign((size_t)_height * (_width + 1), 0.0f);

        for (int y = 0; y < _height; ++y)
        {
            float rowLuminance = 0.0f;
            float sinTheta     = std::sin(M_PI * (float)(y + 0.5f) / (float)_height);
            for (int x = 0; x < _width; ++x)
            {
                size_t idx = (size_t)(y * _width + x) * 3;
                float  lum = 0.2126f * _pixels[idx] + 0.7152f * _pixels[idx + 1] + 0.0722f * _pixels[idx + 2];
                lum *= sinTheta;
                rowLuminance += lum;
                _colCdf[y * (_width + 1) + x + 1] = _colCdf[y * (_width + 1) + x] + lum;
            }
            if (rowLuminance > 0)
            {
                for (int x = 0; x <= _width; ++x)
                    _colCdf[y * (_width + 1) + x] /= rowLuminance;
            }
            _totalLuminance += rowLuminance;
            _rowCdf[y + 1] = _rowCdf[y] + rowLuminance;
        }
        if (_totalLuminance > 0)
        {
            for (int y = 0; y <= _height; ++y)
                _rowCdf[y] /= _totalLuminance;
        }
    }

    std::optional<LightSample> DomeLight::SampleLight(const GfVec3f & /*hitPos*/, Rng &rng) const
    {
        if (_pixels.empty())
            return std::nullopt;

        float        u1     = rng.NextFloat();
        float        u2     = rng.NextFloat();
        auto         itY    = std::lower_bound(_rowCdf.begin(), _rowCdf.end(), u1);
        int          y      = std::clamp((int)std::distance(_rowCdf.begin(), itY) - 1, 0, _height - 1);
        const float *colCdf = &_colCdf[y * (_width + 1)];
        auto         itX    = std::lower_bound(colCdf, colCdf + _width + 1, u2);
        int          x      = std::clamp((int)std::distance(colCdf, itX) - 1, 0, _width - 1);

        float   theta = M_PI * (float)(y + 0.5f) / (float)_height;
        float   phi   = 2.0f * M_PI * (float)(x + 0.5f) / (float)_width;
        GfVec3f lDir{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};

        size_t  idx = (size_t)(y * _width + x) * 3;
        GfVec3f texColor{_pixels[idx], _pixels[idx + 1], _pixels[idx + 2]};
        GfVec3f lColor = GfCompMult(texColor, _params.Color) * _params.EffectiveIntensity();

        float lightPdf = 1.0f;
        if (_totalLuminance > 0)
        {
            float lum = 0.2126f * texColor[0] + 0.7152f * texColor[1] + 0.0722f * texColor[2];
            lightPdf  = std::max(
                lum / (_totalLuminance * ((float)M_PI / (float)_height) * (2.0f * (float)M_PI / (float)_width)), 1e-6f);
        }

        if (lColor[0] <= 0.0f && lColor[1] <= 0.0f && lColor[2] <= 0.0f)
            return std::nullopt;
        return LightSample{lDir, lColor, -lDir, 1e30f, Pdf{lightPdf, PdfSpace::SolidAngle}};
    }

    Pdf DomeLight::EvalPdf(const GfVec3f & /*hitPos*/, const GfVec3f &dir, float /*dist*/,
                           const GfVec3f & /*lightNormal*/) const
    {
        return {EvalPdf(dir), PdfSpace::SolidAngle};
    }

    float DomeLight::EvalPdf(const GfVec3f &dir) const
    {
        if (_pixels.empty() || _totalLuminance <= 0.f)
            return 0.f;
        float theta = std::acos(std::clamp(dir[1], -1.0f, 1.0f));
        float phi   = std::atan2(dir[2], dir[0]);
        if (phi < 0)
            phi += 2.0f * M_PI;
        int    x   = std::clamp(int(phi / (2.0f * M_PI) * _width), 0, _width - 1);
        int    y   = std::clamp(int(theta / M_PI * _height), 0, _height - 1);
        size_t idx = (size_t)(y * _width + x) * 3;
        float  lum = 0.2126f * _pixels[idx] + 0.7152f * _pixels[idx + 1] + 0.0722f * _pixels[idx + 2];
        return std::max(lum / (_totalLuminance * ((float)M_PI / (float)_height) * (2.0f * (float)M_PI / (float)_width)),
                        1e-6f);
    }

    GfVec3f DomeLight::Sample(const GfVec3f &rayDir) const
    {
        if (_pixels.empty())
            return GfVec3f{0.0f};
        float theta = std::acos(std::clamp(rayDir[1], -1.0f, 1.0f));
        float phi   = std::atan2(rayDir[2], rayDir[0]);
        if (phi < 0)
            phi += 2.0f * M_PI;
        int     x   = std::clamp(int(phi / (2.0f * M_PI) * _width), 0, _width - 1);
        int     y   = std::clamp(int(theta / M_PI * _height), 0, _height - 1);
        size_t  idx = (size_t)(y * _width + x) * 3;
        GfVec3f color{_pixels[idx], _pixels[idx + 1], _pixels[idx + 2]};
        return GfCompMult(color, _params.Color) * _params.EffectiveIntensity();
    }

} // namespace Restir
