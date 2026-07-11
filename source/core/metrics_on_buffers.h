#pragma once

// Generic, arbitrarily-deep buffer-backed metrics instrumentation: any scope
// (a whole RenderPass, or a named phase/sub-phase within one) can record a
// named timing, optionally nested under a parent recorded earlier in the
// same frame — "Pass -> Phase -> SubPhase -> ..." to any depth — using the
// same FrameBuffersMap buffer system used for every other per-frame/
// persistent value in this codebase. Not specific to render passes or any
// other module.
//
// Only construct/destroy a ScopedMetricTimer, or call RecordMetric, from the
// thread that owns the FrameBuffersMap's call sequence — not thread-safe, so
// never do so from inside a parallel worker (e.g. WorkParallelForN's lambda
// body); accumulate locally per-worker and merge once after the parallel
// section instead (see IntegrationPass::_execute for the pattern).
//
// Usage — nesting via a live ScopedMetricTimer's own Index():
//   void SomePass::_execute(RenderContext &ctx)
//   {
//       ScopedMetricTimer phase{ctx.buffers, "Phase1"};
//       {
//           ScopedMetricTimer subPhase{ctx.buffers, "SubPhase", phase.Index()};
//           ...
//       }
//   }
//
// Usage — nesting under an entry whose ScopedMetricTimer object is out of
// scope by the time you need its index (e.g. RenderPass::Execute's own timer
// around the call to _execute()): capture NextMetricIndex() - 1 right after
// that call begins, since nothing else can have recorded an entry between
// the parent reserving its slot and its callee starting.
//   void SomePass::_execute(RenderContext &ctx)
//   {
//       const std::size_t myIndex{NextMetricIndex(ctx.buffers) - 1};
//       RESTIR_METRIC_SCOPE_CHILD(ctx.buffers, "Phase1", myIndex);
//       ...
//   }

#include "debug.h"

#if METRICS_ENABLED

#include "buffer_provider.h"
#include "frame_buffer_map.h"

