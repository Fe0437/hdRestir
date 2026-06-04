#include "split_screen.h"

#include "output_names.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Restir {

namespace {


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

    const std::size_t pixelCount{baseCtx.frame.PixelCount()};
    if (!baseCtx.buffers.Has(name)) {
        baseCtx.buffers.Add(name, sizeof(PixelT), pixelCount);
    }

    auto dst{baseCtx.buf<PixelT>(name)};
    auto srcLMutable{const_cast<RenderContext&>(leftCtx).buf<PixelT>(name)};
    auto srcRMutable{const_cast<RenderContext&>(rightCtx).buf<PixelT>(name)};
    const gsl::span<const PixelT> srcL(srcLMutable.data(), srcLMutable.size());
    const gsl::span<const PixelT> srcR(srcRMutable.data(), srcRMutable.size());

    BlitBuffer(dst, srcL, srcR, baseCtx.frame.windowWidth, baseCtx.frame.windowHeight, splitT);
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

            const std::size_t pixelCount{baseCtx.frame.PixelCount()};
            if (!baseCtx.buffers.Has(outputName)) {
                baseCtx.buffers.Add(outputName, sizeof(GfVec4f), pixelCount);
            }

            auto dst{baseCtx.buf<GfVec4f>(outputName)};
            auto srcLMutable{const_cast<RenderContext&>(leftCtx).buf<GfVec4f>(outputName)};
            auto srcRMutable{const_cast<RenderContext&>(rightCtx).buf<GfVec4f>(outputName)};
            const gsl::span<const GfVec4f> srcL(srcLMutable.data(), srcLMutable.size());
            const gsl::span<const GfVec4f> srcR(srcRMutable.data(), srcRMutable.size());
            BlitBuffer(dst, srcL, srcR, baseCtx.frame.windowWidth, baseCtx.frame.windowHeight, splitT);
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
                                             int rightResolutionLevel,
                                             int leftTargetSamples,
                                             int rightTargetSamples)
    : _left(std::move(left))
    , _right(std::move(right))
    , _leftResolutionLevel(leftResolutionLevel)
    , _rightResolutionLevel(rightResolutionLevel)
    , _leftTargetSamples(leftTargetSamples)
    , _rightTargetSamples(rightTargetSamples)
{
    Expects(_left != nullptr);
    Expects(_right != nullptr);
}

bool SplitScreenCompositor::IsConverged() const noexcept
{
    return _leftFrameIndex >= _leftTargetSamples && _rightFrameIndex >= _rightTargetSamples;
}

void SplitScreenCompositor::Execute(RenderContext& baseCtx)
{
    const int splitX{std::clamp(
        static_cast<int>(_splitT * static_cast<float>(baseCtx.frame.windowWidth)),
        0,
        baseCtx.frame.windowWidth - 1)};

    const bool leftDone  = _leftFrameIndex  >= _leftTargetSamples;
    const bool rightDone = _rightFrameIndex >= _rightTargetSamples;

    RenderContext leftCtx{
        .scene        = baseCtx.scene,
        .viewMatrix   = baseCtx.viewMatrix,
        .projMatrix   = baseCtx.projMatrix,
        .frame        = baseCtx.frame.AtResolutionLevel(_leftResolutionLevel),
        .frameIndex   = _leftFrameIndex,
        .rng          = baseCtx.rng,
        .buffers      = FrameBuffersMap{},
        .OutputNames  = baseCtx.OutputNames,
        .cameraParams = baseCtx.cameraParams,
    };
    leftCtx.frame.visibleMaxX = splitX;

    RenderContext rightCtx{
        .scene        = baseCtx.scene,
        .viewMatrix   = baseCtx.viewMatrix,
        .projMatrix   = baseCtx.projMatrix,
        .frame        = baseCtx.frame.AtResolutionLevel(_rightResolutionLevel),
        .frameIndex   = _rightFrameIndex,
        .rng          = baseCtx.rng,
        .buffers      = FrameBuffersMap{},
        .OutputNames  = baseCtx.OutputNames,
        .cameraParams = baseCtx.cameraParams,
    };
    rightCtx.frame.visibleMinX = splitX + 1;

    const Rng savedRng{baseCtx.rng};

    if (!leftDone) {
        _left->Execute(leftCtx);
        ++_leftFrameIndex;
        if (_leftFrameIndex >= _leftTargetSamples) {
            const auto span = leftCtx.buf<GfVec4f>(kColorOutputName);
            _leftFrozenColor.assign(span.begin(), span.end());
        }
        Expects(!leftCtx.frame.NeedsUpscale());
    } else {
        // Frozen: the saved color is already at full resolution (upscaled during last live frame).
        // Override the frame so compositing sees full-res dimensions and no upscale flag.
        leftCtx.frame = baseCtx.frame;
        leftCtx.frame.visibleMaxX = splitX;
        leftCtx.buffers.Add(kColorOutputName, sizeof(GfVec4f), _leftFrozenColor.size());
        const auto dst = leftCtx.buf<GfVec4f>(kColorOutputName);
        std::copy(_leftFrozenColor.begin(), _leftFrozenColor.end(), dst.begin());
    }

    baseCtx.rng = savedRng;

    if (!rightDone) {
        _right->Execute(rightCtx);
        ++_rightFrameIndex;
        if (_rightFrameIndex >= _rightTargetSamples) {
            const auto span = rightCtx.buf<GfVec4f>(kColorOutputName);
            _rightFrozenColor.assign(span.begin(), span.end());
        }
        Expects(!rightCtx.frame.NeedsUpscale());
    } else {
        rightCtx.frame = baseCtx.frame;
        rightCtx.frame.visibleMinX = splitX + 1;
        rightCtx.buffers.Add(kColorOutputName, sizeof(GfVec4f), _rightFrozenColor.size());
        const auto dst = rightCtx.buf<GfVec4f>(kColorOutputName);
        std::copy(_rightFrozenColor.begin(), _rightFrozenColor.end(), dst.begin());
    }

    ComposeRequestedOutputs(baseCtx, leftCtx, rightCtx, _splitT);
}

void SplitScreenCompositor::ClearPersistentBuffers()
{
    _left->ClearPersistentBuffers();
    _right->ClearPersistentBuffers();
    _leftFrameIndex  = 0;
    _rightFrameIndex = 0;
    _leftFrozenColor.clear();
    _rightFrozenColor.clear();
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
