#pragma once

#include "debug.h"
#include "render_pass.h"

#include <gsl/gsl>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Restir {

class RenderPipeline final {
public:
    explicit RenderPipeline(std::string&& name) noexcept;

    RenderPipeline& add(std::unique_ptr<RenderPass> pass);
    void            execute(RenderContext& ctx);

    [[nodiscard]] std::string_view name()  const noexcept;
    [[nodiscard]] bool             empty() const noexcept { return _passes.empty(); }
    [[nodiscard]] std::size_t      size()  const noexcept { return _passes.size(); }

private:
    std::string                              _name;
    std::vector<std::unique_ptr<RenderPass>> _passes;
};

}  // namespace Restir
