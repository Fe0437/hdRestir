#include "mis_direct_light_integrator.h"

#include "lighting_core/uniform_light_sampler.h"
#include "shading_helpers.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        class MisDirectLightIntegratorFactory final : public IDirectLightIntegratorFactory
        {
          public:
            [[nodiscard]] NotNullUniquePtr<IDirectLightIntegrator> Create(const IScene &scene,
                                                                          IBufferProvider & /*provider*/) const override
            {
                return NotNullUniquePtr<IDirectLightIntegrator>{std::make_unique<MisDirectLightIntegrator>(
                    NotNullUniquePtr<ILightSampler>{std::make_unique<UniformLightSampler>(scene.GetLights())})};
            }
        };

    } // namespace

    NotNullUniquePtr<IDirectLightIntegratorFactory> MisDirectLightIntegrator::MakeFactory()
    {
        return NotNullUniquePtr<IDirectLightIntegratorFactory>{std::make_unique<MisDirectLightIntegratorFactory>()};
    }

    SampledSpectrum MisDirectLightIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
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

    MisDirectLightIntegrator::MISContrib MisDirectLightIntegrator::_evaluateNEE(const RayIntersection &isect,
                                                                                const ILight          &light,
                                                                                const LightSample     &lightSample,
                                                                                const IScene          &scene) const
    {
        const ShadingPoint &surface{*isect.shadingPoint};
        const GfVec3f      &shadingNormal{surface.shadingNormal};
        const GfVec3f      &hitPos{isect.hit->Position};

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

        // The light-selection probability is already baked into lightSample.Pdf by the light sampler
        // (and is 1 for the directly-sampled sky technique), so no extra lightSelectPdf factor is applied.
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

        const GfVec3f wo{-isect.ray.Dir};
        const float   bsdfPdf{surface.bsdf.Pdf(shadingNormal, wo, lightSample.Dir)};
        const GfVec3f bsdfValue{surface.bsdf.Eval(shadingNormal, wo, lightSample.Dir)};

        const SampledSpectrum radiance{RGBToSpectrum(bsdfValue, surface.lambda) *
                                       RGBToSpectrum(lightSample.Color, surface.lambda) * (nDotL / pNee)};

        return {
            .Radiance = radiance,
            .PNee     = pNee,
            .PBsdf    = bsdfPdf,
            .IsDelta  = light.IsDeltaLight(),
        };
    }

    SampledSpectrum MisDirectLightIntegrator::_integrateNEE(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                            bool useBsdfTechnique) const
    {
        SampledSpectrum      totalRadiance{0.0f};
        const ILightSampler &activeSampler{*_sampler};
        const GfVec3f       &hitPos{isect.hit->Position};

        const IEnvironment *envLight{scene.GetEnvironment()};
        const float         envSelectPdf{envLight != nullptr ? activeSampler.EvalPdf(*envLight) : 0.0f};
        const bool          hasSeparateEnvTechnique{envLight != nullptr && envSelectPdf <= 0.0f};

        const auto candidate{activeSampler.ProposeCandidate(hitPos, rng)};
        if (candidate.has_value())
        {
            const MISContrib nee{_evaluateNEE(isect, *candidate->Light, candidate->Ls, scene)};
            if (nee.PNee > 0.0f)
            {
                float misWeight;
                if (nee.IsDelta || !useBsdfTechnique)
                {
                    misWeight = 1.0f;
                }
                else if (hasSeparateEnvTechnique)
                {
                    misWeight = PowerHeuristic(nee.PNee, 1, 0.0f, 1, nee.PBsdf, 1);
                }
                else
                {
                    misWeight = PowerHeuristic(nee.PNee, nee.PBsdf);
                }
                totalRadiance += nee.Radiance * misWeight;
            }
        }

        if (hasSeparateEnvTechnique)
        {
            const auto lightSample{envLight->SampleLight(hitPos, rng)};
            if (lightSample.has_value())
            {
                const MISContrib nee{_evaluateNEE(isect, *envLight, *lightSample, scene)};
                if (nee.PNee > 0.0f)
                {
                    float misWeight;
                    if (nee.IsDelta || !useBsdfTechnique)
                    {
                        misWeight = 1.0f;
                    }
                    else
                    {
                        misWeight = PowerHeuristic(nee.PNee, 1, 0.0f, 1, nee.PBsdf, 1);
                    }
                    totalRadiance += nee.Radiance * misWeight;
                }
            }
        }

        return totalRadiance;
    }

    SampledSpectrum MisDirectLightIntegrator::_integrateBSDFConnection(const RayIntersection      &isect,
                                                                       const BsdfBounceConnection &connection,
                                                                       const IScene               &scene) const
    {
        const SampledWavelengths &lambda{isect.shadingPoint->lambda};

        if (connection.Bounce.ImpossibleNEEConnection || connection.Bounce.BsdfPdf.value <= 0.0f)
        {
            if (!connection.Hit.has_value())
            {
                const IEnvironment *environment{scene.GetEnvironment()};
                return (environment != nullptr
                            ? RGBToSpectrum(environment->Sample(connection.Bounce.NextRay.Dir), lambda)
                            : SampledSpectrum{0.0f}) *
                       connection.Bounce.ThroughputMul;
            }

            const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
            const BSDFClosure closure{material.GetClosure(*connection.Hit)};
            return RGBToSpectrum(closure.Emission, lambda) * connection.Bounce.ThroughputMul;
        }

        float           pNeeSampler{0.0f};
        float           pNeeEnv{0.0f};
        SampledSpectrum radiance{0.0f};

        if (!connection.Hit.has_value())
        {
            const IEnvironment *environment{scene.GetEnvironment()};
            if (environment == nullptr)
            {
                return SampledSpectrum{0.0f};
            }

            radiance = RGBToSpectrum(environment->Sample(connection.Bounce.NextRay.Dir), lambda);

            const float envSelectPdf{_sampler->EvalPdf(*environment)};
            const float envPdf{environment->EvalPdf(connection.Bounce.NextRay.Dir)};
            pNeeSampler = envSelectPdf * envPdf;
            pNeeEnv     = envSelectPdf <= 0.0f ? envPdf : 0.0f;
        }
        else
        {
            const IMaterial  &material{scene.GetMaterial(connection.Hit->MatId)};
            const BSDFClosure closure{material.GetClosure(*connection.Hit)};
            radiance = RGBToSpectrum(closure.Emission, lambda);

            const ILight *hitLight{scene.GetLightAtHit(*connection.Hit)};
            if (hitLight != nullptr)
            {
                const float lightSelectPdf{_sampler->EvalPdf(*hitLight)};
                if (lightSelectPdf > 0.0f)
                {
                    const GfVec3f hitOffset{connection.Hit->Position - connection.Bounce.NextRay.Origin};
                    const float   dist2{GfDot(hitOffset, hitOffset)};
                    const float   cosY{std::max(0.0f, GfDot(-connection.Bounce.NextRay.Dir, connection.Hit->Normal))};
                    if (cosY > 0.0f)
                    {
                        const Pdf areaPdf{hitLight->EvalPdf(isect.hit->Position, connection.Bounce.NextRay.Dir,
                                                            std::sqrt(dist2), connection.Hit->Normal)};
                        pNeeSampler = lightSelectPdf * areaPdf.ConvertTo(PdfSpace::SolidAngle, dist2, cosY).value;
                    }
                }
            }
        }

        if (pNeeSampler + pNeeEnv <= 0.0f)
        {
            return radiance * connection.Bounce.ThroughputMul;
        }

        // BSDF-sampled light-hit estimator with MIS against NEE:
        const float misWeight{PowerHeuristic(connection.Bounce.BsdfPdf.value, 1, pNeeSampler, 1, pNeeEnv, 1)};
        return radiance * misWeight * connection.Bounce.ThroughputMul;
    }

    SampledSpectrum MisDirectLightIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths & /*lambda*/, IBufferProvider & /*provider*/,
                                                 const std::optional<BsdfBounceConnection> &bsdfConnection,
                                                 CallIndex /*callId*/) const
    {
        const bool      useBsdfTechnique{bsdfConnection.has_value()};
        SampledSpectrum totalRadiance{_integrateNEE(isect, scene, rng, useBsdfTechnique)};

        if (bsdfConnection.has_value())
        {
            totalRadiance += _integrateBSDFConnection(isect, *bsdfConnection, scene);
        }

        return totalRadiance;
    }

} // namespace Restir
