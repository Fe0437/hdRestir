#include "renderer_plugin.h"

#include "hydra/hd_restir_render_delegate.h"
#include "pxr/imaging/hd/rendererPluginRegistry.h"

PXR_NAMESPACE_USING_DIRECTIVE

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<HdRestirRendererPlugin>();
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

HdRenderDelegate *HdRestirRendererPlugin::CreateRenderDelegate()
{
    return new HdRestirRenderDelegate();
}

HdRenderDelegate *HdRestirRendererPlugin::CreateRenderDelegate(HdRenderSettingsMap const &settingsMap)
{
    return new HdRestirRenderDelegate(settingsMap);
}

void HdRestirRendererPlugin::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    delete renderDelegate;
}

#if PXR_VERSION >= 2600
bool HdRestirRendererPlugin::IsSupported(HdRendererCreateArgs const & /* createArgs */,
                                         std::string * /* reasonWhyNot */) const
{
    return true;
}
#else
bool HdRestirRendererPlugin::IsSupported(bool /* gpuEnabled */) const
{
    return true;
}
#endif
