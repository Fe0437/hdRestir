#pragma once

#include "material.h"
#include "pxr/base/gf/vec3f.h"

#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Combined diffuse+specular BSDF (Schlick GGX specular + Lambert diffuse) used for direct lighting.
    // Stateful: bound to a BSDFClosure at construction time.
    // Static overloads are also provided for callers that already hold a closure and prefer not to construct an
    // instance.
    struct GGXBsdf final : public IBSDF
    {
        explicit GGXBsdf(BSDFClosure &&c) noexcept : _c{std::move(c)} {}

        // IBSDF interface — uses the stored closure.
        [[nodiscard]] GfVec3f Eval(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                   const GfVec3f &wi) const noexcept override;

        [[nodiscard]] float Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                const GfVec3f &wi) const noexcept override;

        // Static overloads — closure passed explicitly, no instance needed.
        [[nodiscard]] static GfVec3f Eval(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi,
                                          const BSDFClosure &c) noexcept;

        [[nodiscard]] static float Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi,
                                       const BSDFClosure &c) noexcept;

      private:
        BSDFClosure _c;
    };

} // namespace Restir
