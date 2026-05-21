#pragma once

#include "ggx.h"
#include "material.h"

namespace Restir {

// Pure-Lambert fallback used for geometry with no USD material binding.
// Reads diffuse color from hit.Albedo (vertex / primvars:displayColor).
// Stateless — a single global instance is shared across all such prims.
class DefaultMaterial final : public IMaterial {
public:
    [[nodiscard]] static DefaultMaterial& Instance() noexcept;

    [[nodiscard]] BSDFClosure GetClosure(const HitRecord& hit) const override;
    [[nodiscard]] std::unique_ptr<IBSDF> CreateBSDF(BSDFClosure&& c) const override { return std::make_unique<GGXBsdf>(std::move(c)); }
    [[nodiscard]] BounceSample SampleBounce(
        const ShadingPoint& surface, const BounceConfig& config, BounceState& state, Rng& rng) const override;
};

}  // namespace Restir
