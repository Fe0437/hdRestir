#include "upscale_pass.h"

#include "frame_buffer_map.h"
#include "output_names.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include <algorithm>
#include <vector>

namespace Restir {

namespace {

template<typename PixelT>
[[nodiscard]] PixelT SampleNearest(const std::vector<PixelT>& src,
                                   int srcWidth,
                                   int srcHeight,
                                   int dstX,
                                   int dstY,
                                   int dstWidth,
                                   int dstHeight)
{
    Expects(srcWidth > 0);
    Expects(srcHeight > 0);
    Expects(dstWidth > 0);
    Expects(dstHeight > 0);

    const int srcX{std::min(srcWidth - 1, dstX * srcWidth / dstWidth)};
    const int srcY{std::min(srcHeight - 1, dstY * srcHeight / dstHeight)};
    const std::size_t srcIndex{
        static_cast<std::size_t>(srcY) * static_cast<std::size_t>(srcWidth) + static_cast<std::size_t>(srcX)};
    return src[srcIndex];
}

template<typename PixelT>
void UpscaleBuffer(FrameBuffersMap& buffers,
                   std::string_view name,
                   int srcWidth,
                   int srcHeight,
                   int dstWidth,
                   int dstHeight)
{
    if (!buffers.Has(name)) {
        return;
    }

    const auto srcSpan{buffers.Get<PixelT>(name)};
    std::vector<PixelT> src(srcSpan.begin(), srcSpan.end());
    const std::size_t dstCount{
        static_cast<std::size_t>(dstWidth) * static_cast<std::size_t>(dstHeight)};

    buffers.Add(name, sizeof(PixelT), dstCount);
    auto dst{buffers.Get<PixelT>(name)};

    for (int y{0}; y < dstHeight; ++y) {
        for (int x{0}; x < dstWidth; ++x) {
            const std::size_t idx{
                static_cast<std::size_t>(y) * static_cast<std::size_t>(dstWidth) + static_cast<std::size_t>(x)};
            dst[idx] = SampleNearest(src, srcWidth, srcHeight, x, y, dstWidth, dstHeight);
        }
    }
}

}  // namespace

void UpscalePass::Execute(RenderContext& ctx)
{
    if (ctx.width == ctx.outputWidth && ctx.height == ctx.outputHeight) {
        return;
    }

    const int srcWidth{ctx.width};
    const int srcHeight{ctx.height};
    const int dstWidth{ctx.outputWidth};
    const int dstHeight{ctx.outputHeight};

    for (const auto& outputName : Outputs()) {
        switch (GetOutputDataType(outputName)) {
        case OutputDataType::Color4:
            UpscaleBuffer<GfVec4f>(ctx.buffers, outputName, srcWidth, srcHeight, dstWidth, dstHeight);
            break;
        case OutputDataType::Vec3:
            UpscaleBuffer<GfVec3f>(ctx.buffers, outputName, srcWidth, srcHeight, dstWidth, dstHeight);
            break;
        case OutputDataType::Float1:
            UpscaleBuffer<float>(ctx.buffers, outputName, srcWidth, srcHeight, dstWidth, dstHeight);
            break;
        case OutputDataType::Unknown:
            DBG_LOG("UpscalePass: skipping unsupported output '%s'", outputName.c_str());
            break;
        }
    }

    ctx.width = dstWidth;
    ctx.height = dstHeight;
}

}  // namespace Restir