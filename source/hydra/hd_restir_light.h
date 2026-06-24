#ifndef HD_RESTIR_LIGHT_H
#define HD_RESTIR_LIGHT_H

#include "light_factory.h"
#include "light_params.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirLight final : public HdLight
{
  public:
    HdRestirLight(SdfPath const &id, TfToken const &lightType);

    virtual ~HdRestirLight() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const Restir::LightParams &GetParams() const
    {
        return _params;
    }
    Restir::LightType GetLightType() const noexcept
    {
        return _lightType;
    }

    const GfVec3f &GetColor() const
    {
        return _params.Color;
    }
    float GetIntensity() const
    {
        return _params.EffectiveIntensity();
    }
    const GfMatrix4d &GetTransform() const
    {
        return _params.Transform;
    }
    const SdfAssetPath &GetTextureFile() const
    {
        return _params.TextureFile;
    }
    float GetWidth() const
    {
        return _params.Width;
    }
    float GetHeight() const
    {
        return _params.Height;
    }
    float GetShapingConeAngle() const
    {
        return _params.ShapingConeAngle;
    }
    float GetShapingConeSoftness() const
    {
        return _params.ShapingConeSoftness;
    }

  private:
    Restir::LightParams _params{};
    Restir::LightType   _lightType{Restir::LightType::Point};
};

#endif // HD_RESTIR_LIGHT_H
