#pragma once

#include "accumulation_pass.h"
#include "debug.h"
#include "denoiser_pass.h"
#include "path_trace_pass.h"
#include "post_process_pass.h"
#include "raycast_pass.h"
#include "render_pipeline.h"
#include "upscale_pass.h"

#if DEBUG_ENABLED
#include "debug_overlay_pass.h"
#endif

#include "output_names.h"

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Restir {

struct PathTracerPipelineSettings {
    int                   MaxDepth{32};
    int                   ResolutionLevel{0};
    std::vector<std::string> OutputNames{std::string{kColorOutputName}};
    PathTracePassSettings PathTrace{};
    Denoiser::Config      Denoiser{};
    PostProcess::Config   PostProcess{};
#if DEBUG_ENABLED
    DebugOverlayPass::Config DebugOverlay{};
#endif
};

namespace Detail {

struct PipelinePassSpec {
    std::vector<std::string> StaticInputs{};
    std::vector<std::string> StaticOutputs{};
    std::vector<std::string> Inputs{};
    std::vector<std::string> Outputs{};
    std::function<std::unique_ptr<RenderPass>()> Create{};
};

[[nodiscard]] inline bool ContainsName(const std::unordered_set<std::string>& names, const std::string& name)
{
    return names.contains(name);
}

[[nodiscard]] inline bool IsSubset(gsl::span<const std::string> actual,
                                   gsl::span<const std::string> possible)
{
    return std::all_of(actual.begin(), actual.end(), [&](const auto& actualName) {
        return std::find(possible.begin(), possible.end(), actualName) != possible.end();
    });
}

template<typename PassT, typename... Args>
    requires HasStaticPassIo<PassT>
[[nodiscard]] inline PipelinePassSpec MakePassSpec(Args&&... args)
{
    auto ctorArgs{std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}};
    auto pass{std::apply(
        [](const auto&... unpackedArgs) {
            return std::make_unique<PassT>(unpackedArgs...);
        },
        ctorArgs)};
    auto staticInputs{PassT::StaticInputs()};
    auto staticOutputs{PassT::StaticOutputs()};
    auto inputs{pass->Inputs()};
    auto outputs{pass->Outputs()};

    Expects(IsSubset(inputs, staticInputs));
    Expects(IsSubset(outputs, staticOutputs));

    return PipelinePassSpec{
        .StaticInputs = std::move(staticInputs),
        .StaticOutputs = std::move(staticOutputs),
        .Inputs = std::move(inputs),
        .Outputs = std::move(outputs),
        .Create = [ctorArgs = std::move(ctorArgs)]() -> std::unique_ptr<RenderPass> {
            return std::apply(
                [](const auto&... unpackedArgs) -> std::unique_ptr<RenderPass> {
                    return std::make_unique<PassT>(unpackedArgs...);
                },
                ctorArgs);
        },
    };
}

[[nodiscard]] inline std::unique_ptr<RenderPipeline> CompilePipeline(
    std::string&& name,
    const std::vector<std::string>& requestedOutputs,
    std::vector<PipelinePassSpec> passSpecs)
{
    Expects(!requestedOutputs.empty());

    std::unordered_set<std::string> needed{};
    for (const auto outputName : requestedOutputs) {
        needed.insert(std::string{outputName});
    }
    std::vector<std::size_t> includedPassIndices{};
    includedPassIndices.reserve(passSpecs.size());

    for (std::size_t idx{passSpecs.size()}; idx > 0; --idx) {
        const auto& passSpec{passSpecs[idx - 1]};
        const bool producesNeededOutput{
            std::any_of(passSpec.Outputs.begin(),
                        passSpec.Outputs.end(),
                        [&](const std::string& outputName) {
                            return ContainsName(needed, outputName);
                        })
        };
        if (!producesNeededOutput) {
            continue;
        }

        includedPassIndices.push_back(idx - 1);
        for (const auto& inputName : passSpec.Inputs) {
            needed.insert(inputName);
        }
    }

    auto pipeline{std::make_unique<RenderPipeline>(std::move(name))};
    if (includedPassIndices.empty()) {
        std::string requestedOutputList{};
        for (std::size_t idx{0}; idx < requestedOutputs.size(); ++idx) {
            if (idx > 0) {
                requestedOutputList += ", ";
            }
            requestedOutputList += requestedOutputs[idx];
        }
        DBG_LOG("CompilePipeline: no pass chain can satisfy requested outputs [%s]", requestedOutputList.c_str());
        return pipeline;
    }

    std::reverse(includedPassIndices.begin(), includedPassIndices.end());

    for (const auto passIndex : includedPassIndices) {
        pipeline->Add(passSpecs[passIndex].Create());
    }
    return pipeline;
}


}  // namespace Detail

