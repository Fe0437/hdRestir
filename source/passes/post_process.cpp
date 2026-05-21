#include "post_process.h"

#include "pxr/base/gf/vec3i.h"

#include <algorithm>
#include <cmath>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

std::vector<float>
PostProcess::_packRgb(gsl::span<const GfVec4f> framebuffer)
{
    std::vector<float> color(framebuffer.size() * 3);
    for (std::size_t i = 0; i < framebuffer.size(); ++i) {
        color[i * 3] = framebuffer[i][0];
        color[i * 3 + 1] = framebuffer[i][1];
        color[i * 3 + 2] = framebuffer[i][2];
    }
    return color;
}

void
PostProcess::_applyLensFlare(
    std::vector<float>& finalColor,
    gsl::span<const float> color,
    unsigned int width,
    unsigned int height)
{
    std::vector<float> bloom(width * height * 3, 0.0f);
    constexpr float threshold{2.0f};

    for (unsigned int i = 0; i < width * height; ++i) {
        const float r{color[i * 3]};
        const float g{color[i * 3 + 1]};
        const float b{color[i * 3 + 2]};
        const float lum{0.2126f * r + 0.7152f * g + 0.0722f * b};
        if (lum > threshold) {
            bloom[i * 3] = r - threshold;
            bloom[i * 3 + 1] = g - threshold;
            bloom[i * 3 + 2] = b - threshold;
        }
    }

    const int blurSize{std::max(1, static_cast<int>(std::min(width, height) / 20))};
    std::vector<float> blurred(width * height * 3, 0.0f);

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 3;
            if (bloom[idx] == 0.0f && bloom[idx + 1] == 0.0f && bloom[idx + 2] == 0.0f) {
                continue;
            }

            for (int d = -blurSize; d <= blurSize; d += 2) {
                if (d == 0) {
                    continue;
                }
                const float weight{1.0f / (std::abs(d) + 1.0f)};

                if (static_cast<int>(x) + d >= 0 && static_cast<int>(x) + d < static_cast<int>(width)) {
                    const std::size_t bIdx = (static_cast<std::size_t>(y) * width + static_cast<unsigned int>(static_cast<int>(x) + d)) * 3;
                    blurred[bIdx] += bloom[idx] * weight;
                    blurred[bIdx + 1] += bloom[idx + 1] * weight;
                    blurred[bIdx + 2] += bloom[idx + 2] * weight;
                }
                if (static_cast<int>(y) + d >= 0 && static_cast<int>(y) + d < static_cast<int>(height)) {
                    const std::size_t bIdx = (static_cast<std::size_t>(static_cast<int>(y) + d) * width + x) * 3;
                    blurred[bIdx] += bloom[idx] * weight;
                    blurred[bIdx + 1] += bloom[idx + 1] * weight;
                    blurred[bIdx + 2] += bloom[idx + 2] * weight;
                }
                if (static_cast<int>(x) + d >= 0 && static_cast<int>(x) + d < static_cast<int>(width) &&
                    static_cast<int>(y) + d >= 0 && static_cast<int>(y) + d < static_cast<int>(height)) {
                    const std::size_t bIdx = (static_cast<std::size_t>(static_cast<int>(y) + d) * width + static_cast<unsigned int>(static_cast<int>(x) + d)) * 3;
                    blurred[bIdx] += bloom[idx] * weight * 0.5f;
                    blurred[bIdx + 1] += bloom[idx + 1] * weight * 0.5f;
                    blurred[bIdx + 2] += bloom[idx + 2] * weight * 0.5f;
                }
                if (static_cast<int>(x) + d >= 0 && static_cast<int>(x) + d < static_cast<int>(width) &&
                    static_cast<int>(y) - d >= 0 && static_cast<int>(y) - d < static_cast<int>(height)) {
                    const std::size_t bIdx = (static_cast<std::size_t>(static_cast<int>(y) - d) * width + static_cast<unsigned int>(static_cast<int>(x) + d)) * 3;
                    blurred[bIdx] += bloom[idx] * weight * 0.5f;
                    blurred[bIdx + 1] += bloom[idx + 1] * weight * 0.5f;
                    blurred[bIdx + 2] += bloom[idx + 2] * weight * 0.5f;
                }
            }
        }
    }

    for (unsigned int i = 0; i < width * height; ++i) {
        finalColor[i * 3] += blurred[i * 3] * 0.1f;
        finalColor[i * 3 + 1] += blurred[i * 3 + 1] * 0.1f;
        finalColor[i * 3 + 2] += blurred[i * 3 + 2] * 0.1f;
    }
}

