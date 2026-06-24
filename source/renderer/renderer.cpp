#include "renderer.h"

#include "frame_buffer_map.h"
#include "output_names.h"
#include "render_context.h"
#include "renderer_pipeline_state.h"
#include "restir_aov_tokens.h"
#include "restir_render_settings.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>
#include <string>
#include <string_view>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

    using AovTargetMap = std::unordered_map<TfToken, Restir::IFrameBufferTarget *, TfToken::HashFunctor>;

    struct AovOutputSpec
    {
        TfToken          AovName;
        std::string_view FrameBufferName;
    };

    [[nodiscard]] const std::array<AovOutputSpec, 4> &GetAovOutputSpecs()
    {
        static const std::array<AovOutputSpec, 4> specs{{
            {HdAovTokens->color, Restir::kColorOutputName},
            {HdAovTokens->depth, Restir::kDepthOutputName},
            {HdRestirAovTokens->albedo, "Albedo"},
            {HdRestirAovTokens->normal, "Normal"},
        }};
        return specs;
    }

    [[nodiscard]] const AovOutputSpec *FindAovOutputSpec(const TfToken &aovName)
    {
        for (const auto &spec : GetAovOutputSpecs())
        {
            if (spec.AovName == aovName)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    [[nodiscard]] AovTargetMap FindActiveAovTargets(gsl::span<const Restir::AovBinding> aovBindings)
    {
        AovTargetMap targets{};
        for (const auto &binding : aovBindings)
        {
            if (binding.Target == nullptr)
            {
                continue;
            }

            targets.insert_or_assign(binding.AovName, binding.Target);
        }
        return targets;
    }

    [[nodiscard]] bool TryParseEnvironmentBool(std::string_view text, bool &parsedValue)
    {
        if (text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON")
        {
            parsedValue = true;
            return true;
        }
        if (text == "0" || text == "false" || text == "FALSE" || text == "off" || text == "OFF")
        {
            parsedValue = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool TryParseEnvironmentInt(std::string_view text, int &parsedValue)
    {
        try
        {
            std::size_t parsedLength{0};
            const int   value{std::stoi(std::string{text}, &parsedLength)};
            if (parsedLength != text.size())
            {
                return false;
            }
            parsedValue = value;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] bool TryParseEnvironmentFloat(std::string_view text, float &parsedValue)
    {
        try
        {
            std::size_t parsedLength{0};
            const float value{std::stof(std::string{text}, &parsedLength)};
            if (parsedLength != text.size())
            {
                return false;
            }
            parsedValue = value;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] std::string MakeRenderSettingEnvironmentName(const TfToken &token)
    {
        std::string            envName{"HDRESTIR_"};
        const std::string_view tokenText{token.GetString()};
        for (std::size_t i{0}; i < tokenText.size(); ++i)
        {
            const unsigned char ch{static_cast<unsigned char>(tokenText[i])};
            if (std::isupper(ch) != 0 && i > 0)
            {
                envName.push_back('_');
            }
            envName.push_back(static_cast<char>(std::toupper(ch)));
        }
        return envName;
    }

    [[nodiscard]] VtValue ParseEnvironmentOverrideValue(const Restir::Renderer::RenderOptionSpec &spec,
                                                        std::string_view                          rawValue)
    {
        if (spec.DefaultValue.IsHolding<bool>())
        {
            bool parsedValue{false};
            return TryParseEnvironmentBool(rawValue, parsedValue) ? VtValue{parsedValue} : VtValue{};
        }
        if (spec.DefaultValue.IsHolding<int>())
        {
            int parsedValue{0};
            return TryParseEnvironmentInt(rawValue, parsedValue) ? VtValue{parsedValue} : VtValue{};
        }
        if (spec.DefaultValue.IsHolding<float>())
        {
            float parsedValue{0.0f};
            return TryParseEnvironmentFloat(rawValue, parsedValue) ? VtValue{parsedValue} : VtValue{};
        }
        if (spec.DefaultValue.IsHolding<TfToken>())
        {
            return rawValue.empty() ? VtValue{} : VtValue{TfToken{std::string{rawValue}}};
        }

        return VtValue{};
    }

    void ApplyEnvironmentOverrides(std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> &renderSettings,
                                   gsl::span<const Restir::Renderer::RenderOptionSpec>         specs)
    {
        for (const auto &spec : specs)
        {
            const std::string envName{MakeRenderSettingEnvironmentName(spec.Token)};
            const char       *rawValue{std::getenv(envName.c_str())};
            if (rawValue == nullptr || rawValue[0] == '\0')
            {
                continue;
            }

            const VtValue parsedValue{ParseEnvironmentOverrideValue(spec, rawValue)};
            if (parsedValue.IsEmpty())
            {
                continue;
            }

            renderSettings.insert_or_assign(spec.Token, parsedValue);
        }
    }

    [[nodiscard]] gsl::span<const Restir::Renderer::RenderOptionSpec> GetRenderOptionSpecsArray()
    {
        static const Restir::Renderer::RenderOptionSpec specs[] = {
            HD_RESTIR_RENDER_SETTINGS_SPECS(Restir::Renderer::RenderOptionSpec)};
        return specs;
    }

    [[nodiscard]] const Restir::Renderer::RenderOptionSpec *FindRenderOptionSpec(const TfToken &key)
    {
        for (const auto &spec : GetRenderOptionSpecsArray())
        {
            if (spec.Token == key)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    [[nodiscard]] VtValue NormalizeRenderSettingValue(const Restir::Renderer::RenderOptionSpec &spec,
                                                      const VtValue                            &value)
    {
        if (spec.DefaultValue.IsHolding<bool>())
        {
            return value.IsHolding<bool>() ? value : VtValue{};
        }
        if (spec.DefaultValue.IsHolding<int>())
        {
            if (value.IsHolding<int>())
            {
                return value;
            }
            if (value.IsHolding<float>())
            {
                return VtValue{static_cast<int>(value.Get<float>())};
            }
            if (value.IsHolding<double>())
            {
                return VtValue{static_cast<int>(value.Get<double>())};
            }
            return VtValue{};
        }
        if (spec.DefaultValue.IsHolding<float>())
        {
            if (value.IsHolding<float>())
            {
                return value;
            }
            if (value.IsHolding<double>())
            {
                return VtValue{static_cast<float>(value.Get<double>())};
            }
            if (value.IsHolding<int>())
            {
                return VtValue{static_cast<float>(value.Get<int>())};
            }
            return VtValue{};
        }
        if (spec.DefaultValue.IsHolding<TfToken>())
        {
            if (value.IsHolding<TfToken>())
            {
                return value;
            }
            if (value.IsHolding<std::string>())
            {
                return VtValue{TfToken{value.Get<std::string>()}};
            }
            return VtValue{};
        }
        return value;
    }

} // namespace

Restir::Renderer::Renderer()
{
    const auto &specs{GetRenderOptionSpecsArray()};
    for (const auto &spec : specs)
    {
        _renderSettings.insert_or_assign(spec.Token, spec.DefaultValue);
    }
    ApplyEnvironmentOverrides(_renderSettings, specs);
    _pipelineState = std::make_unique<Restir::RendererPipelineState>(
        Restir::MakeRendererPipelineSettings(_renderSettings, _requestedOutputNames));
#ifdef HdRestir_HAS_OIDN
    std::cout << "[Restir] Renderer initialized WITH Open Image Denoise (OIDN) support." << std::endl;
#else
    std::cout << "[Restir] Renderer initialized WITHOUT Open Image Denoise (OIDN) support." << std::endl;
#endif
}

Restir::Renderer::~Renderer() = default;

gsl::span<const Restir::Renderer::RenderOptionSpec> Restir::Renderer::GetRenderOptionSpecs()
{
    return GetRenderOptionSpecsArray();
}

VtValue Restir::Renderer::GetRenderSetting(const TfToken &key) const
{
    const auto it{_renderSettings.find(key)};
    return it != _renderSettings.end() ? it->second : VtValue{};
}

bool Restir::Renderer::SetRenderSetting(const TfToken &key, const VtValue &value)
{
    const auto *spec{FindRenderOptionSpec(key)};
    if (spec == nullptr)
    {
        return false;
    }

    const VtValue normalized{NormalizeRenderSettingValue(*spec, value)};
    if (normalized.IsEmpty())
    {
        return false;
    }

    const VtValue current{GetRenderSetting(key)};
    if (current == normalized)
    {
        return false;
    }

    _renderSettings.insert_or_assign(key, normalized);

    auto indexToToken = [](int idx) -> TfToken
    {
        if (idx == 1)
            return GetPathTracerPostProcessPipelineToken();
        if (idx == 2)
            return GetRISPipelineToken();
        return GetPathTracerPipelineToken();
    };

    if (key == HdRestirRenderSettingsTokens->pipelineIndex)
    {
        _renderSettings.insert_or_assign(HdRestirRenderSettingsTokens->primaryPipeline,
                                         VtValue{indexToToken(normalized.Get<int>())});
    }

    if (key == HdRestirRenderSettingsTokens->splitScreenRightPipelineIndex)
    {
        _renderSettings.insert_or_assign(HdRestirRenderSettingsTokens->splitScreenRightPipeline,
                                         VtValue{indexToToken(normalized.Get<int>())});
    }

    return true;
}

std::vector<std::string> Restir::Renderer::CollectRequestedOutputNames(gsl::span<const Restir::AovBinding> aovBindings)
{
    std::vector<std::string> outputNames{};
    for (const auto &binding : aovBindings)
    {
        const auto *spec{FindAovOutputSpec(binding.AovName)};
        if (spec == nullptr)
        {
            continue;
        }
        outputNames.push_back(std::string{spec->FrameBufferName});
    }
    return outputNames;
}

void Restir::Renderer::SetCamera(const GfMatrix4d &viewMatrix, const GfMatrix4d &projMatrix)
{
    _camera.ViewMatrix        = viewMatrix;
    _camera.ProjMatrix        = projMatrix;
    _camera.InverseViewMatrix = viewMatrix.GetInverse();
    _camera.InverseProjMatrix = projMatrix.GetInverse();
    _integrandDirty           = true;
}

void Restir::Renderer::SetDataWindow(const GfRect2i &dataWindow)
{
    _camera.DataWindow = dataWindow;
    _integrandDirty    = true;
}

void Restir::Renderer::Render(const Restir::IRenderJob &job, Restir::IScene &scene)
{
    std::lock_guard<std::recursive_mutex> sceneLock{scene.GetSceneLock()};

    const int width{_camera.DataWindow.GetWidth()};
    const int height{_camera.DataWindow.GetHeight()};
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (_integrandDirty)
    {
        _pipelineState->ClearPersistentBuffers();
        _camera.FrameCount = 0;
        _integrandDirty    = false;
    }
    const int windowWidth{width};
    const int windowHeight{height};

    scene.BuildRenderState(
        Restir::SceneBuildRenderStateConfig{
            .EnablePhysicalSky = GetRenderSetting(HdRestirRenderSettingsTokens->physicalSkyEnable).Get<bool>(),
            .PhysicalSkyTime   = GetRenderSetting(HdRestirRenderSettingsTokens->physicalSkyTime).Get<float>(),
        },
        job);
    if (job.IsCancelled())
    {
        return;
    }

    _rng.ResetSeed(static_cast<std::uint32_t>(_camera.FrameCount));
    const bool  enableDoF{GetRenderSetting(HdRestirRenderSettingsTokens->enableDoF).Get<bool>()};
    const float lensDistortion{GetRenderSetting(HdRestirRenderSettingsTokens->lensDistortion).Get<float>()};
    const bool  enablePhysicalCamera{GetRenderSetting(HdRestirRenderSettingsTokens->enablePhysicalCamera).Get<bool>()};
    const bool  usePhysicalCamera{enableDoF || lensDistortion != 0.0f || enablePhysicalCamera};
    std::vector<std::string> outputNames{_requestedOutputNames.begin(), _requestedOutputNames.end()};
    Restir::RenderContext    ctx{
        .scene      = &scene,
        .viewMatrix = _camera.ViewMatrix,
        .projMatrix = _camera.ProjMatrix,
        .frame =
            Restir::CameraFrame{
                .windowWidth     = windowWidth,
                .windowHeight    = windowHeight,
                .resolutionLevel = 0,
                .visibleMinX     = 0,
                .visibleMinY     = 0,
                .visibleMaxX     = windowWidth,
                .visibleMaxY     = windowHeight,
            },
        .frameIndex  = _camera.FrameCount,
        .rng         = _rng,
        .buffers     = Restir::FrameBuffersMap{},
        .OutputNames = std::move(outputNames),
        .cameraParams =
            usePhysicalCamera
                ? std::optional<Restir::CameraParams>{Restir::CameraParams{
                      .enableDoF      = enableDoF,
                      .enableExposure = enablePhysicalCamera,
                      .focalLength    = GetRenderSetting(HdRestirRenderSettingsTokens->focalLength).Get<float>(),
                      .fStop          = GetRenderSetting(HdRestirRenderSettingsTokens->fStop).Get<float>(),
                      .focusDistance  = GetRenderSetting(HdRestirRenderSettingsTokens->focusDistance).Get<float>(),
                      .bokehBlades    = GetRenderSetting(HdRestirRenderSettingsTokens->bokehBlades).Get<int>(),
                      .lensDistortion = lensDistortion,
                      .iso            = GetRenderSetting(HdRestirRenderSettingsTokens->iso).Get<float>(),
                      .shutterSpeed   = GetRenderSetting(HdRestirRenderSettingsTokens->shutterSpeed).Get<float>(),
                  }}
                : std::nullopt,
    };

    _pipelineState->Execute(ctx);
    {
        std::lock_guard<std::mutex> lock{_resolvedOutputsMutex};
        _resolvedOutputs.clear();
        for (const auto &spec : GetAovOutputSpecs())
        {
            if (!ctx.buffers.Has(spec.FrameBufferName))
            {
                continue;
            }

            _resolvedOutputs.insert_or_assign(std::string{spec.FrameBufferName},
                                              ctx.buffers.GetFrameBuffer(spec.FrameBufferName));
        }
    }

    ++_camera.FrameCount;
}

void Restir::Renderer::Clear()
{
    _pipelineState = std::make_unique<Restir::RendererPipelineState>(
        Restir::MakeRendererPipelineSettings(_renderSettings, _requestedOutputNames));
    _camera.FrameCount = 0;
    _integrandDirty    = true;
    std::lock_guard<std::mutex> lock{_resolvedOutputsMutex};
    _resolvedOutputs.clear();
}

void Restir::Renderer::ClearTargets(gsl::span<const Restir::AovBinding> aovBindings)
{
    for (const auto &binding : aovBindings)
    {
        if (binding.Target && !binding.ClearValue.IsEmpty())
        {
            auto *rb = binding.Target;
            rb->SetConverged(false);
            if (binding.AovName == HdAovTokens->color)
            {
                GfVec4f clearColor = _getClearColor(binding.ClearValue);
                rb->Clear(4, clearColor.data());
            }
            else if (binding.AovName == HdAovTokens->depth)
            {
                float clearValue = binding.ClearValue.Get<float>();
                rb->Clear(1, &clearValue);
            }
            rb->Resolve();
        }
    }
}

GfVec4f Restir::Renderer::_getClearColor(VtValue const &clearValue)
{
    if (clearValue.IsHolding<GfVec4f>())
        return clearValue.UncheckedGet<GfVec4f>();
    if (clearValue.IsHolding<GfVec3f>())
    {
        GfVec3f v = clearValue.UncheckedGet<GfVec3f>();
        return GfVec4f(v[0], v[1], v[2], 1.0f);
    }
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
}

void Restir::Renderer::MarkAovBuffersUnconverged(gsl::span<const Restir::AovBinding> aovBindings)
{
    for (const auto &binding : aovBindings)
    {
        if (binding.Target)
        {
            binding.Target->SetConverged(false);
        }
    }
}

void Restir::Renderer::ResolveTargets(gsl::span<const Restir::AovBinding> aovBindings)
{
    std::lock_guard<std::mutex> lock{_resolvedOutputsMutex};
    if (_resolvedOutputs.empty())
    {
        return;
    }

    const auto targets{FindActiveAovTargets(aovBindings)};
    for (const auto &spec : GetAovOutputSpecs())
    {
        const auto targetIt{targets.find(spec.AovName)};
        if (targetIt == targets.end() || targetIt->second == nullptr)
        {
            continue;
        }

        const auto outputIt{_resolvedOutputs.find(std::string{spec.FrameBufferName})};
        if (outputIt == _resolvedOutputs.end())
        {
            continue;
        }

        targetIt->second->CopyFromFrameBuffer(outputIt->second);
        targetIt->second->Resolve();
        targetIt->second->SetConverged(IsConverged());
    }
}

bool Restir::Renderer::IsConverged() const
{
    const int target{GetRenderSetting(HdRestirRenderSettingsTokens->targetSampleCount).Get<int>()};
    return _pipelineState->IsConverged(_camera.FrameCount, target);
}