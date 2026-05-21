#include "post_process_pass.h"

#include "debug.h"
#include "post_process.h"
#include "output_names.h"

namespace Restir {

void PostProcessPass::Execute(RenderContext& ctx)
{
    DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "Color must be present before PostProcessPass");

    if (!_config.EnableLensFlare && _config.ChromaticAberration <= 0.0f) {
        return;
    }

    auto framebuffer{ctx.buf<GfVec4f>(kColorOutputName)};
    PostProcess::Run(framebuffer, ctx.width, ctx.height, _config);
}

}  // namespace Restir