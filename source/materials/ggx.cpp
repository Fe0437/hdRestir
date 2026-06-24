#include "ggx.h"

#include "shading_helpers.h"

#include <algorithm>
#include <cmath>

namespace Restir
{

    namespace
    {

        constexpr float kPi = 3.14159265358979323846f;

    } // namespace

    // ---- GGXBsdf -------------------------------------------------------------------

    GfVec3f GGXBsdf::Eval(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi) const noexcept
    {
        const GfVec3f h{(wo + wi).GetNormalized()};
        const float   nDotL{std::max(0.001f, GfDot(shadingNormal, wi))};
        const float   nDotV{std::max(0.001f, GfDot(shadingNormal, wo))};
        const float   nDotH{std::max(0.001f, GfDot(shadingNormal, h))};
        const float   lDotH{std::max(0.001f, GfDot(wi, h))};

        const float alpha{std::max(0.001f, _c.Roughness * _c.Roughness)};
        const float alpha2{alpha * alpha};
        const float denom{nDotH * nDotH * (alpha2 - 1.0f) + 1.0f};
        const float Dval{alpha2 / (kPi * denom * denom)};
        const float k_g{alpha / 2.0f};
        const float G_l{nDotL / (nDotL * (1.0f - k_g) + k_g)};
        const float G_v{nDotV / (nDotV * (1.0f - k_g) + k_g)};
        const float Gval{G_l * G_v};

        const GfVec3f F0{_c.SpecularColor * (1.0f - _c.Metallic) * 0.04f + _c.BaseColor * _c.Metallic};
        const GfVec3f Fval{F0 + (GfVec3f{1.0f, 1.0f, 1.0f} - F0) * std::pow(1.0f - lDotH, 5.0f)};
        const GfVec3f specBsdf{(Fval * Dval * Gval) / (4.0f * nDotL * nDotV)};

        const GfVec3f finalDiffuse{_c.BaseColor * (1.0f - _c.Subsurface) + _c.SubsurfaceColor * _c.Subsurface};
        const GfVec3f diffBsdf{finalDiffuse * (1.0f - _c.Metallic) * (1.0f - _c.Transmission) / kPi};

        return diffBsdf + specBsdf;
    }

    float GGXBsdf::Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi) const noexcept
    {
        const GfVec3f h{(wo + wi).GetNormalized()};
        const float   nDotL{std::max(0.001f, GfDot(shadingNormal, wi))};
        const float   nDotV{std::max(0.001f, GfDot(shadingNormal, wo))};
        const float   nDotH{std::max(0.001f, GfDot(shadingNormal, h))};
        const float   lDotH{std::max(0.001f, GfDot(wi, h))};

        const float alpha{std::max(0.001f, _c.Roughness * _c.Roughness)};
        const float alpha2{alpha * alpha};
        const float denom{nDotH * nDotH * (alpha2 - 1.0f) + 1.0f};
        const float Dval{alpha2 / (kPi * denom * denom)};

        float reflectProb{FresnelDielectric(nDotV, _c.Ior) * _c.Specular};
        if (_c.Metallic > 0.0f)
            reflectProb = std::max(reflectProb, _c.Metallic);

        const float diffPdf{nDotL / kPi};
        const float specPdf{(Dval * nDotH) / (4.0f * lDotH)};
        return reflectProb * specPdf + (1.0f - reflectProb) * (1.0f - _c.Transmission) * diffPdf;
    }

    GfVec3f GGXBsdf::Eval(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi,
                          const BSDFClosure &c) noexcept
    {
        return GGXBsdf{BSDFClosure{c}}.Eval(shadingNormal, wo, wi);
    }

    float GGXBsdf::Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo, const GfVec3f &wi,
                       const BSDFClosure &c) noexcept
    {
        return GGXBsdf{BSDFClosure{c}}.Pdf(shadingNormal, wo, wi);
    }

} // namespace Restir
