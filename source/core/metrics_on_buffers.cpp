#include "metrics_on_buffers.h"

#if METRICS_ENABLED

#include <atomic>
#include <cstdio>
#include <string>

namespace Restir::Metrics
{

    namespace
    {

        std::size_t ReserveMetricSlot(FrameBuffersMap &buffers, std::string_view name, std::size_t parent)
        {
            const std::size_t idx{
                buffers.Has(kMetricTimingOutputName) ? buffers.GetFrameBuffer(kMetricTimingOutputName).Count() : 0};

            buffers.Add(kMetricTimingOutputName, sizeof(float), idx + 1);
            buffers.Get<float>(kMetricTimingOutputName)[idx] = 0.0f;

            buffers.Add(kMetricNameOutputName, sizeof(MetricNameEntry), idx + 1);
            std::snprintf(buffers.Get<MetricNameEntry>(kMetricNameOutputName)[idx].Name, sizeof(MetricNameEntry::Name),
                          "%s", std::string(name).c_str());

            buffers.Add(kMetricParentOutputName, sizeof(std::size_t), idx + 1);
            buffers.Get<std::size_t>(kMetricParentOutputName)[idx] = parent;

            return idx;
        }

    } // namespace

    std::size_t NextMetricIndex(FrameBuffersMap &buffers)
    {
        return buffers.Has(kMetricTimingOutputName) ? buffers.GetFrameBuffer(kMetricTimingOutputName).Count() : 0;
    }

    std::size_t RecordMetric(FrameBuffersMap &buffers, std::string_view name, float value, std::size_t parent)
    {
        const std::size_t idx{ReserveMetricSlot(buffers, name, parent)};
        buffers.Get<float>(kMetricTimingOutputName)[idx] = value;
        return idx;
    }

    ScopedMetricTimer::ScopedMetricTimer(FrameBuffersMap &buffers, std::string_view name, std::size_t parent)
        : _buffers{buffers}, _index{ReserveMetricSlot(buffers, name, parent)}, _t0{std::chrono::steady_clock::now()}
    {
    }

    ScopedMetricTimer::~ScopedMetricTimer()
    {
        const float ms{std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - _t0).count()};
        _buffers.Get<float>(kMetricTimingOutputName)[_index] = ms;
    }

    void AccumulateBufferMetric(IBufferProvider &provider, std::string_view name, std::size_t index, float ms)
    {
        auto                   values{provider.GetChecked(name).As<float>()};
        std::atomic_ref<float> counter{values[index]};
        counter.fetch_add(ms, std::memory_order_relaxed);
    }

    void DeclareSubMetrics(IBufferProvider &provider, const std::vector<std::string_view> &names, SubMetricGroup group)
    {
        auto *nameSlots{
            static_cast<MetricNameEntry *>(provider.Add(group.NamesBuf, sizeof(MetricNameEntry), names.size()))};
        (void)provider.Add(group.ValuesBuf, sizeof(float), names.size());
        for (std::size_t i{0}; i < names.size(); ++i)
        {
            std::snprintf(nameSlots[i].Name, sizeof(MetricNameEntry::Name), "%s", std::string(names[i]).c_str());
        }
    }

    std::vector<std::size_t> RecordSubMetrics(IBufferProvider &provider, FrameBuffersMap &buffers, std::size_t parent,
                                              float wallMs, SubMetricGroup group)
    {
        if (!provider.Has(group.NamesBuf) || !provider.Has(group.ValuesBuf))
        {
            return {};
        }
        auto names{provider.GetChecked(group.NamesBuf).As<MetricNameEntry>()};
        auto values{provider.GetChecked(group.ValuesBuf).As<float>()};

        std::vector<std::pair<std::string_view, float>> metrics;
        metrics.reserve(names.size());
        for (std::size_t i{0}; i < names.size(); ++i)
        {
            metrics.emplace_back(std::string_view{names[i].Name}, values[i]);
        }
        return RecordScaledMetrics(buffers, parent, wallMs, metrics);
    }

    std::vector<std::size_t> RecordScaledMetrics(FrameBuffersMap &buffers, std::size_t parent, float wallMs,
                                                 const std::vector<std::pair<std::string_view, float>> &metrics)
    {
        float cpuSum{0.0f};
        for (const auto &[name, ms] : metrics)
        {
            cpuSum += ms;
        }
        const float scale{cpuSum > 0.0f ? wallMs / cpuSum : 0.0f};

        std::vector<std::size_t> indices;
        indices.reserve(metrics.size());
        for (const auto &[name, ms] : metrics)
        {
            indices.push_back(RecordMetric(buffers, name, ms * scale, parent));
        }
        return indices;
    }

} // namespace Restir::Metrics

#endif // METRICS_ENABLED
