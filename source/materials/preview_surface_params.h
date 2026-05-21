#pragma once

#include "pxr/pxr.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

struct PreviewSurfaceParams {
    GfVec3f DiffuseColor{1.0f};
    float Metallic{0.0f};
    float Roughness{0.5f};
    GfVec3f SpecularColor{1.0f};
    float Specular{1.0f};
    float Opacity{1.0f};
    float Ior{1.5f};

    float Transmission{0.0f};
    GfVec3f TransmissionColor{1.0f};
    float TransmissionDepth{0.0f};
    GfVec3f TransmissionScatter{0.0f};

    GfVec3f EmissionColor{1.0f};
    float Emission{0.0f};

    float Coat{0.0f};
    GfVec3f CoatColor{1.0f};
    float CoatRoughness{0.1f};
    float CoatIor{1.5f};

    float Sheen{0.0f};
    GfVec3f SheenColor{1.0f};
    float SheenRoughness{0.3f};

    float Subsurface{0.0f};
    GfVec3f SubsurfaceColor{1.0f};
    GfVec3f SubsurfaceRadius{1.0f};
    float SubsurfaceScale{1.0f};
    float SubsurfaceAnisotropy{0.0f};

    bool ThinWalled{false};
    float DiffuseRoughness{0.0f};

    SdfAssetPath DiffuseTexture{};
    SdfAssetPath NormalTexture{};
    SdfAssetPath MetallicTexture{};
    SdfAssetPath RoughnessTexture{};
};

}  // namespace Restir