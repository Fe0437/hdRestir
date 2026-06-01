#include "ris_direct_light_integrator.h"

#include "debug.h"
#include "random_index.h"
#include "shading_helpers.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {


SampledSpectrum RisDirectLightIntegrator::Li(
    const ShadingPoint&                      surface,
    const IScene&                            scene,
    Rng&                                     rng,
    const std::optional<BsdfBounceConnection>& bsdfConnection) const
{

    auto allCandidates{
        _generateNEECandidates(surface, scene, rng, *_sampler, _candidateCount, bsdfConnection.has_value())};
    const auto bsdfSamples{_generateBSDFSamplingCandidates(surface, scene, rng, _candidateCount, bsdfConnection)};
    allCandidates.insert(allCandidates.end(), std::make_move_iterator(bsdfSamples.begin()),std::make_move_iterator( bsdfSamples.end()));

    DBG_ASSERT(!allCandidates.empty(), "RIS expected at least one candidate");
    const auto chosen{
        RIS::RandomIndexFromWeights(gsl::span<const OptionalRisLightCandidate>{allCandidates}, rng)};
    if (!chosen.has_value()) {
        return SampledSpectrum{};
    }

    const OptionalRisLightCandidate& chosenCandidateOptional{allCandidates[static_cast<size_t>(chosen->Index)]};
    if (!chosenCandidateOptional.has_value()) {
        return SampledSpectrum{};
    }

    const RISLightCandidate& chosenCandidate{chosenCandidateOptional.value()};
    DBG_ASSERT(chosenCandidate.targetFunction > 0.0, "RIS chosen candidate must have positive target function");

    const float unbiasedContributionWeight{static_cast<float>(chosen->WeightSum / chosenCandidate.targetFunction)};
    return chosenCandidate.Emission * chosenCandidate.ThroupoutMult * unbiasedContributionWeight;
}

SampledSpectrum RisDirectLightIntegrator::Li(
    const ShadingPoint& surface,
    const IScene&       scene,
    Rng&                rng) const
{
    return Li(surface, scene, rng, std::nullopt);
}


SampledSpectrum RisDirectLightIntegrator::Li(
    const RayIntersection&    isect,
    const IScene&             scene,
    Rng&                      rng,
    const SampledWavelengths& lambda) const
{
    if (!isect.hit.has_value()) {
        const IEnvironment* environment{scene.GetEnvironment()};
        return environment != nullptr
            ? RGBToSpectrum(environment->Sample(isect.ray.Dir), lambda)
            : SampledSpectrum{0.0f};
    }

    const HitRecord& hit{*isect.hit};
    const IMaterial& material{scene.GetMaterial(hit.MatId)};

    BSDFClosure closure{material.GetClosure(hit)};
    const bool isInside{GfDot(closure.Normal, isect.ray.Dir) > 0.0f};
    SampledSpectrum throughput{1.0f};
    BeerAbsorption(throughput, closure, hit.Depth, isInside, lambda);

    GfVec3f shadingNormal{closure.Normal};
    if (isInside) {
        shadingNormal = -shadingNormal;
    }

    const std::unique_ptr<IBSDF> bsdfOwner{material.CreateBSDF(BSDFClosure{closure})};
    const ShadingPoint surface{hit, *bsdfOwner, closure, shadingNormal, isect.ray.Dir, lambda, isInside};

    return throughput * (RGBToSpectrum(closure.Emission, lambda) + Li(surface, scene, rng));
}

