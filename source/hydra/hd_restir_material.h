#ifndef HD_RESTIR_MATERIAL_H
#define HD_RESTIR_MATERIAL_H

#include "preview_surface_params.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirMaterial final : public HdMaterial
{
  public:
    HdRestirMaterial(SdfPath const &id);
    ~HdRestirMaterial() override;

    void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const Restir::PreviewSurfaceParams &GetParams() const
    {
        return _params;
    }
    Restir::PreviewSurfaceParams &GetParams()
    {
        return _params;
    }

    const GfVec3f &GetDiffuseColor() const
    {
        return _params.DiffuseColor;
    }
    void SetDiffuseColor(const GfVec3f &v)
    {
        _params.DiffuseColor = v;
    }

    float GetMetallic() const
    {
        return _params.Metallic;
    }
    void SetMetallic(float v)
    {
        _params.Metallic = v;
    }

    float GetRoughness() const
    {
        return _params.Roughness;
    }
    void SetRoughness(float v)
    {
        _params.Roughness = v;
    }

    const GfVec3f &GetSpecularColor() const
    {
        return _params.SpecularColor;
    }
    void SetSpecularColor(const GfVec3f &v)
    {
        _params.SpecularColor = v;
    }

    float GetSpecular() const
    {
        return _params.Specular;
    }
    void SetSpecular(float v)
    {
        _params.Specular = v;
    }

    float GetOpacity() const
    {
        return _params.Opacity;
    }
    void SetOpacity(float v)
    {
        _params.Opacity = v;
    }

    float GetIor() const
    {
        return _params.Ior;
    }
    void SetIor(float v)
    {
        _params.Ior = v;
    }

    float GetTransmission() const
    {
        return _params.Transmission;
    }
    void SetTransmission(float v)
    {
        _params.Transmission = v;
    }

    const GfVec3f &GetTransmissionColor() const
    {
        return _params.TransmissionColor;
    }
    void SetTransmissionColor(const GfVec3f &v)
    {
        _params.TransmissionColor = v;
    }

    const GfVec3f &GetEmissionColor() const
    {
        return _params.EmissionColor;
    }
    void SetEmissionColor(const GfVec3f &v)
    {
        _params.EmissionColor = v;
    }

    float GetEmission() const
    {
        return _params.Emission;
    }
    void SetEmission(float v)
    {
        _params.Emission = v;
    }

    float GetCoat() const
    {
        return _params.Coat;
    }
    void SetCoat(float v)
    {
        _params.Coat = v;
    }

    const GfVec3f &GetCoatColor() const
    {
        return _params.CoatColor;
    }
    void SetCoatColor(const GfVec3f &v)
    {
        _params.CoatColor = v;
    }

    float GetCoatRoughness() const
    {
        return _params.CoatRoughness;
    }
    void SetCoatRoughness(float v)
    {
        _params.CoatRoughness = v;
    }

    float GetCoatIor() const
    {
        return _params.CoatIor;
    }
    void SetCoatIor(float v)
    {
        _params.CoatIor = v;
    }

    float GetTransmissionDepth() const
    {
        return _params.TransmissionDepth;
    }
    void SetTransmissionDepth(float v)
    {
        _params.TransmissionDepth = v;
    }

    const GfVec3f &GetTransmissionScatter() const
    {
        return _params.TransmissionScatter;
    }
    void SetTransmissionScatter(const GfVec3f &v)
    {
        _params.TransmissionScatter = v;
    }

    float GetSheen() const
    {
        return _params.Sheen;
    }
    void SetSheen(float v)
    {
        _params.Sheen = v;
    }

    const GfVec3f &GetSheenColor() const
    {
        return _params.SheenColor;
    }
    void SetSheenColor(const GfVec3f &v)
    {
        _params.SheenColor = v;
    }

    float GetSheenRoughness() const
    {
        return _params.SheenRoughness;
    }
    void SetSheenRoughness(float v)
    {
        _params.SheenRoughness = v;
    }

    float GetSubsurface() const
    {
        return _params.Subsurface;
    }
    void SetSubsurface(float v)
    {
        _params.Subsurface = v;
    }

    const GfVec3f &GetSubsurfaceColor() const
    {
        return _params.SubsurfaceColor;
    }
    void SetSubsurfaceColor(const GfVec3f &v)
    {
        _params.SubsurfaceColor = v;
    }

    const GfVec3f &GetSubsurfaceRadius() const
    {
        return _params.SubsurfaceRadius;
    }
    void SetSubsurfaceRadius(const GfVec3f &v)
    {
        _params.SubsurfaceRadius = v;
    }

    float GetSubsurfaceScale() const
    {
        return _params.SubsurfaceScale;
    }
    void SetSubsurfaceScale(float v)
    {
        _params.SubsurfaceScale = v;
    }

    float GetSubsurfaceAnisotropy() const
    {
        return _params.SubsurfaceAnisotropy;
    }
    void SetSubsurfaceAnisotropy(float v)
    {
        _params.SubsurfaceAnisotropy = v;
    }

    bool GetThinWalled() const
    {
        return _params.ThinWalled;
    }
    void SetThinWalled(bool v)
    {
        _params.ThinWalled = v;
    }

    float GetDiffuseRoughness() const
    {
        return _params.DiffuseRoughness;
    }
    void SetDiffuseRoughness(float v)
    {
        _params.DiffuseRoughness = v;
    }

    const SdfAssetPath &GetDiffuseTexture() const
    {
        return _params.DiffuseTexture;
    }
    void SetDiffuseTexture(const SdfAssetPath &v)
    {
        _params.DiffuseTexture = v;
    }

    const SdfAssetPath &GetNormalTexture() const
    {
        return _params.NormalTexture;
    }
    void SetNormalTexture(const SdfAssetPath &v)
    {
        _params.NormalTexture = v;
    }

    const SdfAssetPath &GetMetallicTexture() const
    {
        return _params.MetallicTexture;
    }
    void SetMetallicTexture(const SdfAssetPath &v)
    {
        _params.MetallicTexture = v;
    }

    const SdfAssetPath &GetRoughnessTexture() const
    {
        return _params.RoughnessTexture;
    }
    void SetRoughnessTexture(const SdfAssetPath &v)
    {
        _params.RoughnessTexture = v;
    }

  private:
    Restir::PreviewSurfaceParams _params{};
};

#endif // HD_RESTIR_MATERIAL_H
