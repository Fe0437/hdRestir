#pragma once

#include "integration_pass.h"
#include "mis_direct_light_integrator.h"
#include "path_integrator.h"

#include <memory>

namespace Restir
{

    class PathTracePass final : public IntegrationPass
    {
      public:
        explicit PathTracePass(PathTracePassSettings settings = {}, int maxDepth = 32)
            : PathTracePass{
                  std::make_unique<PathIntegrator>(MisDirectLightIntegrator::MakeFactory(), settings, maxDepth)}
        {
        }

        void SetSettings(PathTracePassSettings settings)
        {
            _pathIntegrator->SetSettings(std::move(settings));
        }

      private:
        explicit PathTracePass(std::unique_ptr<PathIntegrator> integrator)
            : PathTracePass{integrator.get(), std::move(integrator)}
        {
        }

        PathTracePass(PathIntegrator *ptr, std::unique_ptr<PathIntegrator> integrator)
            : IntegrationPass{"PathTracePass", NotNullUniquePtr<IIntegrator>{std::move(integrator)}},
              _pathIntegrator{ptr}
        {
        }

        PathIntegrator *_pathIntegrator;
    };

} // namespace Restir
