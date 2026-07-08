#pragma once

// Inline math helpers shared between renderer.cpp and the shading layer.
// All functions are pure; they have no external state.

#include "materials/material.h"
#include "pxr/base/gf/vec3f.h"
#include "spectrum.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

inline float RandomFloat(uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state) / static_cast<float>(0xFFFFFFFFu);
}

inline GfVec3f SampleCosineHemisphere(float u1, float u2)
{
    float r     = std::sqrt(u1);
    float theta = 2.0f * static_cast<float>(M_PI) * u2;
    return {r * std::cos(theta), std::sqrt(1.0f - u1), r * std::sin(theta)};
}

inline GfVec3f SampleGGX(float u1, float u2, float roughness)
{
    float alpha    = std::max(0.001f, roughness * roughness);
    float alpha2   = alpha * alpha;
    float phi      = 2.0f * static_cast<float>(M_PI) * u1;
    float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha2 - 1.0f) * u2));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return {sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
}

inline GfVec3f AlignToNormal(const GfVec3f &sample, const GfVec3f &normal)
{
    GfVec3f up        = std::abs(normal[1]) < 0.999f ? GfVec3f(0, 1, 0) : GfVec3f(1, 0, 0);
    GfVec3f tangent   = GfCross(up, normal).GetNormalized();
    GfVec3f bitangent = GfCross(normal, tangent);
    return sample[0] * tangent + sample[1] * normal + sample[2] * bitangent;
}

inline float FresnelDielectric(float cosThetaI, float ior)
{
    cosThetaI  = std::clamp(cosThetaI, -1.0f, 1.0f);
    float etaI = 1.0f, etaT = ior;
    if (cosThetaI > 0)
        std::swap(etaI, etaT);
    float sinThetaT = etaI / etaT * std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
    if (sinThetaT >= 1.0f)
        return 1.0f;
    float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));
    cosThetaI       = std::abs(cosThetaI);
    float rParl     = ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float rPerp     = ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (rParl * rParl + rPerp * rPerp) / 2.0f;
}

// GGX NDF pdf over half-vectors: D(h)*dot(h,n). To get pdf over wi: divide by (4*dot(wi,h)).
inline float GGXPdf(const GfVec3f &h, const GfVec3f &n, float alpha)
{
    float cosH  = std::max(0.f, GfDot(h, n));
    float a2    = alpha * alpha;
    float denom = cosH * cosH * (a2 - 1.f) + 1.f;
    return a2 / ((float)M_PI * denom * denom) * cosH;
}

// Smith-GGX masking-shadowing importance-sampling weight for a half-vector-sampled reflection:
// bsdf*cosThetaL/pdf(wi) once D cancels out. Clamped to bound the grazing-angle spike.
inline float GGXReflectionWeight(float nDotV, float nDotL, float vDotH, float nDotH, float alpha)
{
    constexpr float kMinCos    = 0.001f;
    constexpr float kMaxWeight = 10.0f;
    nDotV                      = std::max(kMinCos, nDotV);
    nDotL                      = std::max(kMinCos, nDotL);
    vDotH                      = std::max(kMinCos, vDotH);
    nDotH                      = std::max(kMinCos, nDotH);
    alpha                      = std::max(kMinCos, alpha);

    const float alpha2{alpha * alpha};
    const float g1V{2.0f * nDotV / (nDotV + std::sqrt(alpha2 + (1.0f - alpha2) * nDotV * nDotV))};
    const float g1L{2.0f * nDotL / (nDotL + std::sqrt(alpha2 + (1.0f - alpha2) * nDotL * nDotL))};
    const float weight{(g1V * g1L * vDotH) / (nDotV * nDotH)};
    return std::min(weight, kMaxWeight);
}

inline float PowerHeuristic(float f, float g)
{
    float f2 = f * f;
    float g2 = g * g;
    return f2 / (f2 + g2);
}

namespace Detail
{
    inline float PowerHeuristicDenominator()
    {
        return 0.0f;
    }

    template <typename... Rest> inline float PowerHeuristicDenominator(float p, int n, Rest... rest)
    {
        static_assert(sizeof...(rest) % 2 == 0, "PowerHeuristic expects (pdf, sampleCount) pairs");

        const float p2{p * p};
        return static_cast<float>(n) * p2 + PowerHeuristicDenominator(rest...);
    }
} // namespace Detail

// Resampling MIS weight for RIS with any number of techniques passed as (pdf, sampleCount) pairs.
// Satisfies Σ M_k * m_k = 1, so W_X = (1/p̂) * Σ w_i is unbiased without an extra 1/M.
// For equal nf=ng=N: equals PowerHeuristic(f,g)/N.
template <typename... Rest> inline float PowerHeuristic(float f, int nf, float g, int ng, Rest... rest)
{
    static_assert(sizeof...(rest) % 2 == 0, "PowerHeuristic expects (pdf, sampleCount) pairs");

    const float f2{f * f};
    const float g2{g * g};
    const float denom{static_cast<float>(nf) * f2 + static_cast<float>(ng) * g2 +
                      Detail::PowerHeuristicDenominator(rest...)};
    return denom > 0.0f ? f2 / denom : 0.0f;
}

// Beer–Lambert volumetric absorption for transmissive materials.
inline void BeerAbsorption(SampledSpectrum &throughput, const Restir::BSDFClosure &c, float t, bool isInside,
                           const SampledWavelengths &lambda)
{
    if (isInside && c.TransmissionDepth > 0.0f && !c.ThinWalled)
    {
        const SampledSpectrum transSpec = RGBToSpectrum(c.TransmissionColor, lambda);
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i)
        {
            const float sigma_a = -std::log(std::max(transSpec[i], 1e-4f)) / c.TransmissionDepth;
            throughput[i] *= std::exp(-sigma_a * t);
        }
    }
}
