#pragma once

#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

namespace Restir
{

    class UpscalePass final : public RenderPass
    {
      public:
        explicit UpscalePass(std::vector<std::string> outputNames = {})
            : RenderPass{"UpscalePass"}, _outputNames{std::move(outputNames)}
        {
        }

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {
                std::string{kColorOutputName},
                std::string{kDepthOutputName},
                std::string{kAlbedoOutputName},
                std::string{kNormalOutputName},
            };
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {
                std::string{kColorOutputName},
                std::string{kDepthOutputName},
                std::string{kAlbedoOutputName},
                std::string{kNormalOutputName},
            };
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return _outputNames;
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return _outputNames;
        }

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        std::vector<std::string> _outputNames;
    };

} // namespace Restir