#include "uniform_light_sampler.h"

#include "pxr/base/gf/vec3f.h"

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    UniformLightSampler::UniformLightSampler(gsl::span<ILight *const> lights)
        : _lights{lights}, _lightSelectPdf{lights.empty() ? 0.0f : 1.0f / static_cast<float>(lights.size())}
    {
        _lightSet.reserve(lights.size());
        for (const ILight *light : lights)
        {
            _lightSet.insert(light);
        }
    }

    std::optional<LightCandidate> UniformLightSampler::ProposeCandidate(const GfVec3f &hitPos, Rng &rng) const
    {
        if (_lights.empty())
        {
            return std::nullopt;
        }

        const std::size_t lightIndex{
            std::min(static_cast<std::size_t>(rng.NextFloat() * _lights.size()), _lights.size() - 1u)};
        const ILight &light{*_lights[lightIndex]};
        const auto    lightSample{light.SampleLight(hitPos, rng)};
        if (!lightSample.has_value())
        {
            return std::nullopt;
        }

        // Bake the light-selection probability into Ls.Pdf so EvaluateLightSample can use it directly.
        LightSample ls = *lightSample;
        ls.Pdf.value *= _lightSelectPdf;

        return LightCandidate{
            .Light = &light,
            .Ls    = ls,
            .Pdf   = ls.Pdf,
        };
    }

    float UniformLightSampler::EvalPdf(const ILight &light) const
    {
        return _lightSet.contains(&light) ? _lightSelectPdf : 0.0f;
    }

} // namespace Restir
