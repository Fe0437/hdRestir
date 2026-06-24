#include "point_light.h"

#include "pxr/base/gf/matrix4f.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    std::optional<LightSample> PointLight::SampleLight(const GfVec3f &hitPos, Rng & /*rng*/) const
    {
        GfVec3f lPos      = GfMatrix4f(_params.Transform).ExtractTranslation();
        GfVec3f toLight   = lPos - hitPos;
        float   lightDist = toLight.GetLength();
        GfVec3f lDir      = toLight / lightDist;
        GfVec3f lColor    = _params.Color * _params.EffectiveIntensity();

        float coneAngle = _params.ShapingConeAngle;
        if (coneAngle < 180.0f)
        {
            GfVec3f lNormal      = GfMatrix4f(_params.Transform).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
            float   cosTheta     = GfDot(lNormal, -lDir);
            float   coneAngleRad = coneAngle * (float)(M_PI / 180.0);
            float   cosConeAngle = std::cos(coneAngleRad);
            if (cosTheta <= cosConeAngle)
            {
                lColor = GfVec3f(0.0f);
            }
            else
            {
                float softness = _params.ShapingConeSoftness;
                if (softness > 0.0f)
                {
                    float innerAngleRad = coneAngleRad * (1.0f - softness);
                    float cosInnerAngle = std::cos(innerAngleRad);
                    if (cosTheta < cosInnerAngle)
                    {
                        float factor = (cosTheta - cosConeAngle) / (cosInnerAngle - cosConeAngle);
                        factor       = factor * factor * (3.0f - 2.0f * factor);
                        lColor *= factor;
                    }
                }
            }
        }

        if (lColor[0] <= 0.0f && lColor[1] <= 0.0f && lColor[2] <= 0.0f)
            return std::nullopt;
        return LightSample{lDir, lColor, -lDir, lightDist, Pdf{1.f, PdfSpace::SolidAngle}};
    }

    Pdf PointLight::EvalPdf(const GfVec3f & /*hitPos*/, const GfVec3f & /*dir*/, float /*dist*/,
                            const GfVec3f & /*lightNormal*/) const
    {
        return {0.f, PdfSpace::Area};
    }

} // namespace Restir
