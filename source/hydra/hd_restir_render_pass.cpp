#include "hd_restir_render_pass.h"

#include "renderer.h"

#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

    [[nodiscard]] std::vector<Restir::AovBinding>
    MakeRendererAovBindings(const HdRenderPassAovBindingVector &aovBindings)
    {
        std::vector<Restir::AovBinding> rendererAovBindings{};
        rendererAovBindings.reserve(aovBindings.size());
        for (const auto &binding : aovBindings)
        {
            auto *target{dynamic_cast<Restir::IFrameBufferTarget *>(binding.renderBuffer)};
            rendererAovBindings.push_back(Restir::AovBinding{
                .AovName    = binding.aovName,
                .Target     = target,
                .ClearValue = binding.clearValue,
            });
        }
        return rendererAovBindings;
    }

} // namespace

HdRestirRenderPass::HdRestirRenderPass(HdRenderIndex *index, HdRprimCollection const &collection,
                                       HdRenderThread *renderThread, Restir::Renderer *renderer,
                                       std::atomic<int> *sceneVersion)
    : HdRenderPass(index, collection), _renderThread(renderThread), _renderer(renderer), _sceneVersion(sceneVersion)
{
}

HdRestirRenderPass::~HdRestirRenderPass()
{
    _renderThread->StopRender();
}

bool HdRestirRenderPass::IsConverged() const
{
    return _renderer == nullptr || _renderer->IsConverged();
}

static GfRect2i _GetDataWindow(HdRenderPassStateSharedPtr const &renderPassState)
{
    const CameraUtilFraming &framing{renderPassState->GetFraming()};
    if (framing.IsValid())
    {
        return framing.dataWindow;
    }

    const GfVec4f vp{renderPassState->GetViewport()};
    return GfRect2i{GfVec2i{0}, static_cast<int>(vp[2]), static_cast<int>(vp[3])};
}

void HdRestirRenderPass::_Execute(HdRenderPassStateSharedPtr const &renderPassState, TfTokenVector const &renderTags)
{
    bool needStartRender{false};
    int  currentSceneVersion{_sceneVersion->load()};
    if (_lastSceneVersion != currentSceneVersion)
    {
        needStartRender   = true;
        _lastSceneVersion = currentSceneVersion;
    }

    const GfMatrix4d view{renderPassState->GetWorldToViewMatrix()};
    const GfMatrix4d proj{renderPassState->GetProjectionMatrix()};
    // Camera changes require stopping the current render and reconfiguring.
    if (_viewMatrix != view || _projMatrix != proj)
    {
        _viewMatrix = view;
        _projMatrix = proj;
        _renderThread->StopRender();
        _renderer->SetCamera(_viewMatrix, _projMatrix);
        needStartRender = true;
    }

    const GfRect2i dataWindow{_GetDataWindow(renderPassState)};
    // Resolution/framing changes require reallocating fallback AOV buffers.
    if (_dataWindow != dataWindow)
    {
        _dataWindow = dataWindow;
        _renderThread->StopRender();
        _renderer->SetDataWindow(dataWindow);

        if (!renderPassState->GetFraming().IsValid())
        {
            const GfVec3i dimensions(_dataWindow.GetWidth(), _dataWindow.GetHeight(), 1);
            _colorBuffer.Allocate(dimensions, HdFormatUNorm8Vec4, false);
            _depthBuffer.Allocate(dimensions, HdFormatFloat32, false);
        }

        needStartRender = true;
    }

    HdRenderPassAovBindingVector aovBindings{renderPassState->GetAovBindings()};
    if (aovBindings.empty())
    {
        HdRenderPassAovBinding colorAov{};
        colorAov.aovName      = HdAovTokens->color;
        colorAov.renderBuffer = &_colorBuffer;
        colorAov.clearValue   = VtValue{GfVec4f{0.0f, 0.0f, 0.0f, 1.0f}};
        aovBindings.push_back(colorAov);
        HdRenderPassAovBinding depthAov{};
        depthAov.aovName      = HdAovTokens->depth;
        depthAov.renderBuffer = &_depthBuffer;
        depthAov.clearValue   = VtValue{1.0f};
        aovBindings.push_back(depthAov);
    }

    const auto rendererAovBindings{MakeRendererAovBindings(aovBindings)};
    const auto requestedOutputNames{Restir::Renderer::CollectRequestedOutputNames(rendererAovBindings)};
    const bool aovBindingsChanged{!_hasBoundAovs || _requestedOutputNames != requestedOutputNames};
    if (aovBindingsChanged)
    {
        _renderThread->StopRender();
        _requestedOutputNames = requestedOutputNames;
        _hasBoundAovs         = true;
        _renderer->SetRequestedOutputNames(_requestedOutputNames);
        _renderer->Clear();
        _renderer->ClearTargets(rendererAovBindings);
        needStartRender = true;
    }

    _renderer->ResolveTargets(rendererAovBindings);

    if (needStartRender)
    {
        // Start (or restart) rendering only when state changes occurred.
        _renderer->MarkAovBuffersUnconverged(rendererAovBindings);
        _renderThread->StartRender();
    }
}
