#pragma once

#include "integrator.h"
#include "not_null_unique_ptr.h"
#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

namespace Restir {

// Manages the tiled pixel iteration loop — analogous to PBRT's ImageTileIntegrator.
// that computes the per-sample radiance. Both share this loop without duplication.
class IntegrationPass : public RenderPass {
public:
    IntegrationPass(std::string name, NotNullUniquePtr<IIntegrator>&& integrator)
        : RenderPass{std::move(name)}
        , _integrator{std::move(integrator)}
    {}

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

    void Execute(RenderContext& ctx) override;

protected:
    NotNullUniquePtr<IIntegrator> _integrator;
};

}  // namespace Restir
