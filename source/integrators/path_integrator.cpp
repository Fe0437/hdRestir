#include "path_integrator.h"

#include "direct_light_integrator_interface.h"
#include "material.h"
#include "metrics_on_buffers.h"
#include "shading_helpers.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

#if METRICS_ENABLED
    namespace
    {

        // Breaks PathIntegrator's own "Li" cost (see IntegrationPass,
        // core/metrics_on_buffers.h) down into its two per-bounce phases:
        // BSDF sampling vs direct lighting/NEE — declared via
        // DeclareSubMetrics as slots 0/1 of the shared
        // Metrics::kSubMetrics.ValuesBuf buffer, and used directly as
        // their display labels.
        constexpr std::string_view kBsdfSampleMsBuf{"BSDFSample"};
        constexpr std::string_view kDirectLightMsBuf{"DirectLight"};
        constexpr std::size_t      kBsdfSampleSlot{0};
        constexpr std::size_t      kDirectLightSlot{1};

    } // namespace
#endif

    PathIntegrator::PathIntegrator(NotNullUniquePtr<IDirectLightIntegratorFactory> &&factory,
                                   PathTracePassSettings settings, int maxDepth)
        : _settings{settings}, _maxDepth{maxDepth}, _factory{std::move(factory)}
    {
    }

    void PathIntegrator::PrepareBuffers(IBufferProvider &provider, const IScene &scene)
    {
#if METRICS_ENABLED
        Metrics::DeclareSubMetrics(provider, {kBsdfSampleMsBuf, kDirectLightMsBuf});
#endif
        if (IBufferStager * inner{_factory->GetBufferStager()})
        {
            inner->PrepareBuffers(provider, scene);
        }
    }

    SampledSpectrum PathIntegrator::Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                       const SampledWavelengths &lambda, IBufferProvider &provider,
                                       CallIndex callId) const
    {
        const IEnvironment *env{scene.GetEnvironment()};
        const GfVec3f       rayDir{isect.ray.Dir};

        std::optional<HitRecord> hit{isect.hit};
        SampledSpectrum          throughput{1.0f};

        while (hit)
        {
            const IMaterial &mat{scene.GetMaterial(hit->MatId)};
            BSDFClosure      c{mat.GetClosure(*hit)};
            if (!_settings.EnableSubsurface)
            {
                c.Subsurface = 0.0f;
            }

            if (c.Opacity >= 0.999f || rng.NextFloat() <= c.Opacity)
            {
                const bool isInside{GfDot(c.Normal, rayDir) > 0.0f};
                BeerAbsorption(throughput, c, hit->Depth, isInside, lambda);
                GfVec3f shadingNormal{c.Normal};
                if (isInside)
                {
                    shadingNormal = -shadingNormal;
                }
                const std::unique_ptr<IBSDF> bsdfOwner{mat.CreateBSDF(BSDFClosure{c})};
                const ShadingPoint           sp{*bsdfOwner, c, shadingNormal, lambda, isInside};
                const RayIntersection        shadedIsect{isect.ray, hit, sp};
                return throughput * _li(shadedIsect, scene, rng, lambda, provider, callId);
            }

            hit = scene.IntersectScene(hit->Position + rayDir * 1e-4f, rayDir);
        }

        if (env != nullptr && _settings.RenderIblBackground)
        {
            return RGBToSpectrum(env->Sample(rayDir), lambda);
        }
        return SampledSpectrum{0.0f};
    }

    SampledSpectrum PathIntegrator::_li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                        const SampledWavelengths &lambda, IBufferProvider &provider,
                                        CallIndex callId) const
    {
        const ShadingPoint &sp{*isect.shadingPoint};
        const BounceConfig  config{_settings.MaxReflectionBounces, _settings.MaxRefractionBounces};

        auto directLight{_factory->Create(scene, provider)};

        SampledSpectrum totalRadiance{RGBToSpectrum(sp.c.Emission, lambda)};
        SampledSpectrum throughput{1.0f};
        BounceState     bounceState{};

        const IMaterial                 &firstMaterial{scene.GetMaterial(isect.hit->MatId)};
        const BounceWithConnectionResult firstBounceResult{
            [&]
            {
                RESTIR_BUFFER_METRIC_SCOPE(provider, Metrics::kSubMetrics.ValuesBuf, kBsdfSampleSlot);
                return Detail::SampleBounceWithConnection(firstMaterial, isect, config, bounceState, scene, rng);
            }()};
        const std::optional<BsdfBounceConnection> firstConnection{
            std::holds_alternative<BsdfBounceConnection>(firstBounceResult)
                ? std::make_optional(std::get<BsdfBounceConnection>(firstBounceResult))
                : std::nullopt};
        {
            RESTIR_BUFFER_METRIC_SCOPE(provider, Metrics::kSubMetrics.ValuesBuf, kDirectLightSlot);
            totalRadiance += directLight->Li(isect, scene, rng, lambda, provider, firstConnection, callId);
        }

        if (!std::holds_alternative<BsdfBounceConnection>(firstBounceResult))
        {
            return totalRadiance;
        }

        const BsdfBounceConnection &firstBounceConnection{std::get<BsdfBounceConnection>(firstBounceResult)};
        throughput *= firstBounceConnection.Bounce.ThroughputMul;
        Ray                      currentRay{firstBounceConnection.Bounce.NextRay};
        std::optional<HitRecord> nextHit{firstBounceConnection.Hit};

        for (int bounce{1}; bounce < _maxDepth; ++bounce)
        {
            if (!nextHit.has_value())
            {
                break;
            }

            HitRecord        bounceHit{*nextHit};
            const IMaterial &material{scene.GetMaterial(bounceHit.MatId)};
            BSDFClosure      c{material.GetClosure(bounceHit)};
            if (!_settings.EnableSubsurface)
            {
                c.Subsurface = 0.0f;
            }

            if (c.Opacity < 0.999f && rng.NextFloat() > c.Opacity)
            {
                currentRay.Origin = bounceHit.Position + currentRay.Dir * 1e-4f;
                nextHit           = scene.IntersectScene(currentRay.Origin, currentRay.Dir);
                --bounce;
                continue;
            }

            const bool isInside{GfDot(c.Normal, currentRay.Dir) > 0.0f};
            BeerAbsorption(throughput, c, bounceHit.Depth, isInside, lambda);
            GfVec3f shadingNormal{c.Normal};
            if (isInside)
            {
                shadingNormal = -shadingNormal;
            }

            const std::unique_ptr<IBSDF> bsdfOwner{material.CreateBSDF(BSDFClosure{c})};
            const ShadingPoint           bounceSp{*bsdfOwner, c, shadingNormal, lambda, isInside};
            const RayIntersection        bounceIsect{currentRay, bounceHit, bounceSp};

            const BounceWithConnectionResult bounceResult{
                [&]
                {
                    RESTIR_BUFFER_METRIC_SCOPE(provider, Metrics::kSubMetrics.ValuesBuf, kBsdfSampleSlot);
                    return Detail::SampleBounceWithConnection(material, bounceIsect, config, bounceState, scene, rng);
                }()};
            const std::optional<BsdfBounceConnection> bsdfConnection{
                std::holds_alternative<BsdfBounceConnection>(bounceResult)
                    ? std::make_optional(std::get<BsdfBounceConnection>(bounceResult))
                    : std::nullopt};
            {
                RESTIR_BUFFER_METRIC_SCOPE(provider, Metrics::kSubMetrics.ValuesBuf, kDirectLightSlot);
                totalRadiance += throughput * directLight->Li(bounceIsect, scene, rng, lambda, provider, bsdfConnection,
                                                              {callId.id + callId.stride, callId.stride});
            }

            if (!std::holds_alternative<BsdfBounceConnection>(bounceResult))
            {
                break;
            }

            const BsdfBounceConnection &resolvedConnection{std::get<BsdfBounceConnection>(bounceResult)};
            throughput *= resolvedConnection.Bounce.ThroughputMul;
            currentRay = resolvedConnection.Bounce.NextRay;
            nextHit    = resolvedConnection.Hit;

            if (resolvedConnection.Bounce.SkipRoulette)
            {
                continue;
            }

            if (bounce > 3)
            {
                const float p{std::clamp(throughput.Max() * _settings.RouletteAggressiveness, 0.0f, 1.0f)};
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