[[maybe_unused]] std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> RisDirectLightIntegrator::_generateBSDFSamplingCandidates(
    const ShadingPoint& surface,
    const IScene&       scene,
    Rng&                rng,
    int                 candidateCount,
    const std::optional<BsdfBounceConnection>& bsdfConnection)
{

    std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> candidates{};
    candidates.reserve(static_cast<size_t>(std::max(0, candidateCount)));

    if (bsdfConnection.has_value()) {
        const BsdfBounceConnection& connection{*bsdfConnection};
        if (connection.Bounce.BsdfPdf.value > 0.0f) {
            const auto bsdfContrib{_evaluateBSDFConnection(surface, connection, scene)};
            const double misWeight{PowerHeuristic(bsdfContrib.PBsdf, candidateCount, bsdfContrib.PNee, candidateCount)};
            const double targetFunction{SpectrumLuminance(bsdfContrib.Emission * bsdfContrib.ThroughputIntegrandMul, surface.lambda)};
            const double risWeight{misWeight * targetFunction / (bsdfContrib.PBsdf + 1e-9)};

            std::optional<LightCandidate> lc{};
            if (connection.Hit.has_value()) {
                const ILight* light{scene.GetLightAtHit(*connection.Hit)};
                if (light != nullptr) {
                    const auto lightSample{light->SampleLight(surface.hit.Position, rng)};
                    if (lightSample.has_value()) {
                        lc = LightCandidate{.Light = light, .Ls = *lightSample};
                    }
                }
            }

            candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                .Candidate      = std::move(lc),
                .Emission       = bsdfContrib.Emission,
                .ThroupoutMult  = bsdfContrib.ThroughputIntegrandMul,
                .risWeight      = risWeight,
                .targetFunction = targetFunction,
            });
        }
    }

    const int candidatesToGenerate{candidateCount - static_cast<int>(candidates.size())};
    for (int i{0}; i < candidatesToGenerate; ++i) {
        const IMaterial& material{scene.GetMaterial(surface.hit.MatId)};
        BounceState      bounceState{};
        BounceConfig     bounceConfig{.effectiveMaxRefl = 1, .effectiveMaxRefr = 1};
        const BounceWithConnectionResult bounceResult{
            Detail::SampleBounceWithConnection(material, surface, bounceConfig, bounceState, scene, rng)};
        if (!std::holds_alternative<BsdfBounceConnection>(bounceResult)) {
            candidates.push_back(std::nullopt);
            continue;
        }

        const BsdfBounceConnection& sampledConnection{std::get<BsdfBounceConnection>(bounceResult)};
        if (!sampledConnection.Hit.has_value()) {
            const IEnvironment* environment{scene.GetEnvironment()};
            if (environment == nullptr) {
                candidates.push_back(std::nullopt);
                continue;
            }
            const SampledSpectrum& envThroughput{sampledConnection.Bounce.ThroughputIntegrandMul};
            const SampledSpectrum envEmission{RGBToSpectrum(environment->Sample(sampledConnection.Bounce.NextRay.Dir), surface.lambda)};
            const float pBsdf{sampledConnection.Bounce.BsdfPdf.value};
            const float pNee{environment->EvalPdf(sampledConnection.Bounce.NextRay.Dir)};
            const double targetFunction{SpectrumLuminance(envEmission * envThroughput, surface.lambda)};
            const double misWeight{PowerHeuristic(pBsdf, candidateCount, pNee, candidateCount)};
            const double risWeight{misWeight * targetFunction / (pBsdf + 1e-9)};
            candidates.push_back(RISLightCandidate{
                .Emission       = envEmission,
                .ThroupoutMult  = envThroughput,
                .risWeight      = risWeight,
                .targetFunction = targetFunction,
            });
            continue;
        }

        const ILight* light{scene.GetLightAtHit(*sampledConnection.Hit)};
        if (light == nullptr) {
            candidates.push_back(std::nullopt);
            continue;
        }

        const auto lightSample{light->SampleLight(surface.hit.Position, rng)};
        if (!lightSample.has_value()) {
            candidates.push_back(std::nullopt);
            continue;
        }


        const auto bsdfContrib{_evaluateBSDFConnection(surface, sampledConnection, scene)};
        const double misWeight{PowerHeuristic(bsdfContrib.PBsdf, candidateCount, bsdfContrib.PNee, candidateCount)};
        const double targetFunction{SpectrumLuminance(bsdfContrib.Emission * bsdfContrib.ThroughputIntegrandMul, surface.lambda)};

        double risWeight{misWeight * targetFunction / (bsdfContrib.PBsdf + 1e-9)};

        candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
            .Candidate = LightCandidate{
                .Light = light,
                .Ls    = *lightSample,
            },
            .Emission      = bsdfContrib.Emission,
            .ThroupoutMult = bsdfContrib.ThroughputIntegrandMul,
            .risWeight     = risWeight,
            .targetFunction = targetFunction,
        });
    }

    return candidates;
}

