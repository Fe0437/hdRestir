#pragma once

#include "integrator.h"
#include "materials/material.h"

#include <cstddef>
#include <optional>
#include <variant>

namespace Restir
{

    using BounceWithConnectionResult = std::variant<BsdfBounceConnection, BounceSampleResult>;

    namespace Detail
    {

        [[nodiscard]] inline BounceWithConnectionResult
        SampleBounceWithConnection(const IMaterial &material, const RayIntersection &isect, const BounceConfig &config,
                                   BounceState &bounceState, const IScene &scene, Rng &rng)
        {
            const ShadingPoint &surface{*isect.shadingPoint};
            const GfVec3f      &hitPos{isect.hit->Position};
            const GfVec3f      &rayDir{isect.ray.Dir};

            const BounceSampleResult bounce{material.SampleBounce(surface, hitPos, rayDir, config, bounceState, rng)};

            if (std::holds_alternative<BounceSampleError>(bounce))
            {
                return BounceWithConnectionResult{std::in_place_type<BounceSampleResult>, bounce};
            }

            const BsdfBounceSample &bounceSample{std::get<BsdfBounceSample>(bounce)};
            return BounceWithConnectionResult{
                std::in_place_type<BsdfBounceConnection>,
                BsdfBounceConnection{
                    .Bounce = bounceSample,
                    .Hit    = scene.IntersectScene(bounceSample.NextRay.Origin, bounceSample.NextRay.Dir),
                }};
        }

    } // namespace Detail

    class IDirectLightIntegrator : public IIntegrator
    {
      public:
        ~IDirectLightIntegrator() override = default;

        // Per-bounce call: same as IIntegrator::Li but with an extra bsdfConnection for MIS.
        // isect.shadingPoint must be populated.
        [[nodiscard]] virtual SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths &lambda, IBufferProvider &provider,
                                                 const std::optional<BsdfBounceConnection> &bsdfConnection,
                                                 CallIndex                                  callId) const = 0;
    };

} // namespace Restir
