#include "render_pass.h"

#include "output_names.h"

#if DEBUG_ENABLED
#include <chrono>
#endif

namespace Restir {

RenderPass::RenderPass(std::string name) noexcept
    : _name(std::move(name))
{
}

void RenderPass::Execute(RenderContext& ctx)
{
#if DEBUG_ENABLED
    const auto t0{std::chrono::steady_clock::now()};
#endif
    _execute(ctx);
#if DEBUG_ENABLED
    const float ms{std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - t0).count()};
    const std::size_t idx{ctx.buffers.Has(kPassTimingOutputName)
        ? ctx.buffers.GetFrameBuffer(kPassTimingOutputName).Count()
        : 0};
    ctx.buffers.Add(kPassTimingOutputName, sizeof(float), idx + 1);
    ctx.buf<float>(kPassTimingOutputName)[idx] = ms;
#endif
}

}  // namespace Restir
