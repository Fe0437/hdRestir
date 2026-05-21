#pragma once

// Inline math helpers shared between renderer.cpp and the shading layer.
// All functions are pure; they have no external state.

#include "materials/material.h"
#include "spectrum.h"

#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#  define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

inline float RandomFloat(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state) / static_cast<float>(0xFFFFFFFFu);
}

inline GfVec3f SampleCosineHemisphere(float u1, float u2) {
    float r     = std::sqrt(u1);
    float theta = 2.0f * static_cast<float>(M_PI) * u2;
    return {r * std::cos(theta), std::sqrt(1.0f - u1), r * std::sin(theta)};
}

inline GfVec3f SampleGGX(float u1, float u2, float roughness) {
    float alpha  = std::max(0.001f, roughness * roughness);
    float alpha2 = alpha * alpha;
    float phi    = 2.0f * static_cast<float>(M_PI) * u1;
    float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha2 - 1.0f) * u2));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return {sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
}

inline GfVec3f AlignToNormal(const GfVec3f& sample, const GfVec3f& normal) {
    GfVec3f up      = std::abs(normal[1]) < 0.999f ? GfVec3f(0, 1, 0) : GfVec3f(1, 0, 0);
    GfVec3f tangent = GfCross(up, normal).GetNormalized();
    GfVec3f bitangent = GfCross(normal, tangent);
    return sample[0] * tangent + sample[1] * normal + sample[2] * bitangent;
}

inline float FresnelDielectric(float cosThetaI, float ior) {
    cosThetaI = std::clamp(cosThetaI, -1.0f, 1.0f);
    float etaI = 1.0f, etaT = ior;
    if (cosThetaI > 0) std::swap(etaI, etaT);
    float sinThetaT = etaI / etaT * std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
    if (sinThetaT >= 1.0f) return 1.0f;
    float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));
    cosThetaI = std::abs(cosThetaI);
    float rParl = ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float rPerp = ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (rParl * rParl + rPerp * rPerp) / 2.0f;
}

inline float PowerHeuristic(float f, float g) {
    float f2 = f * f;
    float g2 = g * g;
    return f2 / (f2 + g2);
}

// Beer–Lambert volumetric absorption for transmissive materials.
inline void BeerAbsorption(SampledSpectrum&           throughput,
                            const Restir::BSDFClosure& c,
                            float                      t,
                            bool                       isInside,
                            const SampledWavelengths&  lambda)
{
    if (isInside && c.TransmissionDepth > 0.0f && !c.ThinWalled) {
        const SampledSpectrum transSpec = RGBToSpectrum(c.TransmissionColor, lambda);
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) {
            const float sigma_a = -std::log(std::max(transSpec[i], 1e-4f)) / c.TransmissionDepth;
            throughput[i] *= std::exp(-sigma_a * t);
        }
    }
}
