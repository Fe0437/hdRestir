#pragma once

// RenderPass-free, RHI-free declaration of the GPU accumulation kernel.
//
// LightRHI's module ABI requires -fno-rtti on any TU that `import`s it, while
// RenderContext/RenderPass transitively pull in USD headers that require RTTI
// (typeid). Those two requirements can't share a translation unit, so the
// RHI-touching implementation (gpu_accumulation_kernel.cpp, built with
// -fno-rtti as part of the isolated HdRestirGpu target) is kept behind this
// header, which has no rhi:: types and no virtual functions - safe to
// `#include` from both an RTTI-enabled TU (gpu_accumulation_pass.cpp) and the
// RTTI-disabled one that implements it.

#include "pxr/base/gf/vec4f.h"

#include <cstdint>
#include <gsl/gsl>
#include <memory>

#if GPU_ENABLED

namespace Restir::Gpu
{

    // GPU port of the AccumulationPass formula (source/passes/accumulation_pass.cpp):
    // running-average accumulation plus an optional 3x3 neighborhood-max firefly
    // clamp, run as a LightRHI compute dispatch (source/gpu_functions/shaders/accumulation.slang).
    class AccumulationKernel
    {
      public:
        AccumulationKernel();
        ~AccumulationKernel();

        AccumulationKernel(AccumulationKernel &&) noexcept;
        AccumulationKernel &operator=(AccumulationKernel &&) noexcept;

        // colorInOut: writable RGBA32F framebuffer, width*height pixels - read as this
        // frame's color, overwritten in place with the accumulated/normalized result.
        // accumInOut: width*height GfVec4f — the running (unnormalized) color sum so
        // far, owned by the caller (RenderContext's persistent buffer store), not by
        // this object. Every call uploads accumInOut's incoming content to the GPU,
        // accumulates in place, and writes the updated sum back into it — nothing
        // survives here independent of the caller's buffer, so if the caller's
        // persistent store was cleared (a fresh, zeroed accumInOut), accumulation
        // restarts automatically with no GPU-specific reset path.
#if METRICS_ENABLED
        // lumSumInOut/lumSumSqInOut: width*height floats each, same in/out contract
        // as accumInOut — the per-pixel running sum of luminance / luminance^2
        // across frames so far (float precision, unlike the CPU AccumulationPass's
        // double accumulators — Apple GPUs have no double type). Caller reduces
        // these into variance stats. These parameters (and the GPU-side buffers/
        // shader branch backing them) only exist in METRICS_ENABLED builds — the
        // metrics-off build never allocates them, uploads them, or dispatches a
        // shader variant that references them.
        void RunFrame(gsl::span<pxr::GfVec4f> colorInOut, gsl::span<pxr::GfVec4f> accumInOut, std::uint32_t width,
                      std::uint32_t height, std::uint32_t frameIndex, bool fireflyEnable, gsl::span<float> lumSumInOut,
                      gsl::span<float> lumSumSqInOut);
#else
        void RunFrame(gsl::span<pxr::GfVec4f> colorInOut, gsl::span<pxr::GfVec4f> accumInOut, std::uint32_t width,
                      std::uint32_t height, std::uint32_t frameIndex, bool fireflyEnable);
#endif

      private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace Restir::Gpu

#endif // GPU_ENABLED
