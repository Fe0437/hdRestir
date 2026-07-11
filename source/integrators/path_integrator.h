#pragma once

#include "direct_light_integrator_factory.h"
#include "integrator.h"
#include "not_null_unique_ptr.h"

#include <utility>

namespace Restir
{

    struct PathTracePassSettings
    {
        bool  EnableSubsurface{true};
        int   MaxReflectionBounces{8};
        int   MaxRefractionBounces{8};
        bool  RenderIblBackground{true};
        float RouletteAggressiveness{1.0f};
    };

    class PathIntegrator final : public IIntegrator, public IBufferStager
    {
      public:
        explicit PathIntegrator(NotNullUniquePtr<IDirectLightIntegratorFactory> &&factory,
                                PathTracePassSettings settings = {}, int maxDepth = 32);

        [[nodiscard]] SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                         const SampledWavelengths &lambda, IBufferProvider &provider,
                                         CallIndex callId) const override;

        [[nodiscard]] IBufferStager *GetBufferStager() override
        {
            return this;
        }

        void PrepareBuffers(IBufferProvider &provider, const IScene &scene) override;

        void SetSettings(PathTracePassSettings settings)
        {
            _settings = std::move(settings);
        }

      private:
        // Called once isect.shadingPoint is guaranteed to be populated.
        [[nodiscard]] SampledSpectrum _li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                          const SampledWavelengths &lambda, IBufferProvider &provider,
                                          CallIndex callId) const;

        PathTracePassSettings                           _settings;
        int                                             _maxDepth;
        NotNullUniquePtr<IDirectLightIntegratorFactory> _factory;
    };

} // namespace Restir
