#ifndef HD_RESTIR_RENDER_DELEGATE_H
#define HD_RESTIR_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "hd_restir_instancer.h"
#include "hd_restir_light.h"
#include "hd_restir_material.h"
#include "hd_restir_mesh.h"
#include "hd_restir_render_param.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "restir_aov_tokens.h"
#include "restir_render_settings.h"
#include "renderer.h"
#include "scene/scene.h"

#include <iostream>

#ifdef HdRestir_DEBUG_PRINTS
    #define HdRestir_LOG std::cout
#else
    #define HdRestir_LOG while(false) std::cout
#endif

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderDelegate final : public HdRenderDelegate
{
public:
    HdRestirRenderDelegate();
    HdRestirRenderDelegate(HdRenderSettingsMap const& settingsMap);
    ~HdRestirRenderDelegate() override;

    HdRenderParam* GetRenderParam() const override;

    const TfTokenVector& GetSupportedRprimTypes() const override;
    const TfTokenVector& GetSupportedSprimTypes() const override;
    const TfTokenVector& GetSupportedBprimTypes() const override;
    TfTokenVector GetShaderSourceTypes() const override;
    TfTokenVector GetMaterialRenderContexts() const override;

    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                                           HdRprimCollection const& collection) override;

    HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                 SdfPath const& id) override;
    void DestroyInstancer(HdInstancer *instancer) override;

    HdRprim *CreateRprim(TfToken const& typeId,
                         SdfPath const& rprimId) override;
    void DestroyRprim(HdRprim *rPrim) override;

    HdSprim *CreateSprim(TfToken const& typeId,
                         SdfPath const& sprimId) override;
    HdSprim *CreateFallbackSprim(TfToken const& typeId) override;
    void DestroySprim(HdSprim *sPrim) override;

    HdBprim *CreateBprim(TfToken const& typeId,
                         SdfPath const& bprimId) override;
    HdBprim *CreateFallbackBprim(TfToken const& typeId) override;
    void DestroyBprim(HdBprim *bPrim) override;

    void CommitResources(HdChangeTracker *tracker) override;

    HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;

    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
    VtValue GetRenderSetting(TfToken const& key) const override;
    void SetRenderSetting(TfToken const& key, VtValue const& value) override;

    void AddMesh(const SdfPath& id, HdRestirMesh* mesh);
    void RemoveMesh(const SdfPath& id);

    void AddInstancer(const SdfPath& id, HdRestirInstancer* instancer);
    void RemoveInstancer(const SdfPath& id);

    void AddMaterial(const SdfPath& id, HdRestirMaterial* material);
    void RemoveMaterial(const SdfPath& id);

    void AddLight(const SdfPath& id, HdRestirLight* light);
    void RemoveLight(const SdfPath& id);

    [[nodiscard]] Restir::Scene& GetScene() { return _scene; }
    [[nodiscard]] const Restir::Scene& GetScene() const { return _scene; }

    std::recursive_mutex& GetSceneLock() { return _scene.GetSceneLock(); }

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    static std::mutex _mutexResourceRegistry;
    static std::atomic_int _counterResourceRegistry;
    static HdResourceRegistrySharedPtr _resourceRegistry;

    void _Initialize();

    std::shared_ptr<HdRestirRenderParam> _renderParam{};
    HdRenderThread _renderThread;
    Restir::Renderer _renderer;
    std::atomic<int> _sceneVersion{0};
    Restir::Scene _scene;

    HdRestirRenderDelegate(const HdRestirRenderDelegate &) = delete;
    HdRestirRenderDelegate &operator =(const HdRestirRenderDelegate &) = delete;
};

#endif // HD_RESTIR_RENDER_DELEGATE_H
