#pragma once

#include "clonable.h"
#include "direct_light_integrator_factory.h"
#include "direct_light_integrator_interface.h"
#include "lighting_core/light_sampler.h"
#include "materials/material.h"
#include "not_null_unique_ptr.h"
#include "scene_interface.h"
#include "spectrum.h"

#include <optional>

namespace Restir
{

    class MisDirectLightIntegrator final : public IDirectLightIntegrator, public IClonableAs<IDirectLightIntegrator>
    {
      public:
        explicit MisDirectLightIntegrator(NotNullUniquePtr<ILightSampler> &&sampler) : _sampler{std::move(sampler)} {}

        [[nodiscard]] static NotNullUniquePtr<IDirectLightIntegratorFactory> MakeFactory();

        [[nodiscard]] std::unique_ptr<IDirectLightIntegrator> CloneAs() const override
        {
            return std::make_unique<MisDirectLightIntegrator>(NotNullUniquePtr<ILightSampler>{_sampler->CloneAs()});
        }

        // Top-level call: computes shading from hit when shadingPoint is absent.
        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda, IBufferProvider &provider,
                                         CallIndex callId) const override;

        // Per-bounce call: isect.shadingPoint must be populated.
        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda, IBufferProvider &provider,
                                         const std::optional<BsdfBounceConnection> &bsdfConnection,
                                         CallIndex                                  callId) const override;

      private:
        struct MISContrib
        {
            SampledSpectrum Radiance{0.0f};
            float           PNee{0.0f};
            float           PBsdf{0.0f};
            bool            IsDelta{false};
        };

        [[nodiscard]] MISContrib _evaluateNEE(const RayIntersection &isect, const ILight &light,
                                              const LightSample &lightSample, const IScene &scene) const;

        [[nodiscard]] SampledSpectrum _integrateNEE(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                    bool useBsdfTechnique) const;

        [[nodiscard]] SampledSpectrum _integrateBSDFConnection(const RayIntersection      &isect,
                                                               const BsdfBounceConnection &connection,
                                                               const IScene               &scene) const;

        NotNullUniquePtr<ILightSampler> _sampler;
    };

} // namespace Restir
