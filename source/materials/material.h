#pragma once

#include "hit_record.h"
#include "pxr/base/gf/vec3f.h"
#include "rng.h"
#include "shading_types.h"

#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    class IMaterial
    {
      public:
        virtual ~IMaterial() = default;

        [[nodiscard]] virtual BSDFClosure GetClosure(const HitRecord &hit) const = 0;

        // Creates a stateful BSDF bound to the given closure, used for direct-lighting evaluation.
        [[nodiscard]] virtual std::unique_ptr<IBSDF> CreateBSDF(BSDFClosure &&c) const = 0;

        // Convenience: resolves the closure from hit, then creates the BSDF.
        [[nodiscard]] std::unique_ptr<IBSDF> CreateBSDF(const HitRecord &hit) const
        {
            return CreateBSDF(GetClosure(hit));
        }

        // Samples the next bounce direction. hitPos and rayDir are passed explicitly
        // because ShadingPoint no longer carries geometry state.
        [[nodiscard]] virtual BounceSampleResult SampleBounce(const ShadingPoint &surface, const GfVec3f &hitPos,
                                                              const GfVec3f &rayDir, const BounceConfig &config,
                                                              BounceState &state, Rng &rng) const = 0;
    };

} // namespace Restir
