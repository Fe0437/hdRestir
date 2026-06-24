#pragma once

#include "integration_pass.h"
#include "path_integrator.h"
#include "ris_direct_light_integrator.h"

#include <memory>

namespace Restir
{

    class RISPathTracePass final : public IntegrationPass
    {
      public:
        explicit RISPathTracePass(int candidateCount = 16, bool useReservoir = true,
                                  PathTracePassSettings settings = {}, int maxDepth = 32)
            : RISPathTracePass{std::make_unique<PathIntegrator>(
                  RisDirectLightIntegrator::MakeFactory(candidateCount, useReservoir), settings, maxDepth)}
        {
        }

      private:
        explicit RISPathTracePass(std::unique_ptr<PathIntegrator> integrator)
            : IntegrationPass{"RISPathTracePass", NotNullUniquePtr<IIntegrator>{std::move(integrator)}}
        {
        }
    };

} // namespace Restir
