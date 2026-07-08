#include "ris_direct_light_integrator.h"

#include "buffer_user.h"
#include "debug.h"
#include "frame_buffer.h"
#include "lighting_core/uniform_light_sampler.h"
#include "output_names.h"
#include "random_index.h"
#include "reservoir.h"
#include "shading_helpers.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        class RisDirectLightIntegratorFactory final : public IDirectLightIntegratorFactory, public IBufferStager
        {
          public:
            explicit RisDirectLightIntegratorFactory(int candidateCount, bool useReservoir, bool skipVisibility)
                : _candidateCount{candidateCount}, _useReservoir{useReservoir}, _skipVisibility{skipVisibility}
            {
            }

            [[nodiscard]] NotNullUniquePtr<IDirectLightIntegrator> Create(const IScene &scene,
                                                                          IBufferProvider & /*provider*/) const override
            {
                return NotNullUniquePtr<IDirectLightIntegrator>{std::make_unique<RisDirectLightIntegrator>(
                    NotNullUniquePtr<ILightSampler>{std::make_unique<UniformLightSampler>(scene.GetLights())},
                    _candidateCount, _useReservoir, _skipVisibility)};
            }

            [[nodiscard]] IBufferStager *GetBufferStager() override
            {
                return this;
            }

            void PrepareBuffers(IBufferProvider &provider, const IScene & /*scene*/) override
            {
                if (_useReservoir)
                {
                    static_cast<void>(provider.GetOrCreatePersistent(
                        kReservoirBufferName, sizeof(WeightedReservoir<RisDirectLightIntegrator::Candidate>)));
                }
            }

          private:
            int  _candidateCount;
            bool _useReservoir;
            bool _skipVisibility;
        };

    } // namespace

    NotNullUniquePtr<IDirectLightIntegratorFactory>
    RisDirectLightIntegrator::MakeFactory(int candidateCount, bool useReservoir, bool skipVisibility)
    {
        return NotNullUniquePtr<IDirectLightIntegratorFactory>{
            std::make_unique<RisDirectLightIntegratorFactory>(candidateCount, useReservoir, skipVisibility)};
    }

    SampledSpectrum RisDirectLightIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths & /*lambda*/, IBufferProvider &provider,
                                                 const std::optional<BsdfBounceConnection> &bsdfConnection,
                                                 CallIndex                                  callId) const
    {
        // Split _candidateCount over mutually exclusive proposal techniques:
        // light-sampler NEE, optional external sky NEE, and BSDF.
        // The environment is already an ILight; if the light sampler includes it, its full NEE pdf is
        // _sampler->EvalPdf(*envLight) * envLight->EvalPdf(dir), and no external sky technique is added.
        const IEnvironment *envLight{scene.GetEnvironment()};
        const float         skyLightSelectPdf{envLight != nullptr ? _sampler->EvalPdf(*envLight) : 0.0f};
        const int           nTech{envLight != nullptr && skyLightSelectPdf <= 0.0f ? 3 : 2};

        const int        nNeeSampler{std::max(1, _candidateCount / nTech)};
        const int        nNeeSky{envLight != nullptr && skyLightSelectPdf <= 0.0f ? _candidateCount / nTech : 0};
        const int        nBsdf{std::max(0, _candidateCount - nNeeSampler - nNeeSky)};
        SamplingStrategy samplingStrategy{*_sampler, nNeeSampler, nNeeSky, nBsdf, _skipVisibility};

        auto       allCandidates{_generateNEECandidates(isect, scene, rng, samplingStrategy)};
        const auto bsdfSamples{_generateBSDFSamplingCandidates(isect, scene, rng, bsdfConnection, samplingStrategy)};
        allCandidates.insert(allCandidates.end(), std::make_move_iterator(bsdfSamples.begin()),
                             std::make_move_iterator(bsdfSamples.end()));

        DBG_ASSERT(!allCandidates.empty(), "RIS expected at least one candidate");

        if (_useReservoir && callId.id < callId.stride)
        {
            using ReservoirT = WeightedReservoir<RISLightCandidate>;

            Scoped<ReservoirT> reservoir{static_cast<int>(allCandidates.size())};
            for (const auto &opt : allCandidates)
            {
                if (opt.has_value())
                {
                    reservoir->Update(*opt, static_cast<float>(opt->risWeight), rng);
                }
                else
                {
                    reservoir->CountMiss();
                }
            }

            FrameBuffer &reservoirBuf{provider.GetChecked(kReservoirBufferName)};
            ReservoirT   prevReservoir{reservoirBuf.As<ReservoirT>()[callId.id]};

            // Re-evaluate the previous sample under the current shading point and merge it.
            // No shadow ray — stored visibility is accepted (standard temporal-reuse approximation).
            if (prevReservoir.ChosenSample.has_value() && prevReservoir.ChosenSample->Candidate.has_value())
            {
                const LightCandidate &prevCandidate{*prevReservoir.ChosenSample->Candidate};
                const SampledSpectrum reeval{_reEvaluateForTemporalReuse(isect, prevCandidate)};
                const float           phatCurr{SpectrumLuminance(reeval, isect.shadingPoint->lambda)};
                const float           phatGen{static_cast<float>(prevReservoir.ChosenSample->targetFunction)};
                if (phatGen > 0.0f)
                {
                    // Update the sample to reflect the current frame before merging so
                    // that whatever Merge copies into ChosenSample is already correct.
                    prevReservoir.ChosenSample->targetFunction = static_cast<double>(phatCurr);
                    prevReservoir.ChosenSample->Throughput     = reeval;

                    const float mergeWeight{prevReservoir.WSum * (phatCurr / phatGen)};
                    reservoir->Merge(prevReservoir, mergeWeight, rng);
                }
            }

            ReservoirT finalized{reservoir.Release()};

            // The winner's shadow ray may have been skipped for cheap selection
            // (VisibilityTested=false); test it for real now, once, before it's stored — so
            // if this same sample is reused next frame, it's already trusted and correct.
            if (finalized.ChosenSample.has_value() && !finalized.ChosenSample->VisibilityTested &&
                finalized.ChosenSample->Candidate.has_value())
            {
                RISLightCandidate    &winner{*finalized.ChosenSample};
                const LightCandidate &lc{*winner.Candidate};
                const MISContrib      nee{_evaluateLightSample(isect, *lc.Light, lc.Ls, scene,
                                                               /*skipVisibility=*/false)};
                winner.Throughput       = nee.Emission * nee.ThroughputMul;
                winner.VisibilityTested = true;
            }

            reservoirBuf.As<ReservoirT>()[callId.id] = finalized;

            if (!finalized.ChosenSample.has_value() || finalized.W <= 0.0f)
            {
                return SampledSpectrum{};
            }

            return finalized.ChosenSample->Throughput * finalized.W;
        }

        // standard RIS.
        const auto chosen{RIS::RandomIndexFromWeights(gsl::span<const OptionalRisLightCandidate>{allCandidates}, rng)};
        if (!chosen.has_value())
        {
            return SampledSpectrum{};
        }

        const OptionalRisLightCandidate &chosenCandidateOptional{allCandidates[static_cast<size_t>(chosen->Index)]};
        if (!chosenCandidateOptional.has_value())
        {
            return SampledSpectrum{};
        }

        const RISLightCandidate &chosenCandidate{chosenCandidateOptional.value()};
        DBG_ASSERT(chosenCandidate.targetFunction > 0.0, "RIS chosen candidate must have positive target function");
        const float     W{static_cast<float>(chosen->WeightSum / chosenCandidate.targetFunction)};
        SampledSpectrum winnerThroughput{chosenCandidate.Throughput};

        // Same idea as the reservoir branch above: test the winner's visibility now if it
        // was skipped during selection (VisibilityTested=false).
        if (!chosenCandidate.VisibilityTested && chosenCandidate.Candidate.has_value())
        {
            const LightCandidate &lc{*chosenCandidate.Candidate};
            const MISContrib      nee{_evaluateLightSample(isect, *lc.Light, lc.Ls, scene, /*skipVisibility=*/false)};
            winnerThroughput = nee.Emission * nee.ThroughputMul;
        }

        return winnerThroughput * W;
    }

    SampledSpectrum RisDirectLightIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths &lambda, IBufferProvider &provider,
                                                 CallIndex callId) const
    {
        if (!isect.hit.has_value())
        {
            const IEnvironment *environment{scene.GetEnvironment()};
            return environment != nullptr ? RGBToSpectrum(environment->Sample(isect.ray.Dir), lambda)
                                          : SampledSpectrum{0.0f};
        }

        const HitRecord &hit{*isect.hit};
        const IMaterial &material{scene.GetMaterial(hit.MatId)};

        BSDFClosure     closure{material.GetClosure(hit)};
        const bool      isInside{GfDot(closure.Normal, isect.ray.Dir) > 0.0f};
        SampledSpectrum throughput{1.0f};
        BeerAbsorption(throughput, closure, hit.Depth, isInside, lambda);

        GfVec3f shadingNormal{closure.Normal};
        if (isInside)
        {
            shadingNormal = -shadingNormal;
        }

        const std::unique_ptr<IBSDF> bsdfOwner{material.CreateBSDF(BSDFClosure{closure})};
        const ShadingPoint           sp{*bsdfOwner, closure, shadingNormal, lambda, isInside};
        const RayIntersection        shadedIsect{isect.ray, isect.hit, sp};

        return throughput * (RGBToSpectrum(closure.Emission, lambda) +
                             Li(shadedIsect, scene, rng, lambda, provider, std::nullopt, callId));
    }

    [[maybe_unused]] std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate>
    RisDirectLightIntegrator::_generateBSDFSamplingCandidates(const RayIntersection &isect, const IScene &scene,
                                                              Rng                                       &rng,
                                                              const std::optional<BsdfBounceConnection> &bsdfConnection,
                                                              const SamplingStrategy &samplingStrategy)
    {
        const ShadingPoint &surface{*isect.shadingPoint};
        const GfVec3f      &hitPos{isect.hit->Position};

        std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> candidates{};
        candidates.reserve(static_cast<size_t>(std::max(0, samplingStrategy.nBsdf)));

        // connection already provided by the caller
        if (bsdfConnection.has_value())
        {
            const BsdfBounceConnection &connection{*bsdfConnection};
            if (connection.Bounce.BsdfPdf.value > 0.0f)
            {
                const auto            bsdfContrib{_evaluateBSDFConnection(isect, connection, scene, samplingStrategy)};
                const SampledSpectrum contribution{bsdfContrib.Emission * bsdfContrib.ThroughputMul};
                const double          targetFunction{SpectrumLuminance(contribution, surface.lambda)};
                float                 pNeeSampler{bsdfContrib.PNee};
                float                 pNeeSky{0.0f};
                if (!connection.Hit.has_value())
                {
                    const IEnvironment *environment{scene.GetEnvironment()};
                    const ILight *environmentLight{environment != nullptr ? dynamic_cast<const ILight *>(environment)
                                                                          : nullptr};
                    if (environmentLight == nullptr || samplingStrategy.sampler.EvalPdf(*environmentLight) <= 0.0f)
                    {
                        pNeeSampler = 0.0f;
                        pNeeSky     = bsdfContrib.PNee;
                    }
                }
                const double misWeight{PowerHeuristic(bsdfContrib.PBsdf, samplingStrategy.nBsdf, pNeeSampler,
                                                      samplingStrategy.nNeeSampler, pNeeSky, samplingStrategy.nNeeSky)};
                const double risWeight{misWeight * targetFunction / (bsdfContrib.PBsdf + 1e-9)};

                candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                    .Throughput     = contribution,
                    .risWeight      = risWeight,
                    .targetFunction = targetFunction,
                });
            }
        }

        const int candidatesToGenerate{samplingStrategy.nBsdf - static_cast<int>(candidates.size())};
        for (int i{0}; i < candidatesToGenerate; ++i)
        {
            const IMaterial                 &material{scene.GetMaterial(isect.hit->MatId)};
            BounceState                      bounceState{};
            BounceConfig                     bounceConfig{.effectiveMaxRefl = 1, .effectiveMaxRefr = 1};
            const BounceWithConnectionResult bounceResult{
                Detail::SampleBounceWithConnection(material, isect, bounceConfig, bounceState, scene, rng)};
            if (!std::holds_alternative<BsdfBounceConnection>(bounceResult))
            {
                candidates.push_back(std::nullopt);
                continue;
            }

            const BsdfBounceConnection &sampledConnection{std::get<BsdfBounceConnection>(bounceResult)};
            if (!sampledConnection.Hit.has_value())
            {
                const IEnvironment *environment{scene.GetEnvironment()};
                if (environment == nullptr)
                {
                    candidates.push_back(std::nullopt);
                    continue;
                }
                const SampledSpectrum &envThroughput{sampledConnection.Bounce.ThroughputIntegrandMul};
                const GfVec3f          wi{sampledConnection.Bounce.NextRay.Dir};
                const GfVec3f          envRGB{environment->Sample(wi)};
                const SampledSpectrum  envEmission{RGBToSpectrum(envRGB, surface.lambda)};
                const float            pBsdf{sampledConnection.Bounce.BsdfPdf.value};
                const ILight          *envAsLight{dynamic_cast<const ILight *>(environment)};
                const float  lightSelectPdf{envAsLight != nullptr ? samplingStrategy.sampler.EvalPdf(*envAsLight)
                                                                  : 0.0f};
                const float  envPdf{environment->EvalPdf(wi)};
                const float  pNeeSampler{lightSelectPdf * envPdf};
                const float  pNeeSky{lightSelectPdf <= 0.0f && samplingStrategy.nNeeSky > 0 ? envPdf : 0.0f};
                const float  pNee{pNeeSampler + pNeeSky};
                const double targetFunction{SpectrumLuminance(envEmission * envThroughput, surface.lambda)};
                const double misWeight{PowerHeuristic(pBsdf, samplingStrategy.nBsdf, pNeeSampler,
                                                      samplingStrategy.nNeeSampler, pNeeSky, samplingStrategy.nNeeSky)};
                const double risWeight{misWeight * targetFunction / (pBsdf + 1e-9)};

                // Both DomeLight and PhysicalSky implement ILight; dynamic_cast is always valid
                // when environment is non-null.  Only store a temporal candidate when pNee > 0
                // (i.e. the env has importance sampling — DomeLight yes, PhysicalSky background no).
                const std::optional<LightCandidate> envCandidate{
                    (pNee > 0.0f && envAsLight != nullptr)
                        ? std::optional<LightCandidate>{LightCandidate{
                              gsl::not_null<const ILight *>{envAsLight},
                              LightSample{
                                  .Dir         = wi,
                                  .Color       = envRGB,
                                  .LightNormal = -wi, // inward-facing, same convention as DomeLight::SampleLight
                                  .Dist        = 1e30f,
                                  .Pdf         = {pNee, PdfSpace::SolidAngle},
                              },
                              {}}}
                        : std::nullopt};

                candidates.push_back(RISLightCandidate{
                    .Candidate      = envCandidate,
                    .Origin         = CandidateOrigin::BSDF,
                    .Throughput     = envEmission * envThroughput,
                    .risWeight      = risWeight,
                    .targetFunction = targetFunction,
                });
                continue;
            }

            const ILight *light{scene.GetLightAtHit(*sampledConnection.Hit)};
            if (light == nullptr)
            {
                candidates.push_back(std::nullopt);
                continue;
            }

            const auto bsdfContrib{_evaluateBSDFConnection(isect, sampledConnection, scene, samplingStrategy)};
            const SampledSpectrum contribution{bsdfContrib.Emission * bsdfContrib.ThroughputMul};
            const double          targetFunction{SpectrumLuminance(contribution, surface.lambda)};
            const float           pNeeSky{0.0f};
            const double misWeight{PowerHeuristic(bsdfContrib.PBsdf, samplingStrategy.nBsdf, bsdfContrib.PNee,
                                                  samplingStrategy.nNeeSampler, pNeeSky, samplingStrategy.nNeeSky)};
            const double risWeight{misWeight * targetFunction / (bsdfContrib.PBsdf + 1e-9)};

            // Populate Ls.Pdf with the full light-sampler NEE pdf so temporal reuse can reconstruct
            // pNee identically to a generated NEE candidate.
            const GfVec3f wi{sampledConnection.Bounce.NextRay.Dir};
            const GfVec3f emissionRGB{
                scene.GetMaterial(sampledConnection.Hit->MatId).GetClosure(*sampledConnection.Hit).Emission};
            Pdf lightPdf{
                light->EvalPdf(isect.hit->Position, wi, sampledConnection.Hit->Depth, sampledConnection.Hit->Normal)};
            lightPdf.value *= samplingStrategy.sampler.EvalPdf(*light);

            candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                .Candidate      = LightCandidate{light,
                                                 LightSample{
                                                     .Dir         = wi,
                                                     .Color       = emissionRGB,
                                                     .LightNormal = sampledConnection.Hit->Normal,
                                                     .Dist        = sampledConnection.Hit->Depth,
                                                     .Pdf         = lightPdf,
                                                 },
                                                 {}},
                .Origin         = CandidateOrigin::BSDF,
                .Throughput     = contribution,
                .risWeight      = risWeight,
                .targetFunction = targetFunction,
            });
        }

        return candidates;
    }

    [[maybe_unused]] std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate>
    RisDirectLightIntegrator::_generateNEECandidates(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                     const SamplingStrategy &samplingStrategy)
    {
        const ShadingPoint &surface{*isect.shadingPoint};
        const GfVec3f      &hitPos{isect.hit->Position};

        std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> candidates{};
        candidates.reserve(static_cast<size_t>(samplingStrategy.nNeeSampler + samplingStrategy.nNeeSky));

        for (int i{0}; i < samplingStrategy.nNeeSampler; ++i)
        {
            const auto candidate{samplingStrategy.sampler.ProposeCandidate(hitPos, rng)};
            if (!candidate.has_value())
            {
                candidates.push_back(std::nullopt);
                continue;
            }

            const MISContrib nee{
                _evaluateLightSample(isect, *candidate->Light, candidate->Ls, scene, samplingStrategy.skipVisibility)};
            const SampledSpectrum contribution{nee.Emission * nee.ThroughputMul};
            const float           targetFunction{SpectrumLuminance(contribution, surface.lambda)};
            const float  misWeight{nee.UseMis
                                       ? PowerHeuristic(nee.PNee, samplingStrategy.nNeeSampler, 0.0f,
                                                        samplingStrategy.nNeeSky, nee.PBsdf, samplingStrategy.nBsdf)
                                       : 1.0f};
            const double risWeight{misWeight * targetFunction / (nee.PNee + 1e-9)};
            candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                .Candidate        = *candidate,
                .Origin           = CandidateOrigin::NEE,
                .Throughput       = contribution,
                .risWeight        = risWeight,
                .targetFunction   = targetFunction,
                .VisibilityTested = nee.VisibilityTested,
            });
        }

        if (samplingStrategy.nNeeSky > 0)
        {
            const IEnvironment *envLight{scene.GetEnvironment()};
            if (envLight != nullptr)
            {
                for (int i{0}; i < samplingStrategy.nNeeSky; ++i)
                {
                    const auto lightSample{envLight->SampleLight(hitPos, rng)};
                    if (!lightSample.has_value())
                    {
                        candidates.push_back(std::nullopt);
                        continue;
                    }
                    const MISContrib nee{
                        _evaluateLightSample(isect, *envLight, *lightSample, scene, samplingStrategy.skipVisibility)};
                    const SampledSpectrum contribution{nee.Emission * nee.ThroughputMul};
                    const float           targetFunction{SpectrumLuminance(contribution, surface.lambda)};
                    const float  misWeight{nee.UseMis ? PowerHeuristic(nee.PNee, samplingStrategy.nNeeSky, 0.0f,
                                                                       samplingStrategy.nNeeSampler, nee.PBsdf,
                                                                       samplingStrategy.nBsdf)
                                                      : 1.0f};
                    const double risWeight{misWeight * targetFunction / (nee.PNee + 1e-9)};
                    candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                        .Candidate        = LightCandidate{envLight, *lightSample, {}},
                        .Origin           = CandidateOrigin::SkyNEE,
                        .Throughput       = contribution,
                        .risWeight        = risWeight,
                        .targetFunction   = targetFunction,
                        .VisibilityTested = nee.VisibilityTested,
                    });
                }
            }
        }

        return candidates;
    }

    RisDirectLightIntegrator::MISContrib
    RisDirectLightIntegrator::_evaluateLightSample(const RayIntersection &isect, const ILight &light,
                                                   const LightSample &ls, const IScene &scene, bool skipVisibility)
    {
        const ShadingPoint &surface{*isect.shadingPoint};
        const GfVec3f       wo{-isect.ray.Dir};

        const float   nDotL{GfDot(surface.shadingNormal, ls.Dir)};
        const float   dist2{ls.Dist * ls.Dist};
        const float   cosY{std::max(0.0f, GfDot(-ls.Dir, ls.LightNormal))};
        const float   pNee{ls.Pdf.ConvertTo(PdfSpace::SolidAngle, dist2, std::max(cosY, 1e-6f)).value};
        const float   pBsdf{surface.bsdf.Pdf(surface.shadingNormal, wo, ls.Dir)};
        const GfVec3f bsdfValue{surface.bsdf.Eval(surface.shadingNormal, wo, ls.Dir)};
        const bool    useMis{!light.IsDeltaLight()};

        if (nDotL <= 0.0f || pNee <= 0.0f || (cosY <= 0.0f && !light.IsDeltaLight()))
            return MISContrib{.PNee = pNee, .PBsdf = pBsdf, .UseMis = useMis};

        // skipVisibility: the target function (used only for candidate *selection*) is
        // evaluated as if the sample were always unoccluded — no shadow ray fired here.
        // Cheaper (one less scene intersection per candidate), and still unbiased overall:
        // VisibilityTested=false flags this up so whichever candidate wins gets a real shadow
        // ray back in Li(), before its contribution is used in the final estimator.
        if (skipVisibility)
            return MISContrib{.Emission         = RGBToSpectrum(ls.Color, surface.lambda),
                              .ThroughputMul    = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
                              .PNee             = pNee,
                              .PBsdf            = pBsdf,
                              .UseMis           = useMis,
                              .VisibilityTested = false};

        const GfVec3f shadowOrigin{isect.hit->Position + surface.shadingNormal * 1e-4f};
        const auto    shadowHit{scene.IntersectScene(shadowOrigin, ls.Dir)};
        if (shadowHit && shadowHit->Depth < ls.Dist - 1e-3f)
            return MISContrib{.ThroughputMul = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
                              .PNee          = pNee,
                              .PBsdf         = pBsdf,
                              .UseMis        = useMis};

        return MISContrib{.ShadowHit     = shadowHit,
                          .Emission      = RGBToSpectrum(ls.Color, surface.lambda),
                          .ThroughputMul = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
                          .PNee          = pNee,
                          .PBsdf         = pBsdf,
                          .UseMis        = useMis};
    }

    SampledSpectrum RisDirectLightIntegrator::_reEvaluateForTemporalReuse(const RayIntersection &isect,
                                                                          const LightCandidate  &candidate)
    {
        const ShadingPoint &surface{*isect.shadingPoint};
        const GfVec3f       wo{-isect.ray.Dir};
        const LightSample  &ls{candidate.Ls};

        const float nDotL{GfDot(surface.shadingNormal, ls.Dir)};
        const float dist2{ls.Dist * ls.Dist};
        const float cosY{std::max(0.0f, GfDot(-ls.Dir, ls.LightNormal))};
        const float pNee{ls.Pdf.ConvertTo(PdfSpace::SolidAngle, dist2, std::max(cosY, 1e-6f)).value};
        if (nDotL <= 0.0f || pNee <= 0.0f || (cosY <= 0.0f && !candidate.Light->IsDeltaLight()))
            return SampledSpectrum{};

        const GfVec3f bsdfValue{surface.bsdf.Eval(surface.shadingNormal, wo, ls.Dir)};
        return RGBToSpectrum(ls.Color, surface.lambda) * RGBToSpectrum(bsdfValue * nDotL, surface.lambda);
    }

    RisDirectLightIntegrator::MISContrib
    RisDirectLightIntegrator::_evaluateBSDFConnection(const RayIntersection      &isect,
                                                      const BsdfBounceConnection &connection, const IScene &scene,
                                                      const SamplingStrategy &samplingStrategy)
    {
        const SampledWavelengths &lambda{isect.shadingPoint->lambda};
        const float               pBsdf{connection.Bounce.BsdfPdf.value};
        const GfVec3f            &wi{connection.Bounce.NextRay.Dir};
        const SampledSpectrum    &integrandMul{connection.Bounce.ThroughputIntegrandMul};

        // impossible NEE
        if (connection.Bounce.ImpossibleNEEConnection || pBsdf <= 0.0f)
        {
            // if no hit use the environment
            if (!connection.Hit.has_value())
            {
                const IEnvironment *environment{scene.GetEnvironment()};
                return {
                    .Emission      = environment ? RGBToSpectrum(environment->Sample(wi), lambda) : SampledSpectrum{},
                    .ThroughputMul = integrandMul,
                    .PNee          = 0.0f,
                    .PBsdf         = pBsdf,
                    .UseMis        = false,
                };
            }

            // otherwise get the emission of the hit material
            const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
            const BSDFClosure closure{material.GetClosure(*connection.Hit)};
            return {
                .Emission      = RGBToSpectrum(closure.Emission, lambda),
                .ThroughputMul = integrandMul,
                .PNee          = 0.0f,
                .PBsdf         = pBsdf,
                .UseMis        = false,
            };
        }

        // if nee connection was possible, compute the NEE pdf for MIS

        // if no hit, check if the environment is present and compute its pdf
        if (!connection.Hit.has_value())
        {
            const IEnvironment *environment{scene.GetEnvironment()};
            if (environment == nullptr)
            {
                return {.ThroughputMul = integrandMul, .PBsdf = pBsdf};
            }
            const float lightSelectPdf{samplingStrategy.sampler.EvalPdf(*environment)};
            const float environmentPdf{environment->EvalPdf(wi)};
            const float pNee{lightSelectPdf > 0.0f ? lightSelectPdf * environmentPdf
                                                   : (samplingStrategy.nNeeSky > 0 ? environmentPdf : 0.0f)};
            return {
                .Emission      = RGBToSpectrum(environment->Sample(wi), lambda),
                .ThroughputMul = integrandMul,
                .PNee          = pNee,
                .PBsdf         = pBsdf,
                .UseMis        = pNee > 0.0f,
            };
        }

        // if we did have a hit, check if it was a light and compute its pdf
        float             pNee{0.f};
        const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
        const BSDFClosure closure{material.GetClosure(*connection.Hit)};
        const auto        emission{RGBToSpectrum(closure.Emission, lambda)};
        const ILight     *hitLight{scene.GetLightAtHit(*connection.Hit)};
        if (hitLight != nullptr)
        {
            const float   lightSelectPdf{samplingStrategy.sampler.EvalPdf(*hitLight)};
            const GfVec3f hitOffset{connection.Hit->Position - connection.Bounce.NextRay.Origin};
            const float   dist2{GfDot(hitOffset, hitOffset)};
            const float   cosY{std::max(0.0f, GfDot(-wi, connection.Hit->Normal))};
            if (cosY > 0.0f && lightSelectPdf > 0.0f)
            {
                const Pdf areaPdf{hitLight->EvalPdf(isect.hit->Position, wi, std::sqrt(dist2), connection.Hit->Normal)};
                pNee = lightSelectPdf * areaPdf.ConvertTo(PdfSpace::SolidAngle, dist2, cosY).value;
            }
        }

        return {
            .Emission      = emission,
            .ThroughputMul = integrandMul,
            .PNee          = pNee,
            .PBsdf         = pBsdf,
            .UseMis        = pNee > 0.0f,
        };
    }

} // namespace Restir
