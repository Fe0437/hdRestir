#include "mis_direct_light_integrator.h"

#include "shading_helpers.h"

#include <algorithm>
#include <cmath>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    SampledSpectrum MisDirectLightIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths &lambda) const
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
        const ShadingPoint           surface{hit, *bsdfOwner, closure, shadingNormal, isect.ray.Dir, lambda, isInside};

        return throughput * (RGBToSpectrum(closure.Emission, lambda) + Li(surface, scene, rng));
    }

    MisDirectLightIntegrator::MISContrib MisDirectLightIntegrator::_evaluateNEE(const ShadingPoint &surface,
                                                                                const ILight       &light,
                                                                                const LightSample  &lightSample,
                                                                                const IScene       &scene) const
    {
        const GfVec3f &shadingNormal{surface.shadingNormal};
        const GfVec3f &hitPos{surface.hit.Position};

        const float nDotL{GfDot(shadingNormal, lightSample.Dir)};
        if (nDotL <= 0.0f)
        {
            return {};
        }

        const float dist2{lightSample.Dist * lightSample.Dist};
        const float cosY{std::max(0.0f, GfDot(-lightSample.Dir, lightSample.LightNormal))};
        if (cosY <= 0.0f && !light.IsDeltaLight())
        {
            return {};
        }

        const float pNee{lightSample.Pdf.ConvertTo(PdfSpace::SolidAngle, dist2, std::max(cosY, 1e-6f)).value};
        if (pNee <= 0.0f)
        {
            return {};
        }

        const GfVec3f shadowOrigin{hitPos + shadingNormal * 1e-4f};
        const auto    shadowHit{scene.IntersectScene(shadowOrigin, lightSample.Dir)};
        if (shadowHit && shadowHit->Depth < lightSample.Dist - 1e-3f)
        {
            return {};
        }

        const GfVec3f wo{-surface.rayDir};
        const GfVec3f bsdfValue{surface.bsdf.Eval(shadingNormal, wo, lightSample.Dir)};
        const float   bsdfPdf{surface.bsdf.Pdf(shadingNormal, wo, lightSample.Dir)};

        // NEE estimator for one sampled light direction:
        // f(x, wo, wi) * Le(y -> x) * cos(theta_x) / p_nee(wi).
        const SampledSpectrum radiance{RGBToSpectrum(bsdfValue, surface.lambda) *
                                       RGBToSpectrum(lightSample.Color, surface.lambda) * (nDotL / pNee)};

        return {
            .Radiance = radiance,
            .PNee     = pNee,
            .PBsdf    = bsdfPdf,
            .IsDelta  = light.IsDeltaLight(),
        };
    }

    SampledSpectrum MisDirectLightIntegrator::_integrateNEE(const ShadingPoint &surface, const IScene &scene, Rng &rng,
                                                            bool useBsdfTechnique) const
    {
        SampledSpectrum      totalRadiance{0.0f};
        const ILightSampler &activeSampler{*_sampler};

        const auto candidate{activeSampler.ProposeCandidate(surface.hit.Position, rng)};
        if (candidate.has_value())
        {
            const MISContrib nee{_evaluateNEE(surface, *candidate->Light, candidate->Ls, scene)};
            if (nee.PNee > 0.0f)
            {
                const float misWeight{(nee.IsDelta || !useBsdfTechnique) ? 1.0f : PowerHeuristic(nee.PNee, nee.PBsdf)};
                totalRadiance += nee.Radiance * misWeight;
            }
        }

        if (!activeSampler.IsConsideringSkyLight())
        {
            const ILight *skyLight{scene.GetSkyLight()};
            if (skyLight != nullptr)
            {
                const auto lightSample{skyLight->SampleLight(surface.hit.Position, rng)};
                if (lightSample.has_value())
                {
                    const MISContrib nee{_evaluateNEE(surface, *skyLight, *lightSample, scene)};
                    if (nee.PNee > 0.0f)
                    {
                        const float misWeight{(nee.IsDelta || !useBsdfTechnique) ? 1.0f
                                                                                 : PowerHeuristic(nee.PNee, nee.PBsdf)};
                        totalRadiance += nee.Radiance * misWeight;
                    }
                }
            }
        }

        return totalRadiance;
    }

    SampledSpectrum MisDirectLightIntegrator::_integrateBSDFConnection(const ShadingPoint         &surface,
                                                                       const BsdfBounceConnection &connection,
                                                                       const IScene               &scene) const
    {
        if (connection.Bounce.ImpossibleNEEConnection || connection.Bounce.BsdfPdf.value <= 0.0f)
        {
            if (!connection.Hit.has_value())
            {
                const IEnvironment *environment{scene.GetEnvironment()};
                return (environment != nullptr
                            ? RGBToSpectrum(environment->Sample(connection.Bounce.NextRay.Dir), surface.lambda)
                            : SampledSpectrum{0.0f}) *
                       connection.Bounce.ThroughputMul;
            }

            const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
            const BSDFClosure closure{material.GetClosure(*connection.Hit)};
            return RGBToSpectrum(closure.Emission, surface.lambda) * connection.Bounce.ThroughputMul;
        }

        float           pNee{0.0f};
        SampledSpectrum radiance{0.0f};

        if (!connection.Hit.has_value())
        {
            const IEnvironment *environment{scene.GetEnvironment()};
            if (environment == nullptr)
            {
                return SampledSpectrum{0.0f};
            }

            radiance = RGBToSpectrum(environment->Sample(connection.Bounce.NextRay.Dir), surface.lambda);
            pNee     = environment->EvalPdf(connection.Bounce.NextRay.Dir);
        }
        else
        {
            const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
            const BSDFClosure closure{material.GetClosure(*connection.Hit)};
            radiance = RGBToSpectrum(closure.Emission, surface.lambda);

            const ILight *hitLight{scene.GetLightAtHit(*connection.Hit)};
            if (hitLight != nullptr)
            {
                const GfVec3f hitOffset{connection.Hit->Position - connection.Bounce.NextRay.Origin};
                const float   dist2{GfDot(hitOffset, hitOffset)};
                const float   cosY{std::max(0.0f, GfDot(-connection.Bounce.NextRay.Dir, connection.Hit->Normal))};
                if (cosY > 0.0f)
                {
                    const Pdf areaPdf{hitLight->EvalPdf(surface.hit.Position, connection.Bounce.NextRay.Dir,
                                                        std::sqrt(dist2), connection.Hit->Normal)};
                    pNee = areaPdf.ConvertTo(PdfSpace::SolidAngle, dist2, cosY).value;
                }
            }
        }

        if (pNee <= 0.0f)
        {
            return radiance * connection.Bounce.ThroughputMul;
        }

        // BSDF-sampled light-hit estimator with MIS against NEE:
        // (f * cos / p_bsdf) * Le * w_bsdf, where ThroughputMul already stores f * cos / p_bsdf.
        const float misWeight{PowerHeuristic(connection.Bounce.BsdfPdf.value, pNee)};
        return radiance * misWeight * connection.Bounce.ThroughputMul;
    }

    SampledSpectrum MisDirectLightIntegrator::Li(const ShadingPoint &surface, const IScene &scene, Rng &rng) const
    {
        return Li(surface, scene, rng, std::nullopt);
    }

    SampledSpectrum MisDirectLightIntegrator::Li(const ShadingPoint &surface, const IScene &scene, Rng &rng,
                                                 const std::optional<BsdfBounceConnection> &bsdfConnection) const
    {
        const bool      useBsdfTechnique{bsdfConnection.has_value()};
        SampledSpectrum totalRadiance{_integrateNEE(surface, scene, rng, useBsdfTechnique)};

        if (bsdfConnection.has_value())
        {
            totalRadiance += _integrateBSDFConnection(surface, *bsdfConnection, scene);
        }

        return totalRadiance;
    }

} // namespace Restir
