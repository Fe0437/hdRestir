#pragma once

#include "clonable.h"
#include "light_interface.h"
#include "rng.h"

#include "pxr/base/gf/vec3f.h"

#include <optional>
#include <gsl/gsl>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

/**
 * One RIS candidate proposed by a light sampler.
 */
struct LightCandidate {
    gsl::not_null<const ILight*> Light;
    int           LightIndex;
    LightSample   Ls;
    Pdf           Pdf;
};

class ILightSampler : public IClonableAs<ILightSampler> {
public:
    virtual ~ILightSampler() = default;

    [[nodiscard]] virtual std::optional<LightCandidate> ProposeCandidate(
        const GfVec3f& hitPos,
        Rng&           rng) const = 0;

    // Returns true if this sampler already includes the sky light in its proposals,
    // so callers should NOT add a separate sky-light contribution.
    [[nodiscard]] virtual bool IsConsideringSkyLight() const noexcept { return false; }
};

}  // namespace Restir