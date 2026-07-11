#include "render_pass.h"

#if METRICS_ENABLED
#include "metrics_on_buffers.h"
#endif

namespace Restir
{

    RenderPass::RenderPass(std::string name) noexcept : _name(std::move(name)) {}

    void RenderPass::Execute(RenderContext &ctx)
    {
#if METRICS_ENABLED
        Metrics::ScopedMetricTimer timer{ctx.buffers, _name};
#endif
        _execute(ctx);
    }

} // namespace Restir
