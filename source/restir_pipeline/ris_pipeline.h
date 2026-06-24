#pragma once

#include "accumulation_pass.h"
#include "path_tracer_pipeline.h"
#include "raycast_pass.h"
#include "render_pipeline.h"
#include "ris_path_trace_pass.h"
#include "upscale_pass.h"

#include <memory>

namespace Restir
{

    struct RISPipelineSettings
    {
        PathTracerPipelineSettings PathTracer{};
        int                        CandidateCount{16};
        bool                       UseReservoir{true};
    };

    [[nodiscard]] inline std::unique_ptr<RenderPipeline> MakeRISPipeline(std::string              &&name     = "RIS",
                                                                         const RISPipelineSettings &settings = {})
    {
        std::vector<Detail::PipelinePassSpec> passSpecs{};
        passSpecs.push_back(Detail::MakePassSpec<RaycastPass>(settings.PathTracer.OutputNames));
        passSpecs.push_back(Detail::MakePassSpec<RISPathTracePass>(settings.CandidateCount, settings.UseReservoir,
                                                                   settings.PathTracer.PathTrace,
                                                                   settings.PathTracer.MaxDepth));
        passSpecs.push_back(Detail::MakePassSpec<AccumulationPass>(settings.PathTracer.Denoiser.EnableFireflyFilter));
        if (settings.PathTracer.ResolutionLevel > 0)
        {
            passSpecs.push_back(Detail::MakePassSpec<UpscalePass>(settings.PathTracer.OutputNames));
        }
#if DEBUG_ENABLED
        if (settings.PathTracer.DebugOverlay.Enable)
        {
            auto cfg{settings.PathTracer.DebugOverlay};
            cfg.Entries.push_back(DebugOverlayTextEntry{.Text = "RIS"});
            passSpecs.push_back(Detail::MakePassSpec<DebugOverlayPass>(cfg));
        }
#endif
        return Detail::CompilePipeline(std::move(name), settings.PathTracer.OutputNames, std::move(passSpecs));
    }

} // namespace Restir