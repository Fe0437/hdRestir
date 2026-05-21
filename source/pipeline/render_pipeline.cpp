#include "render_pipeline.h"

namespace Restir {

RenderPipeline::RenderPipeline(std::string&& name) noexcept
    : _name(std::move(name))
{
}

RenderPipeline& RenderPipeline::add(std::unique_ptr<RenderPass> pass)
{
    Expects(pass != nullptr);
    _passes.emplace_back(std::move(pass));
    return *this;
}

std::string_view RenderPipeline::name() const noexcept
{
    return _name;
}

void RenderPipeline::execute(RenderContext& ctx)
{
    if (_passes.empty()) {
        DBG_LOG("[Pipeline %s] no passes scheduled", _name.c_str());
        return;
    }
    for (auto& p : _passes) {
        //DBG_LOG("[Pipeline %s] %s", _name.c_str(), std::string(p->name()).c_str());
        p->Execute(ctx);
    }
}

}  // namespace Restir
