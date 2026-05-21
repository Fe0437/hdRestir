#pragma once

#include "render_pass.h"
#include "output_names.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec4f.h"

#include <vector>

namespace Restir {

class AccumulationPass final : public RenderPass {
public:
    explicit AccumulationPass(bool enableFireflyFilter = true)
        : RenderPass{"AccumulationPass"}
        , _enableFireflyFilter{enableFireflyFilter}
    {}

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
    void Reset() noexcept;

    [[nodiscard]] int SampleCount() const noexcept { return _sampleCount; }

private:
    bool                 _enableFireflyFilter{true};
    std::vector<GfVec4f> _accumulator;
    int                  _sampleCount{0};
    GfMatrix4d           _lastViewMatrix{1.0};
    GfMatrix4d           _lastProjMatrix{1.0};
    int                  _lastWidth{0};
    int                  _lastHeight{0};
#if METRICS_ENABLED
    std::vector<double>  _luminanceSum;
    std::vector<double>  _luminanceSumSquares;
#endif

};

}  // namespace Restir
