#pragma once

#include "clonable.h"
#include "light_interface.h"
#include "pxr/base/gf/vec3f.h"
#include "rng.h"

#include <gsl/gsl>
#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    /**
     * One RIS candidate proposed by a light sampler.
     */
    struct LightCandidate
    {
        gsl::not_null<const ILight *> Light;
        LightSample                   Ls;
        Pdf                           Pdf;
    };

    class ILightSampler : public IClonableAs<ILightSampler>
    {
      public:
        virtual ~ILightSampler() = default;

        [[nodiscard]] virtual std::optional<LightCandidate> ProposeCandidate(const GfVec3f &hitPos, Rng &rng) const = 0;

        // Probability of choosing this light before sampling a point/direction on it.
        [[nodiscard]] virtual float EvalPdf(const ILight &light) const = 0;
    };

} // namespace Restir