[[maybe_unused]] std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> RisDirectLightIntegrator::_generateNEECandidates(
    const ShadingPoint& surface,
    const IScene&       scene,
    Rng&                rng,
    const ILightSampler& sampler,
    int                 candidateCount,
    bool                useBsdfTechnique)
{
    std::vector<RisDirectLightIntegrator::OptionalRisLightCandidate> candidates{};
    candidates.reserve(static_cast<size_t>(std::max(0, candidateCount)));

    for (int i{0}; i < candidateCount; ++i) {
        const auto candidate{sampler.ProposeCandidate(surface.hit.Position, rng)};
        if (!candidate.has_value()) {
            candidates.push_back(std::nullopt);
            continue;
        }

        //mis weight
        const MISContrib nee{_evaluateNEE(surface, *candidate->Light, candidate->Ls, scene)};
        const float targetFunction{SpectrumLuminance(nee.Emission * nee.ThroughputIntegrandMul, surface.lambda)};
        const float misWeight{(nee.UseMis && useBsdfTechnique)
                ? PowerHeuristic(nee.PNee, candidateCount, nee.PBsdf, candidateCount)
                : 1.0f};

        //𝑚𝑖(𝑋𝑖)f(𝑋𝑖)𝑊𝑋𝑖 where 𝑊𝑋𝑖 in this case is pNee
        const double risWeight{misWeight * targetFunction / (nee.PNee + 1e-9)};
        candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
            .Candidate = *candidate,
            .Emission = nee.Emission,
            .ThroupoutMult = nee.ThroughputIntegrandMul,
            .risWeight = risWeight,
            .targetFunction = targetFunction,
        });
    }

    if (!sampler.IsConsideringSkyLight()) {
        const ILight* skyLight{scene.GetSkyLight()};
        if (skyLight != nullptr) {
            const auto lightSample{skyLight->SampleLight(surface.hit.Position, rng)};
            if (lightSample.has_value()) {
                const MISContrib nee{_evaluateNEE(surface, *skyLight, *lightSample, scene)};
                const float targetFunction{SpectrumLuminance(nee.Emission * nee.ThroughputIntegrandMul, surface.lambda)};
                const float misWeight{(nee.UseMis && useBsdfTechnique)
                    ? PowerHeuristic(nee.PNee, 1, nee.PBsdf, candidateCount)
                    : 1.0f};
                const double risWeight{misWeight * targetFunction / (nee.PNee + 1e-9)};
                candidates.push_back(RisDirectLightIntegrator::RISLightCandidate{
                    .Emission       = nee.Emission,
                    .ThroupoutMult  = nee.ThroughputIntegrandMul,
                    .risWeight      = risWeight,
                    .targetFunction = targetFunction,
                });
            }
        }
    }

    return candidates;
}



RisDirectLightIntegrator::MISContrib RisDirectLightIntegrator::_evaluateNEE(
    const ShadingPoint& surface,
    const ILight&       light,
    const LightSample&  lightSample,
    const IScene&       scene)
{
    const GfVec3f& shadingNormal{surface.shadingNormal};
    const GfVec3f& hitPos{surface.hit.Position};

    const float nDotL{GfDot(shadingNormal, lightSample.Dir)};
    const float dist2{lightSample.Dist * lightSample.Dist};
    const float cosY{std::max(0.0f, GfDot(-lightSample.Dir, lightSample.LightNormal))};
    const float pNee{lightSample.Pdf.ConvertTo(PdfSpace::SolidAngle, dist2, std::max(cosY, 1e-6f)).value};
    const GfVec3f wo{-surface.rayDir};
    const GfVec3f bsdfValue{surface.bsdf.Eval(shadingNormal, wo, lightSample.Dir)};
    const float bsdfPdf{surface.bsdf.Pdf(shadingNormal, wo, lightSample.Dir)};
    
    if ( nDotL <= 0.0f || pNee <= 0.0f || (cosY <= 0.0f && !light.IsDeltaLight())) {
        return MISContrib{
            .ShadowHit = std::nullopt,
            .Emission                = 0,
            .ThroughputIntegrandMul  = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
            .PNee                    = pNee,
            .PBsdf                   = bsdfPdf,
            .UseMis                  = !light.IsDeltaLight(),
        };
    }

    const GfVec3f shadowOrigin{hitPos + shadingNormal * 1e-4f};
    const auto shadowHit{scene.IntersectScene(shadowOrigin, lightSample.Dir)};
    if (shadowHit && shadowHit->Depth < lightSample.Dist - 1e-3f) {
        return MISContrib{
            .ShadowHit = std::nullopt,
            .Emission                = 0,
            .ThroughputIntegrandMul  = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
            .PNee                    = pNee,
            .PBsdf                   = bsdfPdf,
            .UseMis                  = !light.IsDeltaLight(),
        };
    }

    return MISContrib{
            .ShadowHit              = shadowHit,
            .Emission               = RGBToSpectrum(lightSample.Color, surface.lambda),
            .ThroughputIntegrandMul = RGBToSpectrum(bsdfValue * nDotL, surface.lambda),
            .PNee                   = pNee,
            .PBsdf                  = bsdfPdf,
            .UseMis                 = !light.IsDeltaLight(),
        };
}


