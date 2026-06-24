#include "path_integrator.h"

#include "shading_helpers.h"

#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    PathIntegrator::PathIntegrator(DirectLightIntegratorFactory directLightFactory, PathTracePassSettings settings,
                                   int maxDepth)
        : _settings{settings}, _maxDepth{maxDepth}, _directLightFactory{std::move(directLightFactory)}
    {
        if (!_directLightFactory)
        {
            throw std::runtime_error{"PathIntegrator requires a direct light integrator factory"};
        }
    }

    SampledSpectrum PathIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                       const SampledWavelengths &lambda) const
    {
        const IEnvironment *env{scene.GetEnvironment()};
        const GfVec3f       rayDir{isect.ray.Dir};

        // Advance through transparent surfaces until we reach an opaque hit or miss everything.
        std::optional<HitRecord> hit{isect.hit};
        while (hit)
        {
            const IMaterial &mat{scene.GetMaterial(hit->MatId)};

            BSDFClosure c{mat.GetClosure(*hit)};
            if (!_settings.EnableSubsurface)
            {
                c.Subsurface = 0.0f;
            }

            if (c.Opacity >= 0.999f || rng.NextFloat() <= c.Opacity)
            {
                const bool      isInside{GfDot(c.Normal, rayDir) > 0.0f};
                SampledSpectrum throughput{1.0f};
                BeerAbsorption(throughput, c, hit->Depth, isInside, lambda);

                GfVec3f shadingNormal{c.Normal};
                if (isInside)
                {
                    shadingNormal = -shadingNormal;
                }

                const std::unique_ptr<IBSDF> bsdfOwner{mat.CreateBSDF(BSDFClosure{c})};
                const ShadingPoint           firstSurface{*hit, *bsdfOwner, c, shadingNormal, rayDir, lambda, isInside};
                return throughput * Li(firstSurface, scene, rng);
            }

            hit = scene.IntersectScene(hit->Position + rayDir * 1e-4f, rayDir);
        }

        // Primary ray missed all geometry.
        if (env != nullptr && _settings.RenderIblBackground)
        {
            return RGBToSpectrum(env->Sample(rayDir), lambda);
        }
        return SampledSpectrum{0.0f};
    }

    SampledSpectrum PathIntegrator::Li(const ShadingPoint &firstSurface, const IScene &scene, Rng &rng) const
    {
        const SampledWavelengths &lambda{firstSurface.lambda};
        const BounceConfig        config{_settings.MaxReflectionBounces, _settings.MaxRefractionBounces};

        SampledSpectrum throughput{1.0f};
        SampledSpectrum totalRadiance{0.0f};
        BounceState     bounceState{};
        auto            directLightIntegrator{_directLightFactory(scene)};

        // --- First surface: already evaluated by the caller, no hit/material eval needed ---
        // Primary-hit emission is not part of MIS direct-light estimation.
        totalRadiance += RGBToSpectrum(firstSurface.c.Emission, lambda);

        const IMaterial &firstMaterial{scene.GetMaterial(firstSurface.hit.MatId)};

        const BounceWithConnectionResult firstBounceResult{
            Detail::SampleBounceWithConnection(firstMaterial, firstSurface, config, bounceState, scene, rng)};
        const std::optional<BsdfBounceConnection> firstConnection{
            std::holds_alternative<BsdfBounceConnection>(firstBounceResult)
                ? std::make_optional(std::get<BsdfBounceConnection>(firstBounceResult))
                : std::nullopt};
        totalRadiance += directLightIntegrator->Li(firstSurface, scene, rng, firstConnection);

        if (!std::holds_alternative<BsdfBounceConnection>(firstBounceResult))
        {
            return totalRadiance;
        }

        const BsdfBounceConnection &firstBounceConnection{std::get<BsdfBounceConnection>(firstBounceResult)};

        throughput *= firstBounceConnection.Bounce.ThroughputMul;
        Ray                      currentRay{firstBounceConnection.Bounce.NextRay};
        std::optional<HitRecord> nextHit{firstBounceConnection.Hit};

        // --- Subsequent bounces ---
        for (int bounce{1}; bounce < _maxDepth; ++bounce)
        {
            if (!nextHit.has_value())
            {
                break;
            }

            HitRecord        hit{*nextHit};
            const IMaterial &material{scene.GetMaterial(hit.MatId)};

            BSDFClosure c{material.GetClosure(hit)};
            if (!_settings.EnableSubsurface)
            {
                c.Subsurface = 0.0f;
            }

            if (c.Opacity < 0.999f && rng.NextFloat() > c.Opacity)
            {
                currentRay.Origin = hit.Position + currentRay.Dir * 1e-4f;
                nextHit           = scene.IntersectScene(currentRay.Origin, currentRay.Dir);
                --bounce;
                continue;
            }

            const bool isInside{GfDot(c.Normal, currentRay.Dir) > 0.0f};
            BeerAbsorption(throughput, c, hit.Depth, isInside, lambda);

            GfVec3f shadingNormal{c.Normal};
            if (isInside)
            {
                shadingNormal = -shadingNormal;
            }

            const std::unique_ptr<IBSDF> bsdfOwner{material.CreateBSDF(BSDFClosure{c})};
            const ShadingPoint           surface{hit, *bsdfOwner, c, shadingNormal, currentRay.Dir, lambda, isInside};

            const BounceWithConnectionResult bounceResult{
                Detail::SampleBounceWithConnection(material, surface, config, bounceState, scene, rng)};
            const std::optional<BsdfBounceConnection> bsdfConnection{
                std::holds_alternative<BsdfBounceConnection>(bounceResult)
                    ? std::make_optional(std::get<BsdfBounceConnection>(bounceResult))
                    : std::nullopt};
            totalRadiance += throughput * directLightIntegrator->Li(surface, scene, rng, bsdfConnection);

            if (!std::holds_alternative<BsdfBounceConnection>(bounceResult))
            {
                break;
            }

            const BsdfBounceConnection &resolvedConnection{std::get<BsdfBounceConnection>(bounceResult)};

            const BsdfBounceSample &bounceSample{resolvedConnection.Bounce};
            throughput *= bounceSample.ThroughputMul;
            currentRay = bounceSample.NextRay;
            nextHit    = resolvedConnection.Hit;

            if (bounceSample.SkipRoulette)
            {
                continue;
            }

            if (bounce > 3)
            {
                const float p{throughput.Max()};
                if (rng.NextFloat() > p)
                {
                    break;
                }
                throughput *= 1.0f / p;
            }
        }

        return totalRadiance;
    }
} // namespace Restir
