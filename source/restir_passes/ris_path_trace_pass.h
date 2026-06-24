#pragma once

#include "integration_pass.h"
#include "lighting_core/uniform_light_sampler.h"
#include "path_integrator.h"
#include "ris_direct_light_integrator.h"

#include <memory>
#include <utility>

namespace Restir
{

    class RISPathTracePass final : public IntegrationPass
    {
      public:
        explicit RISPathTracePass(int candidateCount = 16, PathTracePassSettings settings = {}, int maxDepth = 32)
            : RISPathTracePass{NotNullUniquePtr<PathIntegrator>{std::make_unique<PathIntegrator>(
                  [candidateCount](const IScene &scene)
                  {
                      return NotNullUniquePtr<IDirectLightIntegrator>{std::make_unique<RisDirectLightIntegrator>(
                          NotNullUniquePtr<ILightSampler>{std::make_unique<UniformLightSampler>(scene.GetLights())},
                          candidateCount)};
                  },
                  settings, maxDepth)}}
        {
        }

      private:
        explicit RISPathTracePass(NotNullUniquePtr<PathIntegrator> &&integrator)
            : IntegrationPass{"RISPathTracePass", std::move(integrator)}
        {
        }
    };

} // namespace Restir
