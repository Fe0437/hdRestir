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

            // Whether Throughput already reflects a real shadow ray (or, for BSDF-origin
            // candidates, a real traced bounce ray — visibility is inherent to the hit test).
            // False only for a skipVisibility-generated NEE/SkyNEE candidate that hasn't won a
            // resampling yet. Persists on the candidate itself (including across temporal reuse
            // in the reservoir), so a candidate is only ever shadow-ray-corrected once, from the
            // shading point where it was actually selected — never re-tested later against a
            // different (possibly stale) shading point.
            bool VisibilityTested{true};
        };

      public:
        using Candidate = RISLightCandidate;

        explicit RisDirectLightIntegrator(NotNullUniquePtr<ILightSampler> &&sampler, int candidateCount,
                                          bool useReservoir = true, bool skipVisibility = false)
            : _sampler{std::move(sampler)}, _candidateCount{candidateCount}, _useReservoir{useReservoir},
              _skipVisibility{skipVisibility}
        {
        }

        [[nodiscard]] std::unique_ptr<IDirectLightIntegrator> CloneAs() const override
        {
            return std::make_unique<RisDirectLightIntegrator>(NotNullUniquePtr<ILightSampler>{_sampler->CloneAs()},
                                                              _candidateCount, _useReservoir, _skipVisibility);
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

        [[nodiscard]] static NotNullUniquePtr<IDirectLightIntegratorFactory>
        MakeFactory(int candidateCount = 16, bool useReservoir = true, bool skipVisibility = false);

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
            bool                 skipVisibility{false};
        };

        struct MISContrib
        {
            std::optional<HitRecord> ShadowHit;
            SampledSpectrum          Emission{};
            SampledSpectrum          ThroughputMul{};
            float                    PNee{0.0f};
            float                    PBsdf{0.0f};
            bool                     UseMis{false};
            bool                     VisibilityTested{true}; // see RISLightCandidate::VisibilityTested
        };

        // Fresh evaluation: fires a shadow ray (unless skipVisibility is set, in which case the
        // sample is treated as always visible — cheaper target function for candidate *selection*,
        // for A/B perf testing; the returned VisibilityTested=false flags this up to the caller).
        // computes PNee and PBsdf for MIS. Used when generating NEE candidates each frame.
        [[nodiscard]] static MISContrib _evaluateLightSample(const RayIntersection &isect, const ILight &light,
                                                             const LightSample &ls, const IScene &scene,
                                                             bool skipVisibility);

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
        bool                            _skipVisibility;
    };

} // namespace Restir
