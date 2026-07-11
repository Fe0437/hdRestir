#include "renderer_pipeline_state.h"

#include "restir_render_settings.h"
#include "ris_pipeline.h"

namespace Restir
{

    namespace
    {

        enum class PipelineKind
        {
            RIS,
            PathTracer
        };

        [[nodiscard]] PipelineKind ParsePipelineKind(const TfToken &token)
        {
            if (token == GetRISPipelineToken())
                return PipelineKind::RIS;
            return PipelineKind::PathTracer;
        }

        template <typename T>
        [[nodiscard]] T Get(const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> &values,
                            const TfToken &key, T fallback)
        {
            const auto it{values.find(key)};
            if (it == values.end() || !it->second.IsHolding<T>())
            {
                return fallback;
            }
            return it->second.Get<T>();
        }

        [[nodiscard]] PathTracerPipelineSettings MakePathTracerSettings(const RendererPipelineSettings &s)
        {
            const auto &v{s.Values};
            return PathTracerPipelineSettings{
                .MaxDepth        = Get<int>(v, HdRestirRenderSettingsTokens->maxDepth, 32),
                .ResolutionLevel = Get<int>(v, HdRestirRenderSettingsTokens->resolutionLevel, 0),
                .OutputNames     = s.OutputNames,
                .PathTrace =
                    PathTracePassSettings{
                        .EnableSubsurface     = Get<bool>(v, HdRestirRenderSettingsTokens->enableSubsurface, false),
                        .MaxReflectionBounces = Get<int>(v, HdRestirRenderSettingsTokens->maxReflectionBounces, 0),
                        .MaxRefractionBounces = Get<int>(v, HdRestirRenderSettingsTokens->maxRefractionBounces, 0),
                        .RenderIblBackground  = Get<bool>(v, HdRestirRenderSettingsTokens->renderIblBackground, false),
                        .RouletteAggressiveness =
                            Get<float>(v, HdRestirRenderSettingsTokens->rouletteAggressiveness, 1.0f),
                    },
                .Denoiser =
                    Denoiser::Config{
                        .EnableDenoiser      = Get<bool>(v, HdRestirRenderSettingsTokens->enableDenoiser, false),
                        .EnableFireflyFilter = Get<bool>(v, HdRestirRenderSettingsTokens->enableFireflyFilter, false),
                        .EnableChromaticityBlur =
                            Get<bool>(v, HdRestirRenderSettingsTokens->enableChromaticityBlur, false),
                    },
                .PostProcess =
                    PostProcess::Config{
                        .EnableLensFlare     = Get<bool>(v, HdRestirRenderSettingsTokens->enableLensFlare, false),
                        .ChromaticAberration = Get<float>(v, HdRestirRenderSettingsTokens->chromaticAberration, 0.0f),
                    },
                .UseGpu = Get<bool>(v, HdRestirRenderSettingsTokens->useGpu, PathTracerPipelineSettings::DefaultUseGpu),
#if DEBUG_ENABLED
                .DebugOverlay =
                    DebugOverlayPass::Config{
                        .Enable = Get<bool>(v, HdRestirRenderSettingsTokens->debugOverlay, false),
#if METRICS_ENABLED
                        .EnableProfiling = Get<bool>(v, HdRestirRenderSettingsTokens->profileOverlay, false),
#endif
                    },
#endif
            };
        }

        [[nodiscard]] RISPipelineSettings MakeRISSettings(const RendererPipelineSettings &s)
        {
            return RISPipelineSettings{
                .PathTracer     = MakePathTracerSettings(s),
                .CandidateCount = Get<int>(s.Values, HdRestirRenderSettingsTokens->risCandidateCount, 16),
                .UseReservoir   = Get<bool>(s.Values, HdRestirRenderSettingsTokens->risUseReservoir, true),
                .SkipVisibility = Get<bool>(s.Values, HdRestirRenderSettingsTokens->risSkipVisibility, false),
            };
        }

        // Maps each split-screen right-side override token to the base setting it replaces.
        // To make any setting independently configurable per side, add a row here and declare
        // the matching splitScreen:* token in restir_render_settings.h. No other code changes needed.
        static const std::pair<TfToken, TfToken> kRightPipelineOverrides[] = {
            {HdRestirRenderSettingsTokens->splitScreenRisUseReservoir, HdRestirRenderSettingsTokens->risUseReservoir},
        };

