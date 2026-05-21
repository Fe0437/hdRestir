#include "accumulation_pass.h"

#include "debug.h"
#include "render_context.h"

#include <algorithm>
#include <gsl/gsl>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

namespace {

float GetLuminance(const GfVec4f& color)
{
    return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
}

void ApplyFireflySuppression(gsl::span<GfVec4f> framebuffer, int width, int height)
{
    for (int y{0}; y < height; ++y) {
        for (int x{0}; x < width; ++x) {
            const std::size_t idx{static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)};
            const float lum{GetLuminance(framebuffer[idx])};

            float maxNeighborLum{0.0f};
            for (int dy{-1}; dy <= 1; ++dy) {
                for (int dx{-1}; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }

                    const int nx{std::clamp(x + dx, 0, width - 1)};
                    const int ny{std::clamp(y + dy, 0, height - 1)};
                    const std::size_t nIdx{static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx)};
                    maxNeighborLum = std::max(maxNeighborLum, GetLuminance(framebuffer[nIdx]));
                }
            }

            const float threshold{maxNeighborLum * 4.0f + 0.5f};
            if (lum > threshold && lum > 0.0f) {
                const float scale{threshold / lum};
                framebuffer[idx][0] *= scale;
                framebuffer[idx][1] *= scale;
                framebuffer[idx][2] *= scale;
            }
        }
    }
}

#if METRICS_ENABLED
[[nodiscard]] bool ShouldLogEstimatorVariance(int sampleCount)
{
    return sampleCount >= 16 && ((sampleCount & (sampleCount - 1)) == 0);
}
#endif

}  // namespace

void AccumulationPass::Execute(RenderContext& ctx)
{
    const int         width  = ctx.width;
    const int         height = ctx.height;
    const std::size_t count  = gsl::narrow_cast<std::size_t>(width * height);

    const bool cameraChanged = (ctx.viewMatrix != _lastViewMatrix ||
                                ctx.projMatrix != _lastProjMatrix);
    const bool sizeChanged   = (width != _lastWidth || height != _lastHeight);

    if (cameraChanged || sizeChanged) {
        Reset();
    }

    if (_accumulator.size() != count) {
        _accumulator.assign(count, GfVec4f{0.0f, 0.0f, 0.0f, 0.0f});
        _sampleCount = 0;
#if METRICS_ENABLED
        _luminanceSum.assign(count, 0.0);
        _luminanceSumSquares.assign(count, 0.0);
#endif
    }

    DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "Color must be present (produced by PathTracePass)");
    auto fb{ ctx.buf<GfVec4f>(kColorOutputName) };

    if (_enableFireflyFilter) {
        ApplyFireflySuppression(fb, width, height);
    }

    ++_sampleCount;
    const float invN = 1.0f / static_cast<float>(_sampleCount);
    for (std::size_t i = 0; i < count; ++i) {
#if METRICS_ENABLED
        const double luminance{static_cast<double>(GetLuminance(fb[i]))};
        _luminanceSum[i] += luminance;
        _luminanceSumSquares[i] += luminance * luminance;
#endif
        _accumulator[i] += fb[i];
        fb[i] = _accumulator[i] * invN;
    }

#if METRICS_ENABLED
    if (ShouldLogEstimatorVariance(_sampleCount)) {
        double meanEstimatorVariance{0.0};
        double maxEstimatorVariance{0.0};
        for (std::size_t i = 0; i < count; ++i) {
            const double n{static_cast<double>(_sampleCount)};
            const double mean{_luminanceSum[i] / n};
            const double centeredSumSquares{
                _luminanceSumSquares[i] - n * mean * mean};
            const double sampleVariance{std::max(0.0, centeredSumSquares / (n - 1.0))};
            const double estimatorVariance{sampleVariance / n};
            meanEstimatorVariance += estimatorVariance;
            maxEstimatorVariance = std::max(maxEstimatorVariance, estimatorVariance);
        }

        meanEstimatorVariance /= static_cast<double>(count);
    METRICS_LOG(
            "AccumulationPass: samples=%d mean estimator variance=%g max estimator variance=%g",
            _sampleCount,
            meanEstimatorVariance,
            maxEstimatorVariance);
    }
#endif

    _lastViewMatrix = ctx.viewMatrix;
    _lastProjMatrix = ctx.projMatrix;
    _lastWidth      = width;
    _lastHeight     = height;
}

void AccumulationPass::Reset() noexcept
{
    _accumulator.clear();
    _sampleCount = 0;
#if METRICS_ENABLED
    _luminanceSum.clear();
    _luminanceSumSquares.clear();
#endif
}

}  // namespace Restir
