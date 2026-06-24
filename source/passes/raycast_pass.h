#pragma once

#include "hit_record.h"
#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

namespace Restir
{

    class RaycastPass final : public RenderPass
    {
      public:
        explicit RaycastPass(std::vector<std::string> requestedOutputs = {})
            : RenderPass{"RaycastPass"}, _requestedOutputs{std::move(requestedOutputs)}
        {
        }

        [[nodiscard]] static std::vector<std::string> StaticInputs();
        [[nodiscard]] static std::vector<std::string> StaticOutputs();

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return {};
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override;

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        std::vector<std::string> _requestedOutputs;
    };

} // namespace Restir
