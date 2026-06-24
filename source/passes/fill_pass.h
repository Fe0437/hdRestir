#pragma once

#include "output_names.h"
#include "pxr/base/gf/vec4f.h"
#include "render_pass.h"
#include "render_pipeline.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Restir
{

    class FillPass final : public RenderPass
    {
      public:
        explicit FillPass(GfVec4f color = GfVec4f(0.5f, 0.0f, 0.5f, 1.0f)) : RenderPass{"FillPass"}, _color(color) {}

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {};
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {std::string{kColorOutputName}};
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return {};
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return {std::string{kColorOutputName}};
        }

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        GfVec4f _color;
    };

    inline std::unique_ptr<RenderPipeline> makeFillPipeline(std::string &&name  = "Fill",
                                                            GfVec4f       color = GfVec4f(0.5f, 0.0f, 0.5f, 1.0f))
    {
        auto pipeline{std::make_unique<RenderPipeline>(std::move(name))};
        pipeline->Add(std::make_unique<FillPass>(color));
        return pipeline;
    }

} // namespace Restir