        [[nodiscard]] std::unique_ptr<RenderPipeline> MakeBuiltinPipeline(PipelineKind kind, std::string &&name,
                                                                          const RendererPipelineSettings &settings)
        {
            switch (kind)
            {
                case PipelineKind::RIS:
                    return MakeRISPipeline(std::move(name), MakeRISSettings(settings));
                case PipelineKind::PathTracer:
                    return makePathTracerPipeline(std::move(name), MakePathTracerSettings(settings));
                    // To re-add PathTracerPostProcess: add PipelineKind::PathTracerPostProcess, register
                    // its token via GetPathTracerPostProcessPipelineToken(), and add a case here that calls
                    // makePathTracerPostProcessPipeline() from path_tracer_pipeline.h.
            }
            return makePathTracerPipeline(std::move(name), MakePathTracerSettings(settings));
        }

    } // namespace

    RendererPipelineState::RendererPipelineState(const RendererPipelineSettings &settings)
    {
        const PipelineKind primary{ParsePipelineKind(settings.PrimaryPipeline)};

        if (settings.EnableSplitScreen)
        {
            const PipelineKind right{ParsePipelineKind(settings.SplitScreenRightPipeline)};
            const int          resLevel{Get<int>(settings.Values, HdRestirRenderSettingsTokens->resolutionLevel, 0)};
            const int leftTarget{Get<int>(settings.Values, HdRestirRenderSettingsTokens->targetSampleCount, 32)};
            const int rightTarget{
                Get<int>(settings.Values, HdRestirRenderSettingsTokens->splitScreenTargetSampleCount, 32)};

            RendererPipelineSettings rightSettings{settings};
            for (const auto &[splitToken, baseToken] : kRightPipelineOverrides)
            {
                const auto it{rightSettings.Values.find(splitToken)};
                if (it != rightSettings.Values.end())
                {
                    rightSettings.Values[baseToken] = it->second;
                }
            }

            _splitScreen =
                std::make_unique<SplitScreenCompositor>(MakeBuiltinPipeline(primary, "LeftPipeline", settings),
                                                        MakeBuiltinPipeline(right, "RightPipeline", rightSettings),
                                                        resLevel, resLevel, leftTarget, rightTarget);
            return;
        }

        _singlePipeline = MakeBuiltinPipeline(primary, "PrimaryPipeline", settings);
    }

    void RendererPipelineState::ClearPersistentBuffers()
    {
        if (_splitScreen)
        {
            _splitScreen->ClearPersistentBuffers();
        }
        else if (_singlePipeline)
        {
            _singlePipeline->ClearPersistentBuffers();
        }
    }

    void RendererPipelineState::Execute(RenderContext &ctx)
    {
        if (_splitScreen != nullptr)
        {
            _splitScreen->Execute(ctx);
            return;
        }

        Expects(_singlePipeline != nullptr);
        _singlePipeline->Execute(ctx);
    }

    RendererPipelineSettings
    MakeRendererPipelineSettings(const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> &renderSettings,
                                 gsl::span<const std::string>                                      requestedOutputNames)
    {
        return RendererPipelineSettings{
            .Values                   = renderSettings,
            .PrimaryPipeline          = Get<TfToken>(renderSettings, HdRestirRenderSettingsTokens->primaryPipeline,
                                                     GetPathTracerPipelineToken()),
            .SplitScreenRightPipeline = Get<TfToken>(
                renderSettings, HdRestirRenderSettingsTokens->splitScreenRightPipeline, GetRISPipelineToken()),
            .EnableSplitScreen = Get<bool>(renderSettings, HdRestirRenderSettingsTokens->enableSplitScreen, false),
            .OutputNames       = {requestedOutputNames.begin(), requestedOutputNames.end()},
        };
    }

    bool RendererPipelineState::IsConverged(int frameCount, int singleTarget) const noexcept
    {
        if (_splitScreen)
        {
            return _splitScreen->IsConverged();
        }
        return frameCount >= singleTarget;
    }

    TfToken GetPathTracerPipelineToken()
    {
        return TfToken{"PathTracer"};
    }
    TfToken GetRISPipelineToken()
    {
        return TfToken{"RIS"};
    }

} // namespace Restir
