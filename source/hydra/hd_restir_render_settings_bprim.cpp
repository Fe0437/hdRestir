#include "hd_restir_render_settings_bprim.h"

#include "hd_restir_render_delegate.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdRestirRenderSettingsBprim::HdRestirRenderSettingsBprim(SdfPath const &id, HdRestirRenderDelegate *delegate)
    : HdRenderSettings(id), _delegate(delegate)
{
}

void HdRestirRenderSettingsBprim::_Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam,
                                        const HdDirtyBits *dirtyBits)
{
    if (!(*dirtyBits & DirtyNamespacedSettings))
    {
        return;
    }
    const NamespacedSettings &current = GetNamespacedSettings();
    for (const auto &[key, value] : current)
    {
        const auto it = _lastApplied.find(key);
        if (it == _lastApplied.end() || it->second != value)
        {
            _delegate->SetRenderSetting(TfToken(key), value);
        }
    }
    _lastApplied = current;
}