RisDirectLightIntegrator::MISContrib RisDirectLightIntegrator::_evaluateBSDFConnection(
    const ShadingPoint&         surface,
    const BsdfBounceConnection& connection,
    const IScene&               scene)
{
    const float pBsdf{connection.Bounce.BsdfPdf.value};
    const GfVec3f& wi{connection.Bounce.NextRay.Dir};
    const SampledSpectrum& integrandMul{connection.Bounce.ThroughputIntegrandMul};

    if (connection.Bounce.ImpossibleNEEConnection || pBsdf <= 0.0f) {
        if (!connection.Hit.has_value()) {
            const IEnvironment* environment{scene.GetEnvironment()};
            return {
                .Emission         = environment
                    ? RGBToSpectrum(environment->Sample(wi), surface.lambda)
                    : SampledSpectrum{},
                .ThroughputIntegrandMul = integrandMul,
                .PNee             = 0.0f,
                .PBsdf            = pBsdf,
                .UseMis           = false,
            };
        }

        const IMaterial& material{scene.GetMaterial(connection.Hit->MatId)};
        const BSDFClosure closure{material.GetClosure(*connection.Hit)};
        return {
            .Emission         = RGBToSpectrum(closure.Emission, surface.lambda),
            .ThroughputIntegrandMul = integrandMul,
            .PNee             = 0.0f,
            .PBsdf            = pBsdf,
            .UseMis           = false,
        };
    }

    if (!connection.Hit.has_value()) {
        const IEnvironment* environment{scene.GetEnvironment()};
        if (environment == nullptr) {
            return { .ThroughputIntegrandMul = integrandMul, .PBsdf = pBsdf };
        }
        const float pNee{environment->EvalPdf(wi)};
        return {
            .Emission         = RGBToSpectrum(environment->Sample(wi), surface.lambda),
            .ThroughputIntegrandMul = integrandMul,
            .PNee             = pNee,
            .PBsdf            = pBsdf,
            .UseMis           = pNee > 0.0f,
        };
    }

    float pNee{0.f};
    const IMaterial& material{scene.GetMaterial(connection.Hit->MatId)};
    const BSDFClosure closure{material.GetClosure(*connection.Hit)};
    auto emission{RGBToSpectrum(closure.Emission, surface.lambda)};
    const ILight* hitLight{scene.GetLightAtHit(*connection.Hit)};
    if (hitLight != nullptr) {
        const GfVec3f hitOffset{connection.Hit->Position - connection.Bounce.NextRay.Origin};
        const float dist2{GfDot(hitOffset, hitOffset)};
        const float cosY{std::max(0.0f, GfDot(-wi, connection.Hit->Normal))};
        if (cosY > 0.0f) {
            const Pdf areaPdf{hitLight->EvalPdf(surface.hit.Position, wi, std::sqrt(dist2), connection.Hit->Normal)};
            pNee = areaPdf.ConvertTo(PdfSpace::SolidAngle, dist2, cosY).value;
        }
    }

    return {
        .Emission         = emission,
        .ThroughputIntegrandMul = integrandMul,
        .PNee             = pNee,
        .PBsdf            = pBsdf,
        .UseMis           = pNee > 0.0f,
    };
}


}  // namespace Restir
