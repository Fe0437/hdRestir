#include "renderer_pipeline_state.h"

#include "restir_render_settings.h"

namespace Restir {

namespace {

enum class PipelineKind {
    PathTracer,
    PathTracerPostProcess,
};

template<typename T>
[[nodiscard]] T Get(
    const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor>& values,
    const TfToken& key,
    T fallback)
{
    const auto it{values.find(key)};
    if (it == values.end() || !it->second.IsHolding<T>()) {
        return fallback;
    }
    return it->second.Get<T>();
}

[[nodiscard]] PathTracerPipelineSettings MakePathTracerSettings(
    const RendererPipelineSettings& s)
{
    const auto& v{s.Values};
    return PathTracerPipelineSettings{
        .MaxDepth         = 32,
        .ResolutionLevel  = Get<int>(v,  HdRestirRenderSettingsTokens->resolutionLevel,    0),
        .OutputNames      = s.OutputNames,
        .PathTrace        = PathTracePassSettings{
            .EnableSubsurface      = Get<bool>(v, HdRestirRenderSettingsTokens->enableSubsurface,      false),
            .MaxReflectionBounces  = Get<int> (v, HdRestirRenderSettingsTokens->maxReflectionBounces,  0),
            .MaxRefractionBounces  = Get<int> (v, HdRestirRenderSettingsTokens->maxRefractionBounces,  0),
            .RenderIblBackground   = Get<bool>(v, HdRestirRenderSettingsTokens->renderIblBackground,   false),
        },
        .Denoiser         = Denoiser::Config{
            .EnableDenoiser        = Get<bool>(v, HdRestirRenderSettingsTokens->enableDenoiser,        false),
            .EnableFireflyFilter   = Get<bool>(v, HdRestirRenderSettingsTokens->enableFireflyFilter,   false),
            .EnableChromaticityBlur= Get<bool>(v, HdRestirRenderSettingsTokens->enableChromaticityBlur,false),
        },
        .PostProcess      = PostProcess::Config{
            .EnableLensFlare       = Get<bool> (v, HdRestirRenderSettingsTokens->enableLensFlare,      false),
            .ChromaticAberration   = Get<float>(v, HdRestirRenderSettingsTokens->chromaticAberration,  0.0f),
        },
    };
}
}

[[nodiscard]] std::unique_ptr<RenderPipeline> MakeBuiltinPipeline(
    PipelineKind      kind,
    std::string&&     name,
    const RendererPipelineSettings& settings)
{
    switch (kind) {
    case PipelineKind::PathTracer:
        return makePathTracerPipeline(std::move(name), MakePathTracerSettings(settings));
    case PipelineKind::PathTracerPostProcess:
        return makePathTracerPostProcessPipeline(std::move(name), MakePathTracerSettings(settings));
    }
    return makePathTracerPipeline(std::move(name), MakePathTracerSettings(settings));
}

}  // namespace

RendererPipelineState::RendererPipelineState(const RendererPipelineSettings& settings)
{
    const PipelineKind primary{ParsePipelineKind(settings.PrimaryPipeline)};

    if (settings.EnableSplitScreen) {
        const PipelineKind right{ParsePipelineKind(settings.SplitScreenRightPipeline)};
        const int resLevel{Get<int>(settings.Values, HdRestirRenderSettingsTokens->resolutionLevel, 0)};
        _splitScreen = std::make_unique<SplitScreenCompositor>(
            MakeBuiltinPipeline(primary, "LeftPipeline",  settings),
            MakeBuiltinPipeline(right,   "RightPipeline", settings),
            resLevel, resLevel);
        return;
    }

    _singlePipeline = MakeBuiltinPipeline(primary, "PrimaryPipeline", settings);
}

void RendererPipelineState::Execute(RenderContext& ctx)
{
    if (_splitScreen != nullptr) {
        _splitScreen->execute(ctx);
        return;
    }

    Expects(_singlePipeline != nullptr);
    _singlePipeline->execute(ctx);
}

RendererPipelineSettings MakeRendererPipelineSettings(
    const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor>& renderSettings,
    gsl::span<const std::string> requestedOutputNames)
{
    return RendererPipelineSettings{
        .Values                  = renderSettings,
        .PrimaryPipeline         = Get<TfToken>(renderSettings,
                                       HdRestirRenderSettingsTokens->primaryPipeline,
                                       GetPathTracerPipelineToken()),
        .SplitScreenRightPipeline = Get<TfToken>(renderSettings,
                                       HdRestirRenderSettingsTokens->splitScreenRightPipeline,
                                       GetPathTracerPostProcessPipelineToken()),
        .EnableSplitScreen       = Get<bool>(renderSettings,
                                       HdRestirRenderSettingsTokens->enableSplitScreen,
                                       false),
        .OutputNames             = {requestedOutputNames.begin(), requestedOutputNames.end()},
    };
}

TfToken GetPathTracerPipelineToken()        { return TfToken{"PathTracer"}; }
TfToken GetPathTracerPostProcessPipelineToken() { return TfToken{"PathTracerPostProcess"}; }

}  // namespace Restir
