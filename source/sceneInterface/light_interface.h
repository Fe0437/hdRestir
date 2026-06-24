#pragma once

#include "light_params.h"
#include "math/pdf.h"
#include "pxr/base/gf/vec3f.h"
#include "rng.h"

#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct LightSample
    {
        GfVec3f Dir{};
        GfVec3f Color{};
        GfVec3f LightNormal{}; // outward normal of the light surface at the sampled point
        float   Dist{1e30f};
        Pdf     Pdf{}; // combined (selection × sample) pdf in the light's native space
    };

    class ILight
    {
      public:
        virtual ~ILight() = default;

        virtual void SetParams(const LightParams &params) = 0;

        // Called once when the light is added or its parameters change.
        // Expensive work (texture loading, CDF building) goes here.
        // Default is a no-op; only DomeLight overrides.
        virtual void Prepare() {}

        // True for delta lights (directional, point with zero radius) — MIS weight = 1.
        [[nodiscard]] virtual bool IsDeltaLight() const noexcept
        {
            return false;
        }

        [[nodiscard]] virtual std::optional<LightSample> SampleLight(const GfVec3f &hitPos, Rng &rng) const = 0;

        // Cross-evaluation: area Pdf that SampleLight would assign to the surface point hit
        // by (hitPos, dir) at distance dist with outward normal lightNormal.
        // Returns {0, Area} for delta lights or unreachable directions.
        [[nodiscard]] virtual Pdf EvalPdf(const GfVec3f &hitPos, const GfVec3f &dir, float dist,
                                          const GfVec3f &lightNormal) const = 0;
    };

} // namespace Restir
