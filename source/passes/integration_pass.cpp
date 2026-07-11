#include "integration_pass.h"

#include "buffer_provider.h"
#include "buffer_user.h"
#include "camera_ray.h"
#include "debug.h"
#include "metrics_on_buffers.h"
#include "pxr/base/gf/vec4f.h"
#include "render_context.h"
#include "spectrum.h"

#include <gsl/gsl>
#include <pxr/base/work/loops.h>

#if METRICS_ENABLED
#include <atomic>
#include <chrono>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        // Wraps FrameBuffersMap for use as IBufferProvider.
        struct FrameBuffersProvider final : IBufferProvider
        {
            explicit FrameBuffersProvider(FrameBuffersMap &b, std::size_t n) : buffers{b}, count{n} {}

            [[nodiscard]] bool Has(std::string_view name) const override
            {
                return buffers.Has(name);
            }

            [[nodiscard]] FrameBuffer &GetChecked(std::string_view name) override
            {
                DBG_ASSERT(buffers.Has(name), "Buffer must be prepared before parallel Li(): " + std::string{name});
                return buffers.GetFrameBuffer(name);
            }

            [[nodiscard]] void *GetOrCreatePersistent(std::string_view name, std::size_t stride) override
            {
                if (!buffers.Has(name))
                {
                    buffers.AddOrGetPersistent(name, stride, count);
                }
                return buffers.GetFrameBuffer(name).Data();
            }

            [[nodiscard]] void *Add(std::string_view name, std::size_t stride, std::size_t elementCount) override
            {
                buffers.Add(name, stride, elementCount == 0 ? count : elementCount);
                return buffers.GetFrameBuffer(name).Data();
            }

            FrameBuffersMap &buffers;
            std::size_t      count;
        };

#if METRICS_ENABLED
        // Encapsulates this pass's whole profiling story so _execute() below
        // needs no #if of its own: reserves this pass's own metric index
        // (RenderPass::Execute already reserved its slot via its
        // ScopedMetricTimer before calling _execute(), and nothing else can
        // have recorded an entry between that reservation and here, so
        // NextMetricIndex() - 1 is exactly it), accumulates RayGen/Li/
        // Writeback across parallel workers, and records everything —
        // rescaled to actual wall-clock time, plus whatever sub-metrics the
        // integrator itself declared (and one more nested level if THOSE
        // include a "DirectLight" entry that further subdivided itself, see
        // Metrics::kSubMetricsDetail) — once the parallel loop finishes.
        class IntegrationMetrics
        {
          public:
            explicit IntegrationMetrics(FrameBuffersMap &buffers)
                : _passIndex{Metrics::NextMetricIndex(buffers) - 1}, _wallStart{std::chrono::steady_clock::now()}
            {
            }

            [[nodiscard]] std::size_t PassIndex() const noexcept
            {
                return _passIndex;
            }

            // Called once per WorkParallelForN worker, after its tile loop —
            // merges that worker's local totals (see RESTIR_LOCAL_TIMER)
            // into the shared atomics.
            void MergeWorkerTotals(float rayGenMs, float liMs, float writebackMs) noexcept
            {
                _rayGenMs.fetch_add(rayGenMs, std::memory_order_relaxed);
                _liMs.fetch_add(liMs, std::memory_order_relaxed);
                _writebackMs.fetch_add(writebackMs, std::memory_order_relaxed);
            }

            void Finish(IBufferProvider &provider, FrameBuffersMap &buffers)
            {
                const float wallMs{
                    std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - _wallStart).count()};
                const auto phaseIndices{Metrics::RecordScaledMetrics(
                    buffers, _passIndex, wallMs,
                    {{"RayGen", _rayGenMs.load()}, {"Li", _liMs.load()}, {"Writeback", _writebackMs.load()}})};

                const std::size_t liIndex{phaseIndices[1]};
                const float       liWallMs{buffers.Get<float>(Metrics::kMetricTimingOutputName)[liIndex]};
                const auto        subIndices{Metrics::RecordSubMetrics(provider, buffers, liIndex, liWallMs)};

                // If whichever sub-metric is named "DirectLight" further
                // declared its own breakdown (e.g. RisDirectLightIntegrator's
                // NEECandidates/BSDFCandidates/Resampling), nest it there —
                // stays integrator-agnostic beyond this one label match.
                const auto subNames{buffers.Get<Metrics::MetricNameEntry>(Metrics::kMetricNameOutputName)};
                for (const std::size_t idx : subIndices)
                {
                    if (std::string_view{subNames[idx].Name} == "DirectLight")
                    {
                        const float subWallMs{buffers.Get<float>(Metrics::kMetricTimingOutputName)[idx]};
                        Metrics::RecordSubMetrics(provider, buffers, idx, subWallMs, Metrics::kSubMetricsDetail);
                        break;
                    }
                }
            }

          private:
            std::size_t                           _passIndex;
            std::atomic<float>                    _rayGenMs{0.0f};
            std::atomic<float>                    _liMs{0.0f};
            std::atomic<float>                    _writebackMs{0.0f};
            std::chrono::steady_clock::time_point _wallStart;
        };
#endif

    } // namespace

