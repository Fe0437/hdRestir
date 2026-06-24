#include "fill_pass.h"

#include "output_names.h"

#include <cstddef>

namespace Restir
{

    void FillPass::_execute(RenderContext &ctx)
    {
        const std::size_t pixelCount{ctx.frame.PixelCount()};
        const auto        outputs{Outputs()};
        const auto        outputName{outputs.empty() ? std::string{kColorOutputName} : outputs.front()};
        if (!ctx.buffers.Has(outputName))
        {
            ctx.buffers.Add(outputName, sizeof(GfVec4f), pixelCount);
        }

        auto framebuffer{ctx.buf<GfVec4f>(outputName)};
        for (auto &pixel : framebuffer)
        {
            pixel = _color;
        }
    }

} // namespace Restir
