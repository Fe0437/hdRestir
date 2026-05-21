#pragma once

#include "post_process.h"
#include "render_pass.h"
#include "output_names.h"

#include <vector>

namespace Restir {

class PostProcessPass final : public RenderPass {
public:
    explicit PostProcessPass(PostProcess::Config config = {})
        : RenderPass{"PostProcessPass"}
        , _config{config}
    {
    }

    [[nodiscard]] static std::vector<std::string> StaticInputs()
    {
        return {std::string{kColorOutputName}};
    }

    [[nodiscard]] static std::vector<std::string> StaticOutputs()
    {
        return {std::string{kColorOutputName}};
    }

    [[nodiscard]] std::vector<std::string> Inputs() const override { return {std::string{kColorOutputName}}; }
    [[nodiscard]] std::vector<std::string> Outputs() const override { return {std::string{kColorOutputName}}; }

    void Execute(RenderContext& ctx) override;

    void SetConfig(PostProcess::Config config) { _config = config; }

private:
    PostProcess::Config _config{};
};

}  // namespace Restir