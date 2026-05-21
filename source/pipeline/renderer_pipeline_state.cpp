#include "renderer_pipeline_state.h"

#include "restir_render_settings.h"

namespace Restir {

namespace {

enum class PipelineKind {
    PathTracer,
    PathTracerPostProcess,
};

template<typename T>
[[nodiscard]] T GetRenderSettingOrDefault(
    const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor>& renderSettings,
    const TfToken& key,
    T fallback)
{
    const auto it{renderSettings.find(key)};
    if (it == renderSettings.end() || !it->second.IsHolding<T>()) {
        return fallback;
    }
    return it->second.Get<T>();
}

[[nodiscard]] PipelineKind ParsePipelineKind(const TfToken& token)
{
    if (token == GetPathTracerPostProcessPipelineToken()) {
        return PipelineKind::PathTracerPostProcess;
    }
    return PipelineKind::PathTracer;
}

[[nodiscard]] std::unique_ptr<RenderPipeline> MakeBuiltinPipeline(
    PipelineKind pipelineKind,
    std::string&& name,
    const PathTracerPipelineSettings& settings)
{
    switch (pipelineKind) {
    case PipelineKind::PathTracer:
        return makePathTracerPipeline(std::move(name), settings);
    case PipelineKind::PathTracerPostProcess:
        return makePathTracerPostProcessPipeline(std::move(name), settings);
    }

    return makePathTracerPipeline(std::move(name), settings);
}

}  // namespace

RendererPipelineState::RendererPipelineState(const RendererPipelineSettings& settings)
{
    const PipelineKind primaryPipelineKind{ParsePipelineKind(settings.PrimaryPipeline)};
    const PipelineKind rightPipelineKind{ParsePipelineKind(settings.SplitScreenRightPipeline)};

    if (settings.EnableSplitScreen) {
        _splitScreen = std::make_unique<SplitScreenCompositor>(
            MakeBuiltinPipeline(primaryPipelineKind, "LeftPipeline", settings.PathTracer),
            MakeBuiltinPipeline(rightPipelineKind, "RightPipeline", settings.PathTracer),
            settings.PathTracer.ResolutionLevel,
            settings.PathTracer.ResolutionLevel);
        return;
    }

    _singlePipeline = MakeBuiltinPipeline(primaryPipelineKind, "PrimaryPipeline", settings.PathTracer);
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
    RendererPipelineSettings settings{};

    settings.EnableSplitScreen = GetRenderSettingOrDefault<bool>(
        renderSettings,
        HdRestirRenderSettingsTokens->enableSplitScreen,
        false);
    settings.PrimaryPipeline = GetRenderSettingOrDefault<TfToken>(
        renderSettings,
        HdRestirRenderSettingsTokens->primaryPipeline,
        GetPathTracerPipelineToken());
    settings.SplitScreenRightPipeline = GetRenderSettingOrDefault<TfToken>(
        renderSettings,
        HdRestirRenderSettingsTokens->splitScreenRightPipeline,
        GetPathTracerPostProcessPipelineToken());

    settings.PathTracer.ResolutionLevel = GetRenderSettingOrDefault<int>(
        renderSettings,
        HdRestirRenderSettingsTokens->resolutionLevel,
        0);
    settings.PathTracer.OutputNames = std::vector<std::string>{requestedOutputNames.begin(), requestedOutputNames.end()};
    settings.PathTracer.PathTrace = PathTracePassSettings{
        .EnableSubsurface = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->enableSubsurface,
            false),
        .MaxReflectionBounces = GetRenderSettingOrDefault<int>(
            renderSettings,
            HdRestirRenderSettingsTokens->maxReflectionBounces,
            0),
        .MaxRefractionBounces = GetRenderSettingOrDefault<int>(
            renderSettings,
            HdRestirRenderSettingsTokens->maxRefractionBounces,
            0),
        .RenderIblBackground = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->renderIblBackground,
            false),
    };
    settings.PathTracer.Denoiser = Denoiser::Config{
        .EnableDenoiser = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->enableDenoiser,
            false),
        .EnableFireflyFilter = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->enableFireflyFilter,
            false),
        .EnableChromaticityBlur = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->enableChromaticityBlur,
            false),
    };
    settings.PathTracer.PostProcess = PostProcess::Config{
        .EnableLensFlare = GetRenderSettingOrDefault<bool>(
            renderSettings,
            HdRestirRenderSettingsTokens->enableLensFlare,
            false),
        .ChromaticAberration = GetRenderSettingOrDefault<float>(
            renderSettings,
            HdRestirRenderSettingsTokens->chromaticAberration,
            0.0f),
    };

    return settings;
}

TfToken GetPathTracerPipelineToken()
{
    return TfToken{"PathTracer"};
}

TfToken GetPathTracerPostProcessPipelineToken()
{
    return TfToken{"PathTracerPostProcess"};
}

}  // namespace Restir