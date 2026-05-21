#include "distant_light.h"

#include "pxr/base/gf/matrix4f.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

std::optional<LightSample> DistantLight::SampleLight(const GfVec3f& /*hitPos*/, Rng& /*rng*/) const
{
    GfVec3f lDir   = GfMatrix4f(_params.Transform).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
    GfVec3f lColor = _params.Color * _params.EffectiveIntensity();
    if (lColor[0] <= 0.0f && lColor[1] <= 0.0f && lColor[2] <= 0.0f) return std::nullopt;
    return LightSample{lDir, lColor, 1e30f, 1.0f};
}

}  // namespace Restir