void
PostProcess::_applyChromaticAberration(
    std::vector<float>& finalColor,
    unsigned int width,
    unsigned int height,
    float chromaticAberration)
{
    std::vector<float> caColor{finalColor};
    const float maxDist{std::sqrt(static_cast<float>(width * width + height * height)) * 0.5f};
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            const float dx{static_cast<float>(x) - width * 0.5f};
            const float dy{static_cast<float>(y) - height * 0.5f};
            const float dist{std::sqrt(dx * dx + dy * dy)};
            const float dirX{dist > 0.0f ? dx / dist : 0.0f};
            const float dirY{dist > 0.0f ? dy / dist : 0.0f};

            const float shift{chromaticAberration * (dist / maxDist)};

            const int rx{std::clamp(static_cast<int>(x - dirX * shift), 0, static_cast<int>(width) - 1)};
            const int ry{std::clamp(static_cast<int>(y - dirY * shift), 0, static_cast<int>(height) - 1)};
            const int bx{std::clamp(static_cast<int>(x + dirX * shift), 0, static_cast<int>(width) - 1)};
            const int by{std::clamp(static_cast<int>(y + dirY * shift), 0, static_cast<int>(height) - 1)};

            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 3;
            const std::size_t rIdx = (static_cast<std::size_t>(ry) * width + static_cast<unsigned int>(rx)) * 3;
            const std::size_t bIdx = (static_cast<std::size_t>(by) * width + static_cast<unsigned int>(bx)) * 3;

            caColor[idx] = finalColor[rIdx];
            caColor[idx + 2] = finalColor[bIdx + 2];
        }
    }
    finalColor = std::move(caColor);
}

void
PostProcess::_applyPostProcessAlgorithm(
    std::vector<float>& color,
    unsigned int width,
    unsigned int height,
    const Config& config)
{
    std::vector<float> finalColor{color};

    if (config.EnableLensFlare) {
        _applyLensFlare(finalColor, color, width, height);
    }

    if (config.ChromaticAberration > 0.0f) {
        _applyChromaticAberration(finalColor, width, height, config.ChromaticAberration);
    }

    color = std::move(finalColor);
}

void
PostProcess::Run(
    Restir::IFrameBufferTarget& colorBuffer,
    unsigned int width,
    unsigned int height,
    const Config& config)
{
    if (width == 0 || height == 0) {
        return;
    }

    std::vector<float> color(width * height * 3, 0.0f);
    const float* mappedColor{static_cast<const float*>(colorBuffer.Map())};
    if (mappedColor != nullptr) {
        for (unsigned int i = 0; i < width * height; ++i) {
            color[i * 3 + 0] = mappedColor[i * 4 + 0];
            color[i * 3 + 1] = mappedColor[i * 4 + 1];
            color[i * 3 + 2] = mappedColor[i * 4 + 2];
        }
    }
    colorBuffer.Unmap();

    _applyPostProcessAlgorithm(color, width, height, config);

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 3;
            const float pixel[4]{color[idx], color[idx + 1], color[idx + 2], 1.0f};
            colorBuffer.Write(GfVec3i(x, y, 0), 4, pixel);
        }
    }
    colorBuffer.Resolve();
}

void
PostProcess::Run(
    gsl::span<GfVec4f> framebuffer,
    int width,
    int height,
    const Config& config)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    auto color{_packRgb(gsl::span<const GfVec4f>{framebuffer.data(), framebuffer.size()})};
    _applyPostProcessAlgorithm(color, static_cast<unsigned int>(width), static_cast<unsigned int>(height), config);

    for (std::size_t i = 0; i < framebuffer.size(); ++i) {
        framebuffer[i] = GfVec4f{color[i * 3], color[i * 3 + 1], color[i * 3 + 2], framebuffer[i][3]};
    }
}