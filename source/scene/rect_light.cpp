#include "rect_light.h"

#include "shading_helpers.h"

#include "pxr/base/gf/matrix4f.h"

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

std::optional<LightSample> RectLight::SampleLight(const GfVec3f& hitPos, Rng& rng) const
{
    float u = rng.NextFloat() - 0.5f;
    float v = rng.NextFloat() - 0.5f;
    GfVec3f lPosLocal{u * _params.Width, v * _params.Height, 0.0f};
    GfVec3f lPosWorld = GfMatrix4f(_params.Transform).Transform(lPosLocal);
    GfVec3f toLight   = lPosWorld - hitPos;
    float lightDist   = toLight.GetLength();
    GfVec3f lDir      = toLight / lightDist;

    float area = _params.Width * _params.Height;
    if (area <= 0.0f) return std::nullopt;

    GfVec3f lNormal = GfMatrix4f(_params.Transform).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
    float cosThetaL = std::max(0.0f, GfDot(lNormal, -lDir));
    if (cosThetaL <= 0.0f) return std::nullopt;

    GfVec3f lColor = _params.Color * _params.EffectiveIntensity();
    if (lColor[0] <= 0.0f && lColor[1] <= 0.0f && lColor[2] <= 0.0f) return std::nullopt;
    return LightSample{lDir, lColor, lNormal, lightDist, Pdf{1.f / area, PdfSpace::Area}};
}

Pdf RectLight::EvalPdf(const GfVec3f& /*hitPos*/, const GfVec3f& /*dir*/,
                        float /*dist*/, const GfVec3f& /*lightNormal*/) const
{
    float area = _params.Width * _params.Height;
    if (area <= 0.f) return {0.f, PdfSpace::Area};
    return {1.f / area, PdfSpace::Area};
}

}  // namespace Restir
