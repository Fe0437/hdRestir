#include "gpu_accumulation_pass.h"

#if GPU_ENABLED

#include "debug.h"
#include "pxr/base/gf/vec4f.h"
#include "render_context.h"

#if METRICS_ENABLED
#include <algorithm>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{
    namespace
    {
        // The kernel's TU is built -fno-rtti and so cannot name GfVec4f at all
        // (see gpu_accumulation_kernel.h); it takes the pixel buffers as bytes.
        static_assert(sizeof(GfVec4f) == Gpu::kBytesPerPixel);
    } // namespace

    void GpuAccumulationPass::_execute(RenderContext &ctx)
    {
        const auto        width{static_cast<std::uint32_t>(ctx.frame.RenderedWidth())};
        const auto        height{static_cast<std::uint32_t>(ctx.frame.RenderedHeight())};
        const std::size_t count{static_cast<std::size_t>(width) * height};

        DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "Color must be present (produced by PathTracePass)");
        auto fb{ctx.buf<GfVec4f>(kColorOutputName)};

        // Persistent running-sum state, injected/extracted by RenderPipeline
        // alongside every other pass's persistent buffers — nothing here
        // survives a camera move or scene edit except through this store,
        // exactly like the CPU AccumulationPass. Shares kAccumColorBuf with
        // the CPU pass (both accumulate GfVec4f), so toggling UseGpu
        // mid-session doesn't lose accumulated color.
        ctx.buffers.AddOrGetPersistent(kAccumColorBuf, sizeof(GfVec4f), count);
        auto accum{ctx.buf<GfVec4f>(kAccumColorBuf)};

#if METRICS_ENABLED
        ctx.buffers.AddOrGetPersistent(kGpuAccumLumSumBuf, sizeof(float), count);
        ctx.buffers.AddOrGetPersistent(kGpuAccumLumSumSqBuf, sizeof(float), count);
        auto lumSum{ctx.buf<float>(kGpuAccumLumSumBuf)};
        auto lumSumSq{ctx.buf<float>(kGpuAccumLumSumSqBuf)};

        _kernel.RunFrame(gsl::as_writable_bytes(fb), gsl::as_writable_bytes(accum), width, height, static_cast<std::uint32_t>(ctx.frameIndex),
                         _enableFireflyFilter, lumSum, lumSumSq);

        const int sampleCount{ctx.frameIndex + 1};
        if (sampleCount >= 2)
        {
            double meanEstimatorVariance{0.0};
            double maxEstimatorVariance{0.0};
            double meanLuminance{0.0};
            for (std::size_t i{0}; i < count; ++i)
            {
                const double n{static_cast<double>(sampleCount)};
                const double mean{static_cast<double>(lumSum[i]) / n};
                const double centeredSumSquares{static_cast<double>(lumSumSq[i]) - n * mean * mean};
                const double sampleVariance{std::max(0.0, centeredSumSquares / (n - 1.0))};
                const double estimatorVariance{sampleVariance / n};
                meanEstimatorVariance += estimatorVariance;
                maxEstimatorVariance = std::max(maxEstimatorVariance, estimatorVariance);
                meanLuminance += mean;
            }

            meanEstimatorVariance /= static_cast<double>(count);
            meanLuminance /= static_cast<double>(count);
            METRICS_LOG("GpuAccumulationPass: samples=%d mean estimator variance=%g max estimator variance=%g mean "
                        "luminance=%g",
                        sampleCount, meanEstimatorVariance, maxEstimatorVariance, meanLuminance);

            ctx.buffers.AddOrGetPersistent(kVarianceOutputName, sizeof(VarianceStats), 1);
            ctx.buf<VarianceStats>(kVarianceOutputName)[0] = VarianceStats{
                .mean = static_cast<float>(meanEstimatorVariance),
                .max  = static_cast<float>(maxEstimatorVariance),
            };
        }
#else
        _kernel.RunFrame(gsl::as_writable_bytes(fb), gsl::as_writable_bytes(accum), width, height, static_cast<std::uint32_t>(ctx.frameIndex),
                         _enableFireflyFilter);
#endif
    }

} // namespace Restir

#endif // GPU_ENABLED
