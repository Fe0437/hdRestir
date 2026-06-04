#pragma once

#include "pxr/pxr.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/imaging/hd/renderSettings.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderDelegate;

class HdRestirRenderSettingsBprim final : public HdRenderSettings {
public:
    HdRestirRenderSettingsBprim(SdfPath const& id, HdRestirRenderDelegate* delegate);
    ~HdRestirRenderSettingsBprim() override = default;

protected:
    void _Sync(HdSceneDelegate* sceneDelegate,
               HdRenderParam* renderParam,
               const HdDirtyBits* dirtyBits) override;

private:
    HdRestirRenderDelegate* _delegate{};
    VtDictionary _lastApplied;
};
