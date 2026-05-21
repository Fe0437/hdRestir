#include "direct_lighting.h"

#include "shading_helpers.h"

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

namespace {

// Evaluate one light sample against the surface and accumulate radiance.
// Works for any ILight — regular area/point/distant lights and the sky sun.
[[nodiscard]] SampledSpectrum EvaluateLightSample(
    const ShadingPoint& surface,
    const ILight&       light,
    const LightSample&  ls,
    float               lightSelectPdf,
    const IScene&       scene)
{
    const GfVec3f& shadingNormal{surface.shadingNormal};
    const GfVec3f& hit          {surface.hit.Position};

    const float nDotL{GfDot(shadingNormal, ls.Dir)};
    if (nDotL <= 0.0f) return SampledSpectrum{0.0f};

    const GfVec3f shadowOrigin{hit + shadingNormal * 1e-4f};
    const auto    shadowHit   {scene.IntersectScene(shadowOrigin, ls.Dir)};
    if (shadowHit && shadowHit->Depth < ls.Dist - 1e-3f) return SampledSpectrum{0.0f};

    const GfVec3f wo     {-surface.rayDir};
    const GfVec3f bsdfVal{surface.bsdf.Eval(shadingNormal, wo, ls.Dir)};
    const float   bsdfPdf{surface.bsdf.Pdf (shadingNormal, wo, ls.Dir)};

    const float totalLightPdf{lightSelectPdf * ls.Pdf};
    const float misWeight    {light.IsDeltaLight() ? 1.0f : PowerHeuristic(totalLightPdf, bsdfPdf)};

    return RGBToSpectrum(bsdfVal, surface.lambda)
         * RGBToSpectrum(ls.Color, surface.lambda)
         * (nDotL / (totalLightPdf + 1e-6f))
         * misWeight;
}

}  // namespace

SampledSpectrum SampleDirectLighting(
    const ShadingPoint&      surface,
    gsl::span<ILight* const> lights,
    const IScene&            scene,
    Rng&                     rng)
{
    SampledSpectrum totalRadiance{0.0f};

    // --- Regular lights (area / point / dome) ---
    if (!lights.empty()) {
        const size_t  lightIdx      {std::min(static_cast<size_t>(rng.NextFloat() * lights.size()),
                                              lights.size() - 1u)};
        const float   lightSelectPdf{1.0f / static_cast<float>(lights.size())};
        const ILight& light         {*lights[lightIdx]};

        const auto ls{light.SampleLight(surface.hit.Position, rng)};
        if (ls.has_value()) {
            totalRadiance += EvaluateLightSample(surface, light, *ls, lightSelectPdf, scene);
        }
    }

    // --- Sky sun (delta light, always Pdf = 1) ---
    const ILight* skyLight{scene.GetSkyLight()};
    if (skyLight) {
        const auto ls{skyLight->SampleLight(surface.hit.Position, rng)};
        if (ls.has_value()) {
            totalRadiance += EvaluateLightSample(surface, *skyLight, *ls, 1.0f, scene);
        }
    }

    return totalRadiance;
}

}  // namespace Restir
