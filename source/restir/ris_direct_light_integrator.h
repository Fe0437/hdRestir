#pragma once

#include "clonable.h"
#include "direct_light_integrator_factory.h"
#include "direct_light_integrator_interface.h"
#include "lighting_core/light_sampler.h"
#include "not_null_unique_ptr.h"
#include "scene_interface.h"
#include "spectrum.h"

#include <optional>

namespace Restir
{

    class RisDirectLightIntegrator final : public IDirectLightIntegrator, public IClonableAs<IDirectLightIntegrator>
    {

        enum class CandidateOrigin : uint8_t
        {
            NEE,
            SkyNEE,
            BSDF
        };

        struct RISLightCandidate
        {
            std::optional<LightCandidate> Candidate{};
            CandidateOrigin               Origin{CandidateOrigin::NEE};

            SampledSpectrum Throughput{}; // L × bsdf × nDotL — updated in-place on temporal reuse
            double          risWeight{0.0};
            double          targetFunction{0.0};
        };

      public:
        using Candidate = RISLightCandidate;

        explicit RisDirectLightIntegrator(NotNullUniquePtr<ILightSampler> &&sampler, int candidateCount,
                                          bool useReservoir = true)
            : _sampler{std::move(sampler)}, _candidateCount{candidateCount}, _useReservoir{useReservoir}
        {
        }

        [[nodiscard]] std::unique_ptr<IDirectLightIntegrator> CloneAs() const override
        {
            return std::make_unique<RisDirectLightIntegrator>(NotNullUniquePtr<ILightSampler>{_sampler->CloneAs()},
                                                              _candidateCount, _useReservoir);
        }

        // Top-level call: computes shading from hit when shadingPoint is absent.
        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda, IBufferProvider &provider,
                                         CallIndex callId) const override;

        // Per-bounce call: isect.shadingPoint must be populated.
        // callId.id < callId.stride iff this is the primary hit — only then is the reservoir written.
        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda, IBufferProvider &provider,
                                         const std::optional<BsdfBounceConnection> &bsdfConnection,
                                         CallIndex                                  callId) const override;

        [[nodiscard]] static NotNullUniquePtr<IDirectLightIntegratorFactory> MakeFactory(int  candidateCount = 16,
                                                                                         bool useReservoir   = true);

      private:
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

        struct SamplingStrategy
        {
            const ILightSampler &sampler;
            int                  nNeeSampler;
            int                  nNeeSky;
            int                  nBsdf;
        };

        struct MISContrib
        {
            std::optional<HitRecord> ShadowHit;
            SampledSpectrum          Emission{};
            SampledSpectrum          ThroughputMul{};
            float                    PNee{0.0f};
            float                    PBsdf{0.0f};
            bool                     UseMis{false};
        };

        // Fresh evaluation: fires a shadow ray, computes PNee and PBsdf for MIS.
        // Used when generating NEE candidates each frame.
        [[nodiscard]] static MISContrib _evaluateLightSample(const RayIntersection &isect, const ILight &light,
                                                             const LightSample &ls, const IScene &scene);

        // Temporal re-evaluation: no shadow ray (stored visibility accepted).
        // Returns L × bsdf × nDotL as a spectrum — the unweighted target function p̂.
        // Used during reservoir temporal merge to compute p̂_curr for the previous frame's sample.
        [[nodiscard]] static SampledSpectrum _reEvaluateForTemporalReuse(const RayIntersection &isect,
                                                                         const LightCandidate  &candidate);

        [[nodiscard]] static MISContrib _evaluateBSDFConnection(const RayIntersection      &isect,
                                                                const BsdfBounceConnection &connection,
                                                                const IScene               &scene,
                                                                const SamplingStrategy     &samplingStrategy);

        [[nodiscard]] static std::vector<OptionalRisLightCandidate>
        _generateBSDFSamplingCandidates(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                        const std::optional<BsdfBounceConnection> &bsdfConnection,
                                        const SamplingStrategy                    &samplingStrategy);

        // nSampler / nSky: how many candidates to draw from each NEE technique.
        // nBsdf: how many BSDF candidates will be in the pool (used for MIS balance).
        [[nodiscard]] static std::vector<OptionalRisLightCandidate>
        _generateNEECandidates(const RayIntersection &isect, const IScene &scene, Rng &rng,
                               const SamplingStrategy &samplingStrategy);

        NotNullUniquePtr<ILightSampler> _sampler;
        int                             _candidateCount;
        bool                            _useReservoir;
    };

} // namespace Restir
