#include "hd_restir_light.h"
#include "hd_restir_render_delegate.h"
#include "hd_restir_render_param.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"

#include <cmath>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

HdRestirLight::HdRestirLight(SdfPath const& id, TfToken const& lightType)
    : HdLight(id)
{
    if      (lightType == HdPrimTypeTokens->distantLight) _lightType = Restir::LightType::Distant;
    else if (lightType == HdPrimTypeTokens->domeLight)    _lightType = Restir::LightType::Dome;
    else if (lightType == HdPrimTypeTokens->rectLight)    _lightType = Restir::LightType::Rect;
    else                                                  _lightType = Restir::LightType::Point;
}

void HdRestirLight::Sync(HdSceneDelegate *sceneDelegate,
                         HdRenderParam   *renderParam,
                         HdDirtyBits     *dirtyBits)
{
    SdfPath const& id = GetId();

    if (*dirtyBits & HdLight::DirtyTransform) {
        _params.Transform = sceneDelegate->GetTransform(id);
    }
    
    if (*dirtyBits & HdLight::DirtyParams) {
        VtValue colorVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->color);
        if (colorVal.IsHolding<GfVec3f>()) {
            _params.Color = colorVal.UncheckedGet<GfVec3f>();
        }

        VtValue intensityVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity);
        if (intensityVal.IsHolding<float>()) {
            _params.Intensity = intensityVal.UncheckedGet<float>();
        }

        VtValue exposureVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure);
        if (exposureVal.IsHolding<float>()) {
            _params.Exposure = exposureVal.UncheckedGet<float>();
        }

        VtValue coneAngleVal = sceneDelegate->GetLightParamValue(id, TfToken("shaping:cone:angle"));
        if (coneAngleVal.IsHolding<float>()) {
            _params.ShapingConeAngle = coneAngleVal.UncheckedGet<float>();
        }

        VtValue coneSoftnessVal = sceneDelegate->GetLightParamValue(id, TfToken("shaping:cone:softness"));
        if (coneSoftnessVal.IsHolding<float>()) {
            _params.ShapingConeSoftness = coneSoftnessVal.UncheckedGet<float>();
        }

        if (_lightType == Restir::LightType::Dome) {
            VtValue textureVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
            if (textureVal.IsHolding<SdfAssetPath>()) {
                _params.TextureFile = textureVal.UncheckedGet<SdfAssetPath>();
            }
        } else if (_lightType == Restir::LightType::Rect) {
            VtValue widthVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
            if (widthVal.IsHolding<float>()) {
                _params.Width = widthVal.UncheckedGet<float>();
            }
            VtValue heightVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
            if (heightVal.IsHolding<float>()) {
                _params.Height = heightVal.UncheckedGet<float>();
            }
        }

        HdRestir_LOG << "[Restir] Syncing light " << id.GetText() << " (type: " << static_cast<int>(_lightType) << "):" << std::endl;
        HdRestir_LOG << "[Restir]   Intensity: " << _params.Intensity << " | Exposure: " << _params.Exposure << " | Final: " << GetIntensity() << std::endl;
        if (_params.ShapingConeAngle < 180.0f) {
            HdRestir_LOG << "[Restir]   Shaping: Angle=" << _params.ShapingConeAngle << " | Softness=" << _params.ShapingConeSoftness << std::endl;
        }
    }

    auto* restirRenderParam{static_cast<HdRestirRenderParam*>(renderParam)};
    restirRenderParam->AcquireSceneForEdit();
    restirRenderParam->GetScene()->SetLightFactoryInput(id, Restir::LightFactoryInput{
        .Id = id,
        .Type = GetLightType(),
        .Params = GetParams(),
    });

    *dirtyBits &= ~HdLight::AllDirty;
}

HdDirtyBits
HdRestirLight::GetInitialDirtyBitsMask() const
{
    return HdLight::AllDirty;
}
