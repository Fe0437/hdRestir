#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdsi/implicitSurfaceSceneIndex.h"
#include "implicit_surface_scene_index_plugin.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdRestir_ImplicitSurfaceSceneIndexPlugin"))
);

static const char * const _pluginDisplayName = "Restir";

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdRestir_ImplicitSurfaceSceneIndexPlugin>();
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        _pluginDisplayName,
        _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);

    // HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
    //     _pluginDisplayName,
    //     TfToken("HdSkelSceneIndexPlugin"),
    //     /* inputArgs = */ nullptr,
    //     insertionPhase,
    //     HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdRestir_ImplicitSurfaceSceneIndexPlugin::HdRestir_ImplicitSurfaceSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdRestir_ImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    HdDataSourceBaseHandle const toMeshSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);

    HdContainerDataSourceHandle const localInputArgs =
        HdRetainedContainerDataSource::New(
            HdPrimTypeTokens->sphere, toMeshSrc,
            HdPrimTypeTokens->cube, toMeshSrc,
            HdPrimTypeTokens->cone, toMeshSrc,
            HdPrimTypeTokens->cylinder, toMeshSrc,
            HdPrimTypeTokens->capsule, toMeshSrc,
            HdPrimTypeTokens->plane, toMeshSrc);

    return HdsiImplicitSurfaceSceneIndex::New(inputScene, localInputArgs);
}
