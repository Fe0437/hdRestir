#pragma once

#include "ggx.h"
#include "image_texture_sampler.h"
#include "material.h"
#include "preview_surface_params.h"

#include <gsl/gsl>

namespace Restir {

class PreviewSurfaceMaterial final : public IMaterial {
public:
    explicit PreviewSurfaceMaterial(
        const PreviewSurfaceParams&                   params,
        gsl::not_null<const ImageTextureSamplerFactory*> texFact) noexcept
        : _params{params}, _texFact{texFact}
    {}

    void SetParams(const PreviewSurfaceParams& params) {
        _params = params;
    }

    [[nodiscard]] BSDFClosure GetClosure(const HitRecord& hit) const override;
    [[nodiscard]] std::unique_ptr<IBSDF> CreateBSDF(BSDFClosure&& c) const override { return std::make_unique<GGXBsdf>(std::move(c)); }
    [[nodiscard]] BounceSample SampleBounce(
        const ShadingPoint& surface, const BounceConfig& config, BounceState& state, Rng& rng) const override;

private:
    PreviewSurfaceParams                          _params;
    gsl::not_null<const ImageTextureSamplerFactory*> _texFact;
};

}  // namespace Restir