// Neither IntegrationMetrics nor any call into it should exist at all when
// METRICS_ENABLED is off — not even as a no-op stub — so construction/merge/
// finish are macros (like RESTIR_LOCAL_TIMER/RESTIR_METRIC_SCOPE) rather
// than a type with an empty fallback body, and _execute() below never
// spells out IntegrationMetrics itself.
#if METRICS_ENABLED
#define RESTIR_INTEGRATION_METRICS(buffers)                                                                            \
    IntegrationMetrics metrics                                                                                         \
    {                                                                                                                  \
        buffers                                                                                                        \
    }
#define RESTIR_INTEGRATION_METRICS_MERGE(rayGenMs, liMs, writebackMs)                                                  \
    metrics.MergeWorkerTotals(rayGenMs, liMs, writebackMs)
#define RESTIR_INTEGRATION_METRICS_FINISH(provider, buffers) metrics.Finish(provider, buffers)
#else
#define RESTIR_INTEGRATION_METRICS(buffers)
#define RESTIR_INTEGRATION_METRICS_MERGE(rayGenMs, liMs, writebackMs)
#define RESTIR_INTEGRATION_METRICS_FINISH(provider, buffers)
#endif

    void IntegrationPass::_execute(RenderContext &ctx)
    {
        DBG_ASSERT(ctx.buffers.Has(kGBufferOutputName), "GBuffer must be present (produced by RaycastPass)");

        const std::size_t    count{ctx.frame.PixelCount()};
        FrameBuffersProvider provider{ctx.buffers, count};
        RESTIR_INTEGRATION_METRICS(ctx.buffers);

        IBufferStager *stager{_integrator->GetBufferStager()};
        {
            RESTIR_METRIC_SCOPE_CHILD(ctx.buffers, "BufferStaging", metrics.PassIndex());
            if (stager)
            {
                stager->PrepareBuffers(provider, *ctx.scene);
            }
        }

        ctx.buffers.Add(kColorOutputName, sizeof(GfVec4f), count);
        auto fb{ctx.buf<GfVec4f>(kColorOutputName)};
        auto gbuf{ctx.buf<std::optional<HitRecord>>(kGBufferOutputName)};

        const GfMatrix4d invView{ctx.viewMatrix.GetInverse()};
        const GfMatrix4d invProj{ctx.projMatrix.GetInverse()};
        const int        width{ctx.frame.RenderedWidth()};
        const int        height{ctx.frame.RenderedHeight()};
        const int        frameIndex{ctx.frameIndex};

        constexpr int kTileSize{16};
        const int     numTilesX{(width + kTileSize - 1) / kTileSize};
        const int     numTilesY{(height + kTileSize - 1) / kTileSize};
        const int     numTiles{numTilesX * numTilesY};

        WorkParallelForN(
            static_cast<std::size_t>(numTiles),
            [&](std::size_t begin, std::size_t end)
            {
                float localRayGenMs{0.0f};
                float localLiMs{0.0f};
                float localWritebackMs{0.0f};
                for (std::size_t t{begin}; t < end; ++t)
                {
                    const int tileX{static_cast<int>(t) % numTilesX};
                    const int tileY{static_cast<int>(t) / numTilesX};
                    const int startX{tileX * kTileSize};
                    const int startY{tileY * kTileSize};
                    const int endX{std::min(startX + kTileSize, width)};
                    const int endY{std::min(startY + kTileSize, height)};
                    for (int y{startY}; y < endY; ++y)
                    {
                        for (int x{startX}; x < endX; ++x)
                        {
                            const std::size_t i{static_cast<std::size_t>(y) * width + x};

                            Rng         pixelRng{static_cast<std::uint32_t>(i) ^
                                                 static_cast<std::uint32_t>(frameIndex * 12345ULL)};
                            const float px{static_cast<float>(x) + pixelRng.NextFloat()};
                            const float py{static_cast<float>(y) + pixelRng.NextFloat()};

                            const Ray ray{[&]
                                          {
                                              RESTIR_LOCAL_TIMER(localRayGenMs);
                                              if (ctx.cameraParams)
                                              {
                                                  return GenerateCameraRay(invView, invProj, px, py, width, height,
                                                                           *ctx.cameraParams, pixelRng);
                                              }
                                              return GenerateCameraRay(invView, invProj, px, py, width, height);
                                          }()};

                            const SampledWavelengths lambda{SampledWavelengths::SampleUniform(pixelRng.NextFloat())};

                            const SampledSpectrum spectrum{[&]
                                                           {
                                                               RESTIR_LOCAL_TIMER(localLiMs);
                                                               return _integrator->Li({ray, gbuf[i]}, *ctx.scene,
                                                                                      pixelRng, lambda, provider,
                                                                                      {i, count});
                                                           }()};

                            {
                                RESTIR_LOCAL_TIMER(localWritebackMs);
                                GfVec3f L{SpectrumToRGB(spectrum, lambda)};
                                if (ctx.cameraParams && ctx.cameraParams->enableExposure)
                                {
                                    L *= GetExposureMultiplier(*ctx.cameraParams);
                                }
                                fb[i] = GfVec4f{L[0], L[1], L[2], 1.0f};
                            }
                        }
                    }
                }
                RESTIR_INTEGRATION_METRICS_MERGE(localRayGenMs, localLiMs, localWritebackMs);
            });

        RESTIR_INTEGRATION_METRICS_FINISH(provider, ctx.buffers);
    }

} // namespace Restir
