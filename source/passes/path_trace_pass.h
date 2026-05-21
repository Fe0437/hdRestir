#pragma once

#include "render_pass.h"
#include "output_names.h"

#include <vector>

namespace Restir {

struct PathTracePassSettings {
    bool EnableSubsurface{true};
    int MaxReflectionBounces{8};
    int MaxRefractionBounces{8};
    bool RenderIblBackground{true};
};

class PathTracePass final : public RenderPass {
public:
    explicit PathTracePass(PathTracePassSettings settings = {}, int maxDepth = 32)
        : RenderPass{"PathTracePass"}
        , _settings{settings}
        , _maxDepth{maxDepth}
    {}

    [[nodiscard]] static std::vector<std::string> StaticInputs()
    {
        return {std::string{kGBufferOutputName}};
    }

    [[nodiscard]] static std::vector<std::string> StaticOutputs()
    {
        return {std::string{kColorOutputName}};
    }

    [[nodiscard]] std::vector<std::string> Inputs() const override { return {std::string{kGBufferOutputName}}; }
    [[nodiscard]] std::vector<std::string> Outputs() const override { return {std::string{kColorOutputName}}; }

    void Execute(RenderContext& ctx) override;

    void SetSettings(PathTracePassSettings settings) { _settings = settings; }

private:
    PathTracePassSettings _settings{};
    int _maxDepth{32};
};

}  // namespace Restir