#include <chrono>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace Restir::Metrics
{

    // Fixed-size name label for a metric entry — FrameBuffersMap stores
    // fixed-stride POD only, so this is how a variable-length name travels
    // alongside a same-index float/parent entry.
    struct MetricNameEntry
    {
        char Name[32]{};
    };

    // Sentinel parent value for a root (not nested under anything) entry.
    inline constexpr std::size_t kMetricNoParent{static_cast<std::size_t>(-1)};

    // The single set of buffers every metric (at every nesting depth) is
    // recorded into. Timing/Name/Parent are plain per-frame growing arrays,
    // reset every frame; Sum is the persistent cross-session cumulative
    // total (see AccumulateMetricSums).
    inline constexpr std::string_view kMetricTimingOutputName{"MetricTiming"};
    inline constexpr std::string_view kMetricSumTimingOutputName{"MetricSumTiming"};
    inline constexpr std::string_view kMetricNameOutputName{"MetricName"};
    inline constexpr std::string_view kMetricParentOutputName{"MetricParent"};

    // The index a metric would be recorded at if reserved right now — one
    // past the last entry recorded so far this frame.
    [[nodiscard]] std::size_t NextMetricIndex(FrameBuffersMap &buffers);

    // Records an already-computed value (e.g. summed across parallel workers
    // after the fact, rather than measured by a single live scope). Returns
    // the index it was recorded at, for nesting further children under it.
    std::size_t RecordMetric(FrameBuffersMap &buffers, std::string_view name, float value,
                             std::size_t parent = kMetricNoParent);

    class ScopedMetricTimer
    {
      public:
        ScopedMetricTimer(FrameBuffersMap &buffers, std::string_view name, std::size_t parent = kMetricNoParent);
        ~ScopedMetricTimer();

        ScopedMetricTimer(const ScopedMetricTimer &)            = delete;
        ScopedMetricTimer &operator=(const ScopedMetricTimer &) = delete;

        // This entry's own index — pass to a nested ScopedMetricTimer/
        // RecordMetric call's `parent` argument to nest it under this one.
        [[nodiscard]] std::size_t Index() const noexcept
        {
            return _index;
        }

      private:
        FrameBuffersMap                      &_buffers;
        std::size_t                           _index;
        std::chrono::steady_clock::time_point _t0;
    };

    // Folds a per-frame float buffer into its persistent cumulative sum.
    // `AccumulateMetricSums` is the canonical instantiation for the unified
    // metrics buffers above; the generic form stays available for any other
    // per-frame/sum buffer pair.
    inline void AccumulateMetricSum(FrameBuffersMap &buffers, std::string_view perFrameName, std::string_view sumName)
    {
        if (!buffers.Has(perFrameName))
        {
            return;
        }
        auto timings{buffers.Get<float>(perFrameName)};
        buffers.AddOrGetPersistent(sumName, sizeof(float), timings.size());
        auto sumData{buffers.Get<float>(sumName)};
        for (std::size_t i{0}; i < timings.size(); ++i)
        {
            sumData[i] += timings[i];
        }
    }

    inline void AccumulateMetricSums(FrameBuffersMap &buffers)
    {
        AccumulateMetricSum(buffers, kMetricTimingOutputName, kMetricSumTimingOutputName);
    }

    // RAII: adds elapsed time to a LOCAL (not buffer-backed) float on
    // destruction — for merging per-worker totals once after a parallel
    // section instead of writing to shared buffers from every worker (see
    // IntegrationPass::_execute for the pattern; use RESTIR_LOCAL_TIMER).
    class ScopedLocalTimer
    {
      public:
        explicit ScopedLocalTimer(float &accumulator) noexcept
            : _accumulator{accumulator}, _t0{std::chrono::steady_clock::now()}
        {
        }
        ~ScopedLocalTimer()
        {
            _accumulator += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - _t0).count();
        }

        ScopedLocalTimer(const ScopedLocalTimer &)            = delete;
        ScopedLocalTimer &operator=(const ScopedLocalTimer &) = delete;

      private:
        float                                &_accumulator;
        std::chrono::steady_clock::time_point _t0;
    };

    // Accumulates `ms` into slot `index` of a buffer via std::atomic_ref —
    // safe to call concurrently from many worker threads during a parallel
    // pixel loop, the same way IBufferProvider::GetChecked is documented
    // safe to call from parallel Li() execution. The buffer must already
    // exist (declared once, single-threaded — typically by an
    // IBufferStager::PrepareBuffers implementation, e.g. via
    // DeclareSubMetrics below) and have at least `index + 1` floats.
    void AccumulateBufferMetric(IBufferProvider &provider, std::string_view name, std::size_t index, float ms);

    // RAII wrapper around AccumulateBufferMetric — same thread-safety
    // guarantee (use RESTIR_BUFFER_METRIC_SCOPE from parallel Li() code).
    class ScopedBufferMetricTimer
    {
      public:
        ScopedBufferMetricTimer(IBufferProvider &provider, std::string_view name, std::size_t index) noexcept
            : _provider{provider}, _name{name}, _index{index}, _t0{std::chrono::steady_clock::now()}
        {
        }
        ~ScopedBufferMetricTimer()
        {
            AccumulateBufferMetric(
                _provider, _name, _index,
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - _t0).count());
        }

        ScopedBufferMetricTimer(const ScopedBufferMetricTimer &)            = delete;
        ScopedBufferMetricTimer &operator=(const ScopedBufferMetricTimer &) = delete;

      private:
        IBufferProvider                      &_provider;
        std::string_view                      _name;
        std::size_t                           _index;
        std::chrono::steady_clock::time_point _t0;
    };

    // Records `metrics` (name + CPU-time summed across parallel workers, as
    // ScopedLocalTimer/RESTIR_LOCAL_TIMER accumulate) as children of
    // `parent`, rescaled so they sum to `wallMs` — the actual wall-clock
    // duration of the section they were measured within, since a CPU-time
    // sum from N concurrent workers is roughly N times too large to compare
    // directly against a wall-clock duration. Returns each entry's recorded
    // index, in the same order as `metrics`.
    std::vector<std::size_t> RecordScaledMetrics(FrameBuffersMap &buffers, std::size_t parent, float wallMs,
                                                 const std::vector<std::pair<std::string_view, float>> &metrics);

    // A pair of well-known buffer names identifying one "named sub-metrics"
    // group. Any IBufferStager can declare a group (via DeclareSubMetrics
    // below) to break its own cost down into named sub-timings, without
    // IBufferStager itself needing a metrics-specific method: a caller that
    // already holds an IBufferProvider (e.g. IntegrationPass) can look a
    // group's two names up directly, the same way it looks up any other
    // named buffer. `IBufferProvider::Add`'s explicit `count` (rather than
    // the default one-per-pixel) is what makes a small fixed-size array like
    // this practical — see buffer_provider.h.
    //
    // Two components nested in the same call chain (e.g. PathIntegrator and
    // the IDirectLightIntegrator it delegates to) must use DISTINCT groups —
    // sharing one would silently clobber whichever declares second.
    // kSubMetrics is the top-level group (e.g. PathIntegrator's own
    // BSDFSample/DirectLight breakdown); kSubMetricsDetail is free for
    // whichever one of those wants a further breakdown of its own (e.g.
    // RisDirectLightIntegrator's NEECandidates/BSDFCandidates/Resampling,
    // nested under PathIntegrator's "DirectLight" entry).
    struct SubMetricGroup
    {
        std::string_view NamesBuf;
        std::string_view ValuesBuf;
    };

    inline constexpr SubMetricGroup kSubMetrics{"SubMetricNames", "SubMetricValues"};
    inline constexpr SubMetricGroup kSubMetricsDetail{"SubMetricNamesDetail", "SubMetricValuesDetail"};

    // Declares group.NamesBuf/group.ValuesBuf sized for `names.size()`
    // sub-metrics and fills in their labels — call once, single-threaded,
    // from PrepareBuffers. Slot `i` of group.ValuesBuf corresponds to
    // `names[i]`; accumulate into it via
    // AccumulateBufferMetric/RESTIR_BUFFER_METRIC_SCOPE with index i.
    void DeclareSubMetrics(IBufferProvider &provider, const std::vector<std::string_view> &names,
                           SubMetricGroup group = kSubMetrics);

    // Single-threaded, after the parallel section: if a stager declared
    // `group`'s sub-metrics via DeclareSubMetrics, reads them back and
    // records them as children of `parent` via RecordScaledMetrics,
    // returning their indices (same order as declared) for further nesting;
    // no-op (empty) otherwise.
    std::vector<std::size_t> RecordSubMetrics(IBufferProvider &provider, FrameBuffersMap &buffers, std::size_t parent,
                                              float wallMs, SubMetricGroup group = kSubMetrics);

} // namespace Restir::Metrics

