#include "split_screen.h"

#include "output_names.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Restir {

namespace {

[[nodiscard]] int ClampResolutionLevel(int level)
{
    return std::clamp(level, 0, 4);
}

[[nodiscard]] int ResolutionDivisor(int level)
{
    return 1 << ClampResolutionLevel(level);
}

[[nodiscard]] int ScaledDimension(int dimension, int level)
{
    Expects(dimension > 0);
    const int divisor{ResolutionDivisor(level)};
    return std::max(1, (dimension + divisor - 1) / divisor);
}

template<typename PixelT>
void BlitBuffer(gsl::span<PixelT> dst,
                gsl::span<const PixelT> srcL,
                gsl::span<const PixelT> srcR,
                int width,
                int height,
                float splitT)
{
    Expects(width > 0);
    Expects(height > 0);

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    Expects(dst.size() >= pixelCount);
    Expects(srcL.size() >= pixelCount);
    Expects(srcR.size() >= pixelCount);

    const int splitX = std::clamp(static_cast<int>(splitT * static_cast<float>(width)), 0, width - 1);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if constexpr (std::is_same_v<PixelT, GfVec4f>) {
                if (x == splitX) {
                    dst[idx] = GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);
                    continue;
                }
            }

            const float normalizedX = static_cast<float>(x) / static_cast<float>(width);
            dst[idx] = (normalizedX < splitT) ? srcL[idx] : srcR[idx];
        }
    }
}

template<typename PixelT>
void ComposeOptionalBuffer(RenderContext& baseCtx,
                           const RenderContext& leftCtx,
                           const RenderContext& rightCtx,
                           std::string_view name,
                           float splitT)
{
    if (!leftCtx.buffers.Has(name) || !rightCtx.buffers.Has(name)) {
        return;
    }

    const std::size_t pixelCount{
        static_cast<std::size_t>(baseCtx.width) * static_cast<std::size_t>(baseCtx.height)};
    if (!baseCtx.buffers.Has(name)) {
        baseCtx.buffers.Add(name, sizeof(PixelT), pixelCount);
    }

    auto dst{baseCtx.buf<PixelT>(name)};
    auto srcLMutable{const_cast<RenderContext&>(leftCtx).buf<PixelT>(name)};
    auto srcRMutable{const_cast<RenderContext&>(rightCtx).buf<PixelT>(name)};
    const gsl::span<const PixelT> srcL(srcLMutable.data(), srcLMutable.size());
    const gsl::span<const PixelT> srcR(srcRMutable.data(), srcRMutable.size());

    BlitBuffer(dst,
               srcL,
               srcR,
               baseCtx.width,
               baseCtx.height,
               splitT);
}

void ComposeRequestedOutputs(RenderContext& baseCtx,
                             const RenderContext& leftCtx,
                             const RenderContext& rightCtx,
                             float splitT)
{
    for (const auto outputName : baseCtx.OutputNames) {
        switch (GetOutputDataType(outputName)) {
        case OutputDataType::Color4: {
            if (!leftCtx.buffers.Has(outputName) || !rightCtx.buffers.Has(outputName)) {
                continue;
            }

            const std::size_t pixelCount{
                static_cast<std::size_t>(baseCtx.width) * static_cast<std::size_t>(baseCtx.height)};
            if (!baseCtx.buffers.Has(outputName)) {
                baseCtx.buffers.Add(outputName, sizeof(GfVec4f), pixelCount);
            }

            auto dst{baseCtx.buf<GfVec4f>(outputName)};
            auto srcLMutable{const_cast<RenderContext&>(leftCtx).buf<GfVec4f>(outputName)};
            auto srcRMutable{const_cast<RenderContext&>(rightCtx).buf<GfVec4f>(outputName)};
            const gsl::span<const GfVec4f> srcL(srcLMutable.data(), srcLMutable.size());
            const gsl::span<const GfVec4f> srcR(srcRMutable.data(), srcRMutable.size());
            BlitBuffer(dst, srcL, srcR, baseCtx.width, baseCtx.height, splitT);
            break;
        }
        case OutputDataType::Vec3:
            ComposeOptionalBuffer<GfVec3f>(baseCtx, leftCtx, rightCtx, outputName, splitT);
            break;
        case OutputDataType::Float1:
            ComposeOptionalBuffer<float>(baseCtx, leftCtx, rightCtx, outputName, splitT);
            break;
        case OutputDataType::Unknown:
            DBG_LOG("SplitScreenCompositor: skipping unsupported output '%s'", outputName.c_str());
            break;
        }
    }
}

}  // namespace

SplitScreenCompositor::SplitScreenCompositor(std::unique_ptr<RenderPipeline> left,
                                             std::unique_ptr<RenderPipeline> right,
                                             int leftResolutionLevel,
                                             int rightResolutionLevel)
    : _left(std::move(left))
    , _right(std::move(right))
    , _leftResolutionLevel(ClampResolutionLevel(leftResolutionLevel))
    , _rightResolutionLevel(ClampResolutionLevel(rightResolutionLevel))
{
    Expects(_left != nullptr);
    Expects(_right != nullptr);
}

void SplitScreenCompositor::execute(RenderContext& baseCtx)
{
    RenderContext leftCtx{
        baseCtx.scene,
        baseCtx.viewMatrix,
        baseCtx.projMatrix,
        ScaledDimension(baseCtx.width, _leftResolutionLevel),
        ScaledDimension(baseCtx.height, _leftResolutionLevel),
        baseCtx.outputWidth,
        baseCtx.outputHeight,
        baseCtx.frameIndex,
        baseCtx.rng,
        FrameBuffersMap{},
        baseCtx.OutputNames,
        baseCtx.cameraParams
    };

    RenderContext rightCtx{
        baseCtx.scene,
        baseCtx.viewMatrix,
        baseCtx.projMatrix,
        ScaledDimension(baseCtx.width, _rightResolutionLevel),
        ScaledDimension(baseCtx.height, _rightResolutionLevel),
        baseCtx.outputWidth,
        baseCtx.outputHeight,
        baseCtx.frameIndex,
        baseCtx.rng,
        FrameBuffersMap{},
        baseCtx.OutputNames,
        baseCtx.cameraParams
    };

    const Rng rng {baseCtx.rng};
    _left->execute(leftCtx);
    baseCtx.rng = rng;
    _right->execute(rightCtx);

    Expects(leftCtx.width == baseCtx.outputWidth);
    Expects(leftCtx.height == baseCtx.outputHeight);
    Expects(rightCtx.width == baseCtx.outputWidth);
    Expects(rightCtx.height == baseCtx.outputHeight);

    ComposeRequestedOutputs(baseCtx, leftCtx, rightCtx, _splitT);
}

void SplitScreenCompositor::swapSides() noexcept
{
    std::swap(_left, _right);
}

void SplitScreenCompositor::setSplitPosition(float t) noexcept
{
    Expects(t >= 0.0f && t <= 1.0f);
    _splitT = t;
}

void SplitScreenCompositor::blit(gsl::span<GfVec4f> dst,
                                 gsl::span<const GfVec4f> srcL,
                                 gsl::span<const GfVec4f> srcR,
                                 int width,
                                 int height,
                                 float splitT)
{
    BlitBuffer(dst, srcL, srcR, width, height, splitT);
}

}  // namespace Restir
