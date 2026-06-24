#pragma once

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "texture_sampler.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace detail
    {
        // Owns the decoded pixel buffer for one resolved path + linear/srgb variant.
        struct Texture
        {
            std::vector<float> pixels;
            int                width{0};
            int                height{0};
        };
    } // namespace detail

    // BoundTextureSampler<T, Linear>
    // A lightweight, copyable view into a factory-owned Texture.
    // T      — return type of Sample(): GfVec3f for colour/normal, float for scalar channels.
    // Linear — compile-time tag: true if pixels were loaded in linear colour space.
    template <typename T, bool Linear = false> class BoundTextureSampler
    {
      public:
        explicit BoundTextureSampler(const detail::Texture *data) noexcept : _data{data} {}

        [[nodiscard]] T Sample(GfVec2f uv) const
        {
            if (!_data || _data->pixels.empty())
                return _default();

            const float u  = uv[0] - std::floor(uv[0]);
            const float v  = 1.0f - (uv[1] - std::floor(uv[1]));
            const float px = u * static_cast<float>(_data->width - 1);
            const float py = v * static_cast<float>(_data->height - 1);
            const int   x0 = static_cast<int>(std::floor(px));
            const int   y0 = static_cast<int>(std::floor(py));
            const int   x1 = std::min(x0 + 1, _data->width - 1);
            const int   y1 = std::min(y0 + 1, _data->height - 1);
            const float fx = px - static_cast<float>(x0);
            const float fy = py - static_cast<float>(y0);

            auto px3 = [&](int x, int y) noexcept -> GfVec3f
            {
                const size_t idx = static_cast<size_t>((y * _data->width + x) * 3);
                return {_data->pixels[idx], _data->pixels[idx + 1], _data->pixels[idx + 2]};
            };

            const GfVec3f rgb = (px3(x0, y0) * (1 - fx) + px3(x1, y0) * fx) * (1 - fy) +
                                (px3(x0, y1) * (1 - fx) + px3(x1, y1) * fx) * fy;
            return _extract(rgb);
        }

      private:
        [[nodiscard]] static T _default() noexcept
        {
            if constexpr (std::same_as<T, float>)
                return 1.0f;
            else
                return T{1.0f, 1.0f, 1.0f};
        }

        [[nodiscard]] static T _extract(const GfVec3f &rgb) noexcept
        {
            if constexpr (std::same_as<T, float>)
                return rgb[0];
            else
                return rgb;
        }

        const detail::Texture *_data;
    };

    static_assert(AnyTextureSampler<BoundTextureSampler<GfVec3f, false>>);
    static_assert(AnyTextureSampler<BoundTextureSampler<float, true>>);

    // ImageTextureSamplerFactory
    // Owns the texture cache. Create() loads on first use and returns a lightweight
    // BoundTextureSampler for repeated Sample() calls without further cache lookups.
    class ImageTextureSamplerFactory
    {
      public:
        template <typename T = GfVec3f, bool Linear = false>
        [[nodiscard]] BoundTextureSampler<T, Linear> Create(std::string_view resolvedPath) const
        {
            return BoundTextureSampler<T, Linear>{_getOrLoad(resolvedPath, Linear)};
        }

      private:
        [[nodiscard]] const detail::Texture *_getOrLoad(std::string_view resolvedPath, bool linear) const;

        mutable std::map<std::string, detail::Texture> _cache;
        mutable std::mutex                             _mutex;
    };

} // namespace Restir
