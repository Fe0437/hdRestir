#pragma once

#include "light_sampler.h"

#include <gsl/gsl>
#include <unordered_set>

namespace Restir
{

    class UniformLightSampler final : public ILightSampler
    {
      public:
        explicit UniformLightSampler(gsl::span<ILight *const> lights);

        [[nodiscard]] std::unique_ptr<ILightSampler> CloneAs() const override
        {
            return std::make_unique<UniformLightSampler>(_lights);
        }

        [[nodiscard]] std::optional<LightCandidate> ProposeCandidate(const GfVec3f &hitPos, Rng &rng) const override;

        [[nodiscard]] float EvalPdf(const ILight &light) const override;

      private:
        gsl::span<ILight *const>           _lights;
        std::unordered_set<const ILight *> _lightSet;
        float                              _lightSelectPdf{0.0f};
    };

} // namespace Restir
