#ifndef HD_RESTIR_RENDERER_PLUGIN_H
#define HD_RESTIR_RENDERER_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRendererPlugin final : public HdRendererPlugin
{
public:
    HdRestirRendererPlugin() = default;
    virtual ~HdRestirRendererPlugin() = default;

    virtual HdRenderDelegate *CreateRenderDelegate() override;
    virtual HdRenderDelegate *CreateRenderDelegate(
        HdRenderSettingsMap const& settingsMap) override;

    virtual void DeleteRenderDelegate(
        HdRenderDelegate *renderDelegate) override;

#if PXR_VERSION >= 2600
    virtual bool IsSupported(HdRendererCreateArgs const& createArgs,
                             std::string *reasonWhyNot = nullptr) const override;
#else
    virtual bool IsSupported(bool gpuEnabled = true) const override;
#endif

private:
    HdRestirRendererPlugin(const HdRestirRendererPlugin&) = delete;
    HdRestirRendererPlugin &operator =(const HdRestirRendererPlugin&) = delete;
};

#endif // HD_RESTIR_RENDERER_PLUGIN_H
