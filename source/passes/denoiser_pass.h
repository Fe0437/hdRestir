#pragma once

#include "denoiser.h"
#include "render_pass.h"
#include "output_names.h"

#include <vector>

namespace Restir {

class DenoiserPass final : public RenderPass {
public:
    explicit DenoiserPass(Denoiser::Config config = {})
        : RenderPass{"DenoiserPass"}
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

    void SetConfig(Denoiser::Config config) { _config = config; }

private:
    Denoiser::Config _config{};
};

}  // namespace Restir