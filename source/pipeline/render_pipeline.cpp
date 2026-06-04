#include "render_pipeline.h"

namespace Restir {

RenderPipeline::RenderPipeline(std::string&& name) noexcept
    : _name(std::move(name))
{
}

RenderPipeline& RenderPipeline::Add(std::unique_ptr<RenderPass> pass)
{
    Expects(pass != nullptr);
    _passes.emplace_back(std::move(pass));
    return *this;
}

std::string_view RenderPipeline::Name() const noexcept
{
    return _name;
}

void RenderPipeline::Execute(RenderContext& ctx)
{
    if (_passes.empty()) {
        DBG_LOG("[Pipeline %s] no passes scheduled", _name.c_str());
        return;
    }
    ctx.buffers.InjectFrom(_persistentStore);
    for (auto& p : _passes) {
        p->Execute(ctx);
    }
    ctx.buffers.ExtractPersistentTo(_persistentStore);
}

void RenderPipeline::ClearPersistentBuffers()
{
    _persistentStore.Clear();
}

}  // namespace Restir
