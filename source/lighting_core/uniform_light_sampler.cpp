#include "uniform_light_sampler.h"

#include "pxr/base/gf/vec3f.h"

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

std::optional<LightCandidate> UniformLightSampler::ProposeCandidate(
    const GfVec3f& hitPos,
    Rng&           rng) const
{
    if (_lights.empty()) {
        return std::nullopt;
    }

    const std::size_t lightIndex{
        std::min(static_cast<std::size_t>(rng.NextFloat() * _lights.size()), _lights.size() - 1u)};
    const ILight& light{*_lights[lightIndex]};
    const auto lightSample{light.SampleLight(hitPos, rng)};
    if (!lightSample.has_value()) {
        return std::nullopt;
    }

    const float lightSelectPdf{1.0f / static_cast<float>(_lights.size())};

    // Bake the light-selection probability into Ls.Pdf so EvaluateLightSample can use it directly.
    LightSample ls = *lightSample;
    ls.Pdf.value *= lightSelectPdf;

    return LightCandidate{
        .Light      = &light,
        .LightIndex = static_cast<int>(lightIndex),
        .Ls         = ls,
        .Pdf        = ls.Pdf,
    };
}

}  // namespace Restir