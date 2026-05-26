#pragma once

#include "light_sampler.h"

#include <gsl/gsl>

namespace Restir {

class UniformLightSampler final : public ILightSampler {
public:
    explicit UniformLightSampler(gsl::span<ILight* const> lights) noexcept
        : _lights{lights}
    {}

    [[nodiscard]] std::unique_ptr<ILightSampler> CloneAs() const override
    {
        return std::make_unique<UniformLightSampler>(_lights);
    }

    [[nodiscard]] std::optional<LightCandidate> ProposeCandidate(
        const GfVec3f& hitPos,
        Rng&           rng) const override;

private:
    gsl::span<ILight* const> _lights;
};

}  // namespace Restir