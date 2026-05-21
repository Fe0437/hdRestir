#include "preview_surface.h"
#include "ggx.h"
#include "shading_helpers.h"

#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#  define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Duff et al. 2017 ONB (same construction as ggx_brdf.cpp)
struct ONB {
    GfVec3f n, t, b;

    [[nodiscard]] static ONB fromNormal(const GfVec3f& normal) noexcept {
        const float sign { (normal[2] >= 0.0f) ? 1.0f : -1.0f };
        const float a    { -1.0f / (sign + normal[2]) };
        const float bb   { normal[0] * normal[1] * a };
        return {
            normal,
            {1.0f + sign * normal[0] * normal[0] * a, sign * bb, -sign * normal[0]},
            {bb, sign + normal[1] * normal[1] * a, -normal[1]}
        };
    }

    [[nodiscard]] GfVec3f toLocal(const GfVec3f& v) const noexcept {
        return {GfDot(v, t), GfDot(v, b), GfDot(v, n)};
    }

    [[nodiscard]] GfVec3f toWorld(const GfVec3f& v) const noexcept {
        return v[0] * t + v[1] * b + v[2] * n;
    }
};

// Cosine-weighted hemisphere sample; returns direction in local frame (z = cosTheta)
[[nodiscard]] GfVec3f sampleCosineHemisphere(float u1, float u2) noexcept {
    const float sinTheta { std::sqrt(u1) };
    const float phi      { 2.0f * kPi * u2 };
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1))};
}

}  // namespace

BSDFClosure PreviewSurfaceMaterial::GetClosure(const HitRecord& hit) const {
    GfVec3f baseColor = _params.DiffuseColor;
    float   metallic  = _params.Metallic;
    float   roughness = std::max(0.001f, _params.Roughness);
    GfVec3f smoothNormal = hit.SmoothNormal;

    // Apply texture overrides
    if (!_params.DiffuseTexture.GetAssetPath().empty()) {
        baseColor = GfCompMult(baseColor,
            _texFact->Create<GfVec3f, false>(_params.DiffuseTexture.GetResolvedPath()).Sample(hit.Uv));
    }
    if (!_params.MetallicTexture.GetAssetPath().empty()) {
        metallic = _texFact->Create<float, true>(_params.MetallicTexture.GetResolvedPath()).Sample(hit.Uv);
    }
    if (!_params.RoughnessTexture.GetAssetPath().empty()) {
        roughness = _texFact->Create<float, true>(_params.RoughnessTexture.GetResolvedPath()).Sample(hit.Uv);
    }
    if (!_params.NormalTexture.GetAssetPath().empty()) {
        GfVec3f nTex = _texFact->Create<GfVec3f, true>(_params.NormalTexture.GetResolvedPath()).Sample(hit.Uv);
        nTex = nTex * 2.0f - GfVec3f(1.0f);

        GfVec3f n = hit.SmoothNormal;
        GfVec3f t = hit.Dpdu;
        GfVec3f b = hit.Dpdv;

        t = (t - n * GfDot(t, n)).GetNormalized();
        b = (b - n * GfDot(b, n) - t * GfDot(b, t)).GetNormalized();

        if (GfDot(GfCross(n, t), b) < 0.0f) b = -b;

        smoothNormal = (t * nTex[0] + b * nTex[1] + n * nTex[2]).GetNormalized();
    }

    return BSDFClosure{
        .BaseColor         = baseColor,
        .Emission          = _params.EmissionColor * _params.Emission,
        .Normal            = smoothNormal,
        .SpecularColor     = _params.SpecularColor,
        .CoatColor         = _params.CoatColor,
        .SheenColor        = _params.SheenColor,
        .SubsurfaceColor   = _params.SubsurfaceColor,
        .TransmissionColor = _params.TransmissionColor,
        .Metallic          = metallic,
        .Roughness         = roughness,
        .Ior               = _params.Ior,
        .Specular          = _params.Specular,
        .Transmission      = _params.Transmission,
        .TransmissionDepth = _params.TransmissionDepth,
        .Coat              = _params.Coat,
        .CoatRoughness     = _params.CoatRoughness,
        .CoatIor           = _params.CoatIor,
        .Sheen             = _params.Sheen,
        .SheenRoughness    = _params.SheenRoughness,
        .Subsurface        = _params.Subsurface,
        .Opacity           = _params.Opacity,
        .ThinWalled        = _params.ThinWalled,
    };
}

BounceSample PreviewSurfaceMaterial::SampleBounce(
    const ShadingPoint& surface, const BounceConfig& config, BounceState& state, Rng& rng) const
{
    const BSDFClosure&        c            {surface.c};
    const GfVec3f&            shadingNormal{surface.shadingNormal};
    const GfVec3f&            currentRayDir{surface.rayDir};
    const GfVec3f&            hitPos       {surface.hit.Position};
    const SampledWavelengths& lambda       {surface.lambda};
    const bool                isInside     {surface.isInside};

    // --- Coat Layer ---
    if (c.Coat > 0.0f && !isInside) {
        float coatFresnel = c.Coat * FresnelDielectric(GfDot(currentRayDir, shadingNormal), c.CoatIor);
        if (rng.NextFloat() < coatFresnel) {
            if (state.reflectionBounces >= config.effectiveMaxRefl)
                return {.Terminate = true};
            ++state.reflectionBounces;
            GfVec3f reflectDir;
            if (c.CoatRoughness > 0.0f) {
                GfVec3f h = AlignToNormal(SampleGGX(rng.NextFloat(), rng.NextFloat(), c.CoatRoughness), shadingNormal);
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
                if (GfDot(reflectDir, shadingNormal) < 0)
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
            } else {
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
            }
            return {
                .NextRay       = {hitPos + shadingNormal * 1e-4f, reflectDir},
                .ThroughputMul = RGBToSpectrum(c.CoatColor, lambda),
                .SkipRoulette  = true,
            };
        }
    }

    // --- Sheen Layer ---
    if (c.Sheen > 0.0f && !isInside) {
        float cosTheta = std::max(0.0f, GfDot(-currentRayDir, shadingNormal));
        float sheenFresnel = c.Sheen * std::pow(1.0f - cosTheta, 5.0f);
        if (rng.NextFloat() < sheenFresnel) {
            if (state.reflectionBounces >= config.effectiveMaxRefl)
                return {.Terminate = true};
            ++state.reflectionBounces;
            GfVec3f reflectDir;
            if (c.SheenRoughness > 0.0f) {
                GfVec3f h = AlignToNormal(SampleGGX(rng.NextFloat(), rng.NextFloat(), c.SheenRoughness), shadingNormal);
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
                if (GfDot(reflectDir, shadingNormal) < 0)
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
            } else {
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
            }
            return {
                .NextRay       = {hitPos + shadingNormal * 1e-4f, reflectDir},
                .ThroughputMul = RGBToSpectrum(c.SheenColor, lambda),
                .SkipRoulette  = true,
            };
        }
    }

    // Apply dispersion (local ior — does not modify the closure)
    float localIor = c.Ior;
    {
        float C = 10000.0f;
        float B = localIor - C / (589.3f * 589.3f);
        localIor = B + C / (lambda.lambda[0] * lambda.lambda[0]);
    }

    // --- BSDF branch selection ---
    float fresnel    = FresnelDielectric(GfDot(currentRayDir, shadingNormal), localIor);
    float reflectProb = fresnel * c.Specular;
    if (c.Metallic > 0.0f) reflectProb = std::max(reflectProb, c.Metallic);

    float randVal = rng.NextFloat();

    if (randVal < reflectProb) {
        // --- Reflection ---
        if (state.reflectionBounces >= config.effectiveMaxRefl)
            return {.Terminate = true};
        ++state.reflectionBounces;
        GfVec3f reflectDir;
        if (c.Roughness > 0.0f) {
            GfVec3f h = AlignToNormal(SampleGGX(rng.NextFloat(), rng.NextFloat(), c.Roughness), shadingNormal);
            reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
            if (GfDot(reflectDir, shadingNormal) < 0)
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
        } else {
            reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
        }
        GfVec3f reflTint = c.SpecularColor * (1.0f - c.Metallic) + c.BaseColor * c.Metallic;
        return {
            .NextRay       = {hitPos + shadingNormal * 1e-4f, reflectDir},
            .ThroughputMul = RGBToSpectrum(reflTint, lambda),
        };
    } else {
        float remainingProb = (randVal - reflectProb) / (1.0f - reflectProb);
        if (c.Transmission > 1e-6f && remainingProb < c.Transmission) {
            // --- Refraction ---
            float etaI = 1.0f, etaT = localIor;
            if (isInside) std::swap(etaI, etaT);

            float eta       = etaI / etaT;
            GfVec3f n       = shadingNormal;
            float cosThetaI = GfDot(currentRayDir, n);  // < 0
            float k         = 1.0f - eta * eta * (1.0f - cosThetaI * cosThetaI);

            if (k >= 0) {
                if (state.refractionBounces >= config.effectiveMaxRefr)
                    return {.Terminate = true};
                ++state.refractionBounces;
                GfVec3f refractDir = (eta * currentRayDir - (eta * cosThetaI + std::sqrt(k)) * n).GetNormalized();

                if (c.Roughness > 0.0f) {
                    GfVec3f h          = AlignToNormal(SampleGGX(rng.NextFloat(), rng.NextFloat(), c.Roughness), n);
                    float cosThetaI_h  = GfDot(currentRayDir, h);
                    float k_h          = 1.0f - eta * eta * (1.0f - cosThetaI_h * cosThetaI_h);
                    if (k_h >= 0.0f)
                        refractDir = (eta * currentRayDir - (eta * cosThetaI_h + std::sqrt(k_h)) * h).GetNormalized();
                    else
                        refractDir = (currentRayDir - 2.0f * cosThetaI_h * h).GetNormalized();
                }

                return {
                    .NextRay       = {hitPos - n * 1e-4f, refractDir},
                    .ThroughputMul = RGBToSpectrum(c.TransmissionColor, lambda),
                };
            } else {
                // Total Internal Reflection
                if (state.reflectionBounces >= config.effectiveMaxRefl)
                    return {.Terminate = true};
                ++state.reflectionBounces;
                GfVec3f reflectDir = (currentRayDir - 2.0f * cosThetaI * shadingNormal).GetNormalized();
                return {.NextRay = {hitPos + shadingNormal * 1e-4f, reflectDir}};
            }
        } else {
            // --- Diffuse ---
            GfVec3f diffuseDir = AlignToNormal(SampleCosineHemisphere(rng.NextFloat(), rng.NextFloat()), shadingNormal);
            float nDotL = std::max(0.0f, GfDot(shadingNormal, diffuseDir));
            float pdf   = nDotL / (float)M_PI;
            if (pdf < 1e-6f)
                return {.Terminate = true};

            GfVec3f finalDiffuse = c.BaseColor * (1.0f - c.Subsurface) + c.SubsurfaceColor * c.Subsurface;
            return {
                .NextRay       = {hitPos + shadingNormal * 1e-4f, diffuseDir},
                .ThroughputMul = RGBToSpectrum(finalDiffuse, lambda),
            };
        }
    }
}

}  // namespace Restir
