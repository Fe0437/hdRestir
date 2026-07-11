#pragma once

#include "metrics_on_buffers.h"

#include <string_view>

namespace Restir
{

    inline constexpr std::string_view kGBufferOutputName{"GBuffer"};
#if METRICS_ENABLED

    inline constexpr std::string_view kVarianceOutputName{"Variance"};

    struct VarianceStats
    {
        float mean{0.0f};
        float max{0.0f};
    };
#endif
    inline constexpr std::string_view kColorOutputName{"Color"};

    // Persistent accumulation buffers (internal to AccumulationPass, not pipeline AOV outputs).
    inline constexpr std::string_view kAccumColorBuf{"__accum_color"};
#if METRICS_ENABLED
    inline constexpr std::string_view kAccumLumSumBuf{"__accum_lum_sum"};
    inline constexpr std::string_view kAccumLumSumSqBuf{"__accum_lum_sum_sq"};
#if GPU_ENABLED
    // GpuAccumulationPass accumulates luminance sums in float precision
    // (Apple GPUs have no double type) — distinct persistent buffers from
    // the CPU AccumulationPass's double-precision accumulators, so switching
    // AccumulationPass <-> GpuAccumulationPass mid-session can't reinterpret
    // one precision's bytes as the other's.
    inline constexpr std::string_view kGpuAccumLumSumBuf{"__gpu_accum_lum_sum"};
    inline constexpr std::string_view kGpuAccumLumSumSqBuf{"__gpu_accum_lum_sum_sq"};
#endif
#endif
    inline constexpr std::string_view kDepthOutputName{"Depth"};
    inline constexpr std::string_view kAlbedoOutputName{"Albedo"};
    inline constexpr std::string_view kNormalOutputName{"Normal"};

    enum class OutputDataType
    {
        Color4,
        Vec3,
        Float1,
        Unknown,
    };

    [[nodiscard]] inline bool IsColorOutputName(std::string_view name) noexcept
    {
        return name == kColorOutputName;
    }

    [[nodiscard]] inline OutputDataType GetOutputDataType(std::string_view name) noexcept
    {
        if (name == kColorOutputName)
        {
            return OutputDataType::Color4;
        }
        if (name == kAlbedoOutputName || name == kNormalOutputName)
        {
            return OutputDataType::Vec3;
        }
        if (name == kDepthOutputName)
        {
            return OutputDataType::Float1;
        }
        return OutputDataType::Unknown;
    }

} // namespace Restir