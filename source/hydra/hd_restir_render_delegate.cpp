#include "hd_restir_render_delegate.h"
#include "hd_restir_render_param.h"
#include "hd_restir_render_pass.h"
#include "hd_restir_render_buffer.h"
#include "hd_restir_mesh.h"
#include "hd_restir_instancer.h"
#include "hd_restir_light.h"
#include "hd_restir_material.h"
#include "hd_restir_render_job_adapter.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/value.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

const TfTokenVector HdRestirRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdRestirRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->extComputation,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->rectLight,
};

const TfTokenVector HdRestirRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
};

std::mutex HdRestirRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdRestirRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdRestirRenderDelegate::_resourceRegistry;

#include <thread>
#include <chrono>

static void _RenderCallback(Restir::Renderer *renderer,
                            HdRenderThread *renderThread,
                            HdRestirRenderDelegate *delegate)
{
    renderer->Clear();
    Restir::Hydra::RenderJob job{renderThread};
    while (!job.IsCancelled() && !renderer->IsConverged()) {
        renderer->Render(job, delegate->GetScene());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

HdRestirRenderDelegate::HdRestirRenderDelegate()
    : HdRenderDelegate()
{
    _Initialize();
}

HdRestirRenderDelegate::HdRestirRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void
HdRestirRenderDelegate::_Initialize()
{
    _sceneVersion.store(0);
    _renderParam = std::make_shared<HdRestirRenderParam>(
        &_renderThread, &_renderer, &_scene, &_sceneVersion);

    _renderThread.SetRenderCallback(
        std::bind(_RenderCallback, &_renderer, &_renderThread, this));
    _renderThread.StartThread();

    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry = HdResourceRegistrySharedPtr{new HdResourceRegistry()};
    }
}

HdRestirRenderDelegate::~HdRestirRenderDelegate()
{
    {
        std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
        if (_counterResourceRegistry.fetch_sub(1) == 1) {
            _resourceRegistry.reset();
        }
    }
    _renderThread.StopThread();
}

HdRenderParam*
HdRestirRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

const TfTokenVector&
HdRestirRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

const TfTokenVector&
HdRestirRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

const TfTokenVector&
HdRestirRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

TfTokenVector
HdRestirRenderDelegate::GetShaderSourceTypes() const
{
    return {TfToken("mtlx"), TfToken("UsdPreviewSurface"), TfToken("preview")};
}

TfTokenVector
HdRestirRenderDelegate::GetMaterialRenderContexts() const
{
    return {TfToken("mtlx")};
}

HdResourceRegistrySharedPtr
HdRestirRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdRenderPassSharedPtr
HdRestirRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr{
        new HdRestirRenderPass(index, collection, &_renderThread, &_renderer, &_sceneVersion)};
}

HdInstancer *
HdRestirRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id)
{
    HdRestir_LOG << "[Restir] CreateInstancer: " << id.GetText() << std::endl;
    auto instancer{std::make_unique<HdRestirInstancer>(delegate, id)};
    AddInstancer(id, instancer.get());
    return instancer.release();
}

void
HdRestirRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    RemoveInstancer(instancer->GetId());
    delete instancer;
}

HdRprim *
HdRestirRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId)
{
    HdRestir_LOG << "[Restir] CreateRprim: " << typeId.GetText() << " " << rprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->mesh) {
        auto rPrim{std::make_unique<HdRestirMesh>(rprimId)};
        return rPrim.release();
    }
    return nullptr;
}

void
HdRestirRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    if (rPrim) {
        RemoveMesh(rPrim->GetId());
        delete rPrim;
    }
}

HdSprim *
HdRestirRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    HdRestir_LOG << "[Restir] CreateSprim: " << typeId.GetText() << " " << sprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->camera) {
        auto sprim{std::make_unique<HdCamera>(sprimId)};
        return sprim.release();
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        auto sprim{std::make_unique<HdExtComputation>(sprimId)};
        return sprim.release();
    } else if (typeId == HdPrimTypeTokens->material) {
        auto mat{std::make_unique<HdRestirMaterial>(sprimId)};
        AddMaterial(sprimId, mat.get());
        return mat.release();
    } else if (typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->sphereLight ||
               typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->rectLight) {
        auto sprim{std::make_unique<HdRestirLight>(sprimId, typeId)};
        return sprim.release();
    }
    return nullptr;
}

