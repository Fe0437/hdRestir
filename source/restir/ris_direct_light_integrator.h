#pragma once

#include "clonable.h"
#include "direct_light_integrator_interface.h"
#include "lighting_core/light_sampler.h"
#include "materials/material.h"
#include "not_null_unique_ptr.h"
#include "scene_interface.h"
#include "spectrum.h"

#include <optional>

namespace Restir
{

    class RisDirectLightIntegrator final : public IDirectLightIntegrator, public IClonableAs<IDirectLightIntegrator>
    {
      public:
        explicit RisDirectLightIntegrator(NotNullUniquePtr<ILightSampler> &&sampler, int candidateCount = 16)
            : _sampler{std::move(sampler)}, _candidateCount{candidateCount}
        {
        }

        [[nodiscard]] std::unique_ptr<IDirectLightIntegrator> CloneAs() const override
        {
            return std::make_unique<RisDirectLightIntegrator>(NotNullUniquePtr<ILightSampler>{_sampler->CloneAs()},
                                                              _candidateCount);
        }

        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda) const override;

        [[nodiscard]] SampledSpectrum Li(const ShadingPoint &surface, const IScene &scene, Rng &rng) const override;

        [[nodiscard]] SampledSpectrum Li(const ShadingPoint &surface, const IScene &scene, Rng &rng,
                                         const std::optional<BsdfBounceConnection> &bsdfConnection) const override;

      private:
        struct RISLightCandidate
        {
            std::optional<LightCandidate> Candidate{};
            SampledSpectrum               Emission{};
            SampledSpectrum               ThroupoutMult{};
            [[nodiscard]] double          GetWeight() const
            {
                return risWeight;
            }
            double risWeight{0.0};
            double targetFunction{0.0};
        };

        struct OptionalRisLightCandidate : std::optional<RISLightCandidate>
        {
            OptionalRisLightCandidate() = default;
            OptionalRisLightCandidate(std::nullopt_t) noexcept : std::optional<RISLightCandidate>{std::nullopt} {}
            OptionalRisLightCandidate(RISLightCandidate &&val) : std::optional<RISLightCandidate>{std::move(val)} {}
            OptionalRisLightCandidate(const RISLightCandidate &val) : std::optional<RISLightCandidate>{val} {}

            [[nodiscard]] double GetWeight() const
            {
                return this->has_value() ? this->value().risWeight : 0.0;
            }
        };

        struct MISContrib
        {
            std::optional<HitRecord> ShadowHit;
            SampledSpectrum          Emission{};
            SampledSpectrum          ThroughputIntegrandMul{}; // bsdf*cos — pure integrand, no pdf division
            float                    PNee{0.0f};
            float                    PBsdf{0.0f};
            bool                     UseMis{false};
        };

        [[nodiscard]] static MISContrib _evaluateNEE(const ShadingPoint &surface, const ILight &light,
                                                     const LightSample &lightSample, const IScene &scene);

        [[nodiscard]] static MISContrib _evaluateBSDFConnection(const ShadingPoint         &surface,
                                                                const BsdfBounceConnection &connection,
                                                                const IScene               &scene);

        [[nodiscard]] static std::vector<OptionalRisLightCandidate>
        _generateBSDFSamplingCandidates(const ShadingPoint &surface, const IScene &scene, Rng &rng, int candidateCount,
                                        const std::optional<BsdfBounceConnection> &bsdfConnection);

        [[nodiscard]] static std::vector<OptionalRisLightCandidate>
        _generateNEECandidates(const ShadingPoint &surface, const IScene &scene, Rng &rng, const ILightSampler &sampler,
                               int candidateCount, bool useBsdfTechnique);

        NotNullUniquePtr<ILightSampler> _sampler;
        int                             _candidateCount;
    };

} // namespace Restir
