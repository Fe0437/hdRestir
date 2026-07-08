#include "accumulation_pass.h"

#include "debug.h"
#include "render_context.h"

#include <algorithm>
#include <gsl/gsl>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        float GetLuminance(const GfVec4f &color)
        {
            return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
        }

        void ApplyFireflySuppression(gsl::span<GfVec4f> framebuffer, int width, int height)
        {
            for (int y{0}; y < height; ++y)
            {
                for (int x{0}; x < width; ++x)
                {
                    const std::size_t idx{static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                          static_cast<std::size_t>(x)};
                    const float       lum{GetLuminance(framebuffer[idx])};

                    float maxNeighborLum{0.0f};
                    for (int dy{-1}; dy <= 1; ++dy)
                    {
                        for (int dx{-1}; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0)
                            {
                                continue;
                            }

                            const int         nx{std::clamp(x + dx, 0, width - 1)};
                            const int         ny{std::clamp(y + dy, 0, height - 1)};
                            const std::size_t nIdx{static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) +
                                                   static_cast<std::size_t>(nx)};
                            maxNeighborLum = std::max(maxNeighborLum, GetLuminance(framebuffer[nIdx]));
                        }
                    }

                    const float threshold{maxNeighborLum * 4.0f + 0.5f};
                    if (lum > threshold && lum > 0.0f)
                    {
                        const float scale{threshold / lum};
                        framebuffer[idx][0] *= scale;
                        framebuffer[idx][1] *= scale;
                        framebuffer[idx][2] *= scale;
                    }
                }
            }
        }

    } // namespace

    void AccumulationPass::_execute(RenderContext &ctx)
    {
        const int         width  = ctx.frame.RenderedWidth();
        const int         height = ctx.frame.RenderedHeight();
        const std::size_t count  = gsl::narrow_cast<std::size_t>(width * height);

        DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "Color must be present (produced by PathTracePass)");
        auto fb{ctx.buf<GfVec4f>(kColorOutputName)};

        if (_enableFireflyFilter)
        {
            ApplyFireflySuppression(fb, width, height);
        }

        const int   sampleCount{ctx.frameIndex + 1};
        const float invN{1.0f / static_cast<float>(sampleCount)};

#if DEBUG_ENABLED
        if (ctx.buffers.Has(kPassTimingOutputName))
        {
            auto timings{ctx.buf<float>(kPassTimingOutputName)};
            ctx.buffers.AddOrGetPersistent(kPassSumTimingOutputName, sizeof(float), timings.size());
            auto sumData{ctx.buf<float>(kPassSumTimingOutputName)};
            for (std::size_t i = 0; i < timings.size(); ++i)
            {
                sumData[i] += timings[i];
            }
        }
#endif

        ctx.buffers.AddOrGetPersistent(kAccumColorBuf, sizeof(GfVec4f), count);
        auto accum{ctx.buf<GfVec4f>(kAccumColorBuf)};

#if METRICS_ENABLED
        ctx.buffers.AddOrGetPersistent(kAccumLumSumBuf, sizeof(double), count);
        ctx.buffers.AddOrGetPersistent(kAccumLumSumSqBuf, sizeof(double), count);
        auto lumSum{ctx.buf<double>(kAccumLumSumBuf)};
        auto lumSumSq{ctx.buf<double>(kAccumLumSumSqBuf)};
#endif

        for (std::size_t i = 0; i < count; ++i)
        {
#if DEBUG_ENABLED
            for (int c{0}; c < 3; ++c)
            {
                DBG_ASSERT(std::isfinite(fb[i][c]), "non-finite framebuffer value");
            }
#endif
#if METRICS_ENABLED
            const double luminance{static_cast<double>(GetLuminance(fb[i]))};
            lumSum[i] += luminance;
            lumSumSq[i] += luminance * luminance;
#endif
            accum[i] += fb[i];
            fb[i] = accum[i] * invN;
        }

#if METRICS_ENABLED
        if (sampleCount >= 2)
        {
            double meanEstimatorVariance{0.0};
            double maxEstimatorVariance{0.0};
            double meanLuminance{0.0};
            for (std::size_t i = 0; i < count; ++i)
            {
                const double n{static_cast<double>(sampleCount)};
                const double mean{lumSum[i] / n};
                const double centeredSumSquares{lumSumSq[i] - n * mean * mean};
                const double sampleVariance{std::max(0.0, centeredSumSquares / (n - 1.0))};
                const double estimatorVariance{sampleVariance / n};
                meanEstimatorVariance += estimatorVariance;
                maxEstimatorVariance = std::max(maxEstimatorVariance, estimatorVariance);
                meanLuminance += mean;
            }

            meanEstimatorVariance /= static_cast<double>(count);
            meanLuminance /= static_cast<double>(count);
            METRICS_LOG(
                "AccumulationPass: samples=%d mean estimator variance=%g max estimator variance=%g mean luminance=%g",
                sampleCount, meanEstimatorVariance, maxEstimatorVariance, meanLuminance);

            ctx.buffers.AddOrGetPersistent(kVarianceOutputName, sizeof(VarianceStats), 1);
            ctx.buf<VarianceStats>(kVarianceOutputName)[0] = VarianceStats{
                .mean = static_cast<float>(meanEstimatorVariance),
                .max  = static_cast<float>(maxEstimatorVariance),
            };
        }
#endif
    }

} // namespace Restir