HdSprim *
HdRestirRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    HdRestir_LOG << "[Restir] CreateFallbackSprim: " << typeId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->camera) {
        auto sprim{std::make_unique<HdCamera>(SdfPath::EmptyPath())};
        return sprim.release();
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        auto sprim{std::make_unique<HdExtComputation>(SdfPath::EmptyPath())};
        return sprim.release();
    } else if (typeId == HdPrimTypeTokens->material) {
        auto mat{std::make_unique<HdRestirMaterial>(SdfPath::EmptyPath())};
        AddMaterial(SdfPath::EmptyPath(), mat.get());
        return mat.release();
    } else if (typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->sphereLight ||
               typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->rectLight) {
        auto sprim{std::make_unique<HdRestirLight>(SdfPath::EmptyPath(), typeId)};
        return sprim.release();
    }
    return nullptr;
}

void
HdRestirRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    if (sPrim) {
        if (dynamic_cast<HdRestirMaterial*>(sPrim) != nullptr) {
            RemoveMaterial(sPrim->GetId());
        } else if (dynamic_cast<HdRestirLight*>(sPrim) != nullptr) {
            RemoveLight(sPrim->GetId());
        }
        delete sPrim;
    }
}

HdBprim *
HdRestirRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    HdRestir_LOG << "[Restir] CreateBprim: " << typeId.GetText() << " " << bprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        auto bPrim{std::make_unique<HdRestirRenderBuffer>(bprimId)};
        return bPrim.release();
    }
    return nullptr;
}

HdBprim *
HdRestirRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    HdRestir_LOG << "[Restir] CreateFallbackBprim: " << typeId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        auto bPrim{std::make_unique<HdRestirRenderBuffer>(SdfPath::EmptyPath())};
        return bPrim.release();
    }
    return nullptr;
}

void
HdRestirRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

void
HdRestirRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
}

HdAovDescriptor
HdRestirRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatFloat32Vec4, true,
                               VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdRestirAovTokens->albedo) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    } else if (name == HdRestirAovTokens->normal) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }
    return HdAovDescriptor();
}

HdRenderSettingDescriptorList
HdRestirRenderDelegate::GetRenderSettingDescriptors() const
{
    HdRenderSettingDescriptorList list;
    for (const auto& spec : Restir::Renderer::GetRenderOptionSpecs()) {
        list.push_back(HdRenderSettingDescriptor{std::string{spec.Label}, spec.Token, spec.DefaultValue});
    }
    return list;
}

VtValue
HdRestirRenderDelegate::GetRenderSetting(TfToken const& key) const
{
    VtValue v = HdRenderDelegate::GetRenderSetting(key);
    if (!v.IsEmpty()) {
        return v;
    }
    return _renderer.GetRenderSetting(key);
}

void
HdRestirRenderDelegate::SetRenderSetting(TfToken const& key, VtValue const& value)
{
    const VtValue current{_renderer.GetRenderSetting(key)};
    if (current.IsEmpty()) {
        HdRenderDelegate::SetRenderSetting(key, value);
        return;
    }

    const bool changed{_renderer.SetRenderSetting(key, value)};
    const VtValue normalized{_renderer.GetRenderSetting(key)};

    if (changed) {
        _renderThread.StopRender();
        _renderer.Clear();
    }
    
    HdRenderDelegate::SetRenderSetting(key, normalized);
}

void
HdRestirRenderDelegate::AddMesh(const SdfPath& id, HdRestirMesh* mesh)
{
    _scene.AddMesh(id, mesh);
}

void
HdRestirRenderDelegate::RemoveMesh(const SdfPath& id)
{
    _scene.RemoveMesh(id);
}

void
HdRestirRenderDelegate::AddLight(const SdfPath& id, HdRestirLight* light)
{
    _scene.SetLightFactoryInput(id, Restir::LightFactoryInput{
        .Id = id,
        .Type = light->GetLightType(),
        .Params = light->GetParams(),
    });
}

void
HdRestirRenderDelegate::RemoveLight(const SdfPath& id)
{
    _scene.RemoveLightFactoryInput(id);
}

void
HdRestirRenderDelegate::AddInstancer(const SdfPath& id, HdRestirInstancer* instancer)
{
    _scene.AddInstancer(id, instancer);
}

void
HdRestirRenderDelegate::RemoveInstancer(const SdfPath& id)
{
    _scene.RemoveInstancer(id);
}

void
HdRestirRenderDelegate::AddMaterial(const SdfPath& id, HdRestirMaterial* material)
{
    _scene.SetMaterialParams(id, material->GetParams());
}

void
HdRestirRenderDelegate::RemoveMaterial(const SdfPath& id)
{
    _scene.RemoveMaterialParams(id);
}
