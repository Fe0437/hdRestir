#pragma once

#include "gpu_accumulation_kernel.h"
#include "output_names.h"
#include "render_pass.h"

#include <string>
#include <vector>

#if GPU_ENABLED

namespace Restir
{

    // GPU port of AccumulationPass (source/passes/accumulation_pass.cpp) via
    // LightRHI compute (Gpu::AccumulationKernel, source/gpu_functions/shaders/accumulation.slang).
    // Same Inputs()/Outputs() contract as AccumulationPass, so the two are
    // interchangeable PassSpecs in the pipeline compiler.
    //
    // Accumulation state (kAccumColorBuf and, under METRICS_ENABLED, the
    // GPU-precision luminance-sum buffers) lives entirely in RenderContext's
    // persistent buffer store, exactly like the CPU AccumulationPass — this
    // pass and Gpu::AccumulationKernel keep no cross-frame accumulation state
    // of their own, so RenderPipeline::ClearPersistentBuffers() (camera move,
    // scene edit) restarts accumulation correctly with no GPU-specific hook.
    //
    // When METRICS_ENABLED, computes the same variance-stats log line as the
    // CPU AccumulationPass (float precision instead of double — see
    // Gpu::AccumulationKernel::RunFrame).
    class GpuAccumulationPass final : public RenderPass
    {
      public:
        explicit GpuAccumulationPass(bool enableFireflyFilter = true)
            : RenderPass{"GpuAccumulationPass"}, _enableFireflyFilter{enableFireflyFilter}
        {
        }

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {std::string{kColorOutputName}};
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {
                std::string{kColorOutputName},
#if METRICS_ENABLED
                std::string{kVarianceOutputName},
#endif
            };
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return {std::string{kColorOutputName}};
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return {
                std::string{kColorOutputName},
#if METRICS_ENABLED
                std::string{kVarianceOutputName},
#endif
            };
        }

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        bool                    _enableFireflyFilter{true};
        Gpu::AccumulationKernel _kernel;
    };

} // namespace Restir

#endif // GPU_ENABLED