#define RESTIR_METRIC_SCOPE_CONCAT_INNER(a, b) a##b
#define RESTIR_METRIC_SCOPE_CONCAT(a, b) RESTIR_METRIC_SCOPE_CONCAT_INNER(a, b)
#define RESTIR_METRIC_SCOPE(buffers, name)                                                                             \
    ::Restir::Metrics::ScopedMetricTimer RESTIR_METRIC_SCOPE_CONCAT(_restirMetricScope_, __LINE__)                     \
    {                                                                                                                  \
        buffers, name                                                                                                  \
    }
#define RESTIR_METRIC_SCOPE_CHILD(buffers, name, parent)                                                               \
    ::Restir::Metrics::ScopedMetricTimer RESTIR_METRIC_SCOPE_CONCAT(_restirMetricScope_, __LINE__)                     \
    {                                                                                                                  \
        buffers, name, parent                                                                                          \
    }
#define RESTIR_LOCAL_TIMER(accumulator)                                                                                \
    ::Restir::Metrics::ScopedLocalTimer RESTIR_METRIC_SCOPE_CONCAT(_restirLocalTimer_, __LINE__)                       \
    {                                                                                                                  \
        accumulator                                                                                                    \
    }
#define RESTIR_BUFFER_METRIC_SCOPE(provider, name, index)                                                              \
    ::Restir::Metrics::ScopedBufferMetricTimer RESTIR_METRIC_SCOPE_CONCAT(_restirBufferMetricScope_, __LINE__)         \
    {                                                                                                                  \
        provider, name, index                                                                                          \
    }

#else

#define RESTIR_METRIC_SCOPE(buffers, name)
#define RESTIR_METRIC_SCOPE_CHILD(buffers, name, parent)
#define RESTIR_LOCAL_TIMER(accumulator)
#define RESTIR_BUFFER_METRIC_SCOPE(provider, name, index)

#endif // METRICS_ENABLED
