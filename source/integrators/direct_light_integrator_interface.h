#pragma once

#include "integrator.h"
#include "materials/material.h"

#include <optional>

namespace Restir
{

    struct BsdfBounceConnection
    {
        BsdfBounceSample         Bounce{};
        std::optional<HitRecord> Hit{};
    };

    using BounceWithConnectionResult = std::variant<BsdfBounceConnection, BounceSampleResult>;

    namespace Detail
    {

        [[nodiscard]] inline BounceWithConnectionResult
        SampleBounceWithConnection(const IMaterial &material, const ShadingPoint &surface, const BounceConfig &config,
                                   BounceState &bounceState, const IScene &scene, Rng &rng)
        {
            const BounceSampleResult bounce{material.SampleBounce(surface, config, bounceState, rng)};

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

        [[nodiscard]] virtual SampledSpectrum Li(const ShadingPoint &surface, const IScene &scene, Rng &rng,
                                                 const std::optional<BsdfBounceConnection> &bsdfConnection) const = 0;
    };

} // namespace Restir
