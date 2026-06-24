#pragma once

#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

namespace Restir
{

    class AccumulationPass final : public RenderPass
    {
      public:
        explicit AccumulationPass(bool enableFireflyFilter = true)
            : RenderPass{"AccumulationPass"}, _enableFireflyFilter{enableFireflyFilter}
        {
        }

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {std::string{kColorOutputName}};
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {
                std::string{kColorOutputName},
#if DEBUG_ENABLED
                std::string{kVarianceOutputName},
#endif
            };
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return {std::string{kColorOutputName}};
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return {
                std::string{kColorOutputName},
#if DEBUG_ENABLED
                std::string{kVarianceOutputName},
#endif
            };
        }

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        bool _enableFireflyFilter{true};
    };

} // namespace Restir
