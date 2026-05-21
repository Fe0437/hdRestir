#include "fill_pass.h"

#include "output_names.h"

#include <cstddef>

namespace Restir {

void FillPass::Execute(RenderContext& ctx)
{
    const std::size_t pixelCount{static_cast<std::size_t>(ctx.width) * static_cast<std::size_t>(ctx.height)};
    const auto outputs{Outputs()};
    const auto outputName{outputs.empty() ? std::string{kColorOutputName} : outputs.front()};
    if (!ctx.buffers.Has(outputName)) {
        ctx.buffers.Add(outputName, sizeof(GfVec4f), pixelCount);
    }

    auto framebuffer{ctx.buf<GfVec4f>(outputName)};
    for (auto& pixel : framebuffer) {
        pixel = _color;
    }
}

}  // namespace Restir
