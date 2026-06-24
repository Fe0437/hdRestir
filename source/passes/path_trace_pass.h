#pragma once

#include "integration_pass.h"
#include "lighting_core/uniform_light_sampler.h"
#include "mis_direct_light_integrator.h"
#include "path_integrator.h"

#include <memory>

namespace Restir
{

    class PathTracePass final : public IntegrationPass
    {
      public:
        explicit PathTracePass(PathTracePassSettings settings = {}, int maxDepth = 32)
            : PathTracePass{NotNullUniquePtr<PathIntegrator>{std::make_unique<PathIntegrator>(
                  [](const IScene &scene)
                  {
                      return NotNullUniquePtr<IDirectLightIntegrator>{std::make_unique<MisDirectLightIntegrator>(
                          NotNullUniquePtr<ILightSampler>{std::make_unique<UniformLightSampler>(scene.GetLights())})};
                  },
                  settings, maxDepth)}}
        {
        }

        void SetSettings(PathTracePassSettings settings)
        {
            _pathIntegrator->SetSettings(std::move(settings));
        }

      private:
        explicit PathTracePass(NotNullUniquePtr<PathIntegrator> &&integrator)
            : IntegrationPass{"PathTracePass", std::move(integrator)},
              _pathIntegrator{static_cast<PathIntegrator *>(_integrator.get())}
        {
        }

        PathIntegrator *_pathIntegrator;
    };

} // namespace Restir
