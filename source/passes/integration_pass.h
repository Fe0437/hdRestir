#pragma once

#include "integrator.h"
#include "not_null_unique_ptr.h"
#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

namespace Restir
{

    // Manages the tiled pixel iteration loop.
    // Calls _integrator->GetBufferStager() once before the parallel pixel loop
    // to declare persistent per-pixel buffers, if a stager is present.
    class IntegrationPass : public RenderPass
    {
      public:
        IntegrationPass(std::string name, NotNullUniquePtr<IIntegrator> &&integrator)
            : RenderPass{std::move(name)}, _integrator{std::move(integrator)}
        {
        }

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {std::string{kGBufferOutputName}};
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {std::string{kColorOutputName}};
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return {std::string{kGBufferOutputName}};
        }

        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return {std::string{kColorOutputName}};
        }

      protected:
        void                          _execute(RenderContext &ctx) override;
        NotNullUniquePtr<IIntegrator> _integrator;
    };

} // namespace Restir