[[nodiscard]] inline std::unique_ptr<RenderPipeline> makeCorePathTracerPipeline(
    std::string&&                      name,
    const PathTracerPipelineSettings& settings)
{
    std::vector<Detail::PipelinePassSpec> passSpecs{};
    passSpecs.push_back(Detail::MakePassSpec<RaycastPass>(settings.OutputNames));
    passSpecs.push_back(Detail::MakePassSpec<PathTracePass>(settings.PathTrace, settings.MaxDepth));
    passSpecs.push_back(Detail::MakePassSpec<AccumulationPass>(settings.Denoiser.EnableFireflyFilter));
#if DEBUG_ENABLED
    if (settings.DebugOverlay.Enable) {
        auto cfg{settings.DebugOverlay};
        cfg.Entries.push_back(DebugOverlayTextEntry{.Text = "PathTracer"});
        passSpecs.push_back(Detail::MakePassSpec<DebugOverlayPass>(cfg));
    }
#endif
    return Detail::CompilePipeline(std::move(name), settings.OutputNames, std::move(passSpecs));
}

[[nodiscard]] inline std::unique_ptr<RenderPipeline> makePathTracerPipeline(
    std::string&&                      name = "PathTracer",
    const PathTracerPipelineSettings& settings = {})
{
    std::vector<Detail::PipelinePassSpec> passSpecs{};
    passSpecs.push_back(Detail::MakePassSpec<RaycastPass>(settings.OutputNames));
    passSpecs.push_back(Detail::MakePassSpec<PathTracePass>(settings.PathTrace, settings.MaxDepth));
    passSpecs.push_back(Detail::MakePassSpec<AccumulationPass>(settings.Denoiser.EnableFireflyFilter));
    if (settings.ResolutionLevel > 0) {
        passSpecs.push_back(Detail::MakePassSpec<UpscalePass>(settings.OutputNames));
    }
#if DEBUG_ENABLED
    if (settings.DebugOverlay.Enable) {
        auto cfg{settings.DebugOverlay};
        cfg.Entries.push_back(DebugOverlayTextEntry{.Text = "PathTracer"});
        passSpecs.push_back(Detail::MakePassSpec<DebugOverlayPass>(cfg));
    }
#endif
    return Detail::CompilePipeline(std::move(name), settings.OutputNames, std::move(passSpecs));
}

[[nodiscard]] inline std::unique_ptr<RenderPipeline> makePathTracerPostProcessPipeline(
    std::string&&                      name = "PathTracerPost",
    const PathTracerPipelineSettings& settings = {})
{
    std::vector<Detail::PipelinePassSpec> passSpecs{};
    passSpecs.push_back(Detail::MakePassSpec<RaycastPass>(settings.OutputNames));
    passSpecs.push_back(Detail::MakePassSpec<PathTracePass>(settings.PathTrace, settings.MaxDepth));
    passSpecs.push_back(Detail::MakePassSpec<AccumulationPass>(settings.Denoiser.EnableFireflyFilter));
    passSpecs.push_back(Detail::MakePassSpec<DenoiserPass>(settings.Denoiser));
    passSpecs.push_back(Detail::MakePassSpec<PostProcessPass>(settings.PostProcess));
    if (settings.ResolutionLevel > 0) {
        passSpecs.push_back(Detail::MakePassSpec<UpscalePass>(settings.OutputNames));
    }
#if DEBUG_ENABLED
    if (settings.DebugOverlay.Enable) {
        auto cfg{settings.DebugOverlay};
        cfg.Entries.push_back(DebugOverlayTextEntry{.Text = "PathTracerPost"});
        passSpecs.push_back(Detail::MakePassSpec<DebugOverlayPass>(cfg));
    }
#endif
    return Detail::CompilePipeline(std::move(name), settings.OutputNames, std::move(passSpecs));
}

}  // namespace Restir
