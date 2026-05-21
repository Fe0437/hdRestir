#include "denoiser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iostream>
#include <vector>

#ifdef HdRestir_HAS_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

std::vector<float>
Denoiser::_packRgb(gsl::span<const pxr::GfVec4f> framebuffer)
{
    std::vector<float> color(framebuffer.size() * 3, 0.0f);
    for (std::size_t i{0}; i < framebuffer.size(); ++i) {
        color[i * 3] = framebuffer[i][0];
        color[i * 3 + 1] = framebuffer[i][1];
        color[i * 3 + 2] = framebuffer[i][2];
    }
    return color;
}

void
Denoiser::_writeRgb(gsl::span<pxr::GfVec4f> framebuffer, gsl::span<const float> color)
{
    for (std::size_t i{0}; i < framebuffer.size(); ++i) {
        framebuffer[i] = GfVec4f{color[i * 3], color[i * 3 + 1], color[i * 3 + 2], framebuffer[i][3]};
    }
}

namespace {

gsl::span<const float> ToOidnGuidePixels(gsl::span<const pxr::GfVec3f> pixels)
{
    return {
        reinterpret_cast<const float*>(pixels.data()),
        pixels.size() * 3
    };
}

}  // namespace

std::string
Denoiser::_toOidnImageName(std::string_view name)
{
    std::string imageName{name};
    std::transform(imageName.begin(), imageName.end(), imageName.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return imageName;
}

void
Denoiser::_applyChromaticityBlur(
    std::vector<float>& prefiltered,
    gsl::span<const float> color,
    unsigned int width,
    unsigned int height)
{
    for (int y{0}; y < static_cast<int>(height); ++y) {
        for (int x{0}; x < static_cast<int>(width); ++x) {
            const std::size_t idx{(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 3};
            const float r{color[idx]};
            const float g{color[idx + 1]};
            const float b{color[idx + 2]};

            const float Y{0.25f * r + 0.5f * g + 0.25f * b};

            float sumCo{0.0f};
            float sumCg{0.0f};
            float weightSum{0.0f};
            for (int dy{-1}; dy <= 1; ++dy) {
                for (int dx{-1}; dx <= 1; ++dx) {
                    const int nx{std::clamp(x + dx, 0, static_cast<int>(width) - 1)};
                    const int ny{std::clamp(y + dy, 0, static_cast<int>(height) - 1)};
                    const std::size_t nIdx{(static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)) * 3};
                    const float nr{color[nIdx]};
                    const float ng{color[nIdx + 1]};
                    const float nb{color[nIdx + 2]};
                    sumCo += 0.50f * nr - 0.5f * nb;
                    sumCg += -0.25f * nr + 0.5f * ng - 0.25f * nb;
                    weightSum += 1.0f;
                }
            }
            const float Co{sumCo / weightSum};
            const float Cg{sumCg / weightSum};

            prefiltered[idx] = std::max(0.0f, Y + Co - Cg);
            prefiltered[idx + 1] = std::max(0.0f, Y + Cg);
            prefiltered[idx + 2] = std::max(0.0f, Y - Co - Cg);
        }
    }
}

void
Denoiser::_runOidnDenoiser(
    std::vector<float>& output,
    gsl::span<const float> prefiltered,
    gsl::span<const GuideBufferView> guideBuffers,
    unsigned int width,
    unsigned int height,
    int frameCount,
    const Config& config)
{

#ifdef HdRestir_HAS_OIDN
    std::cout << "[Restir] Running OIDN Denoiser on frame " << frameCount << "..." << std::endl;
    if (!config.EnableDenoiser) {
        return;
    }

    try {
        oidn::DeviceRef device{oidn::newDevice(oidn::DeviceType::CPU)};
        device.commit();

        oidn::FilterRef filter{device.newFilter("RT")};
        filter.setImage("color", prefiltered.data(), oidn::Format::Float3, width, height);

        for (const GuideBufferView& guideBuffer : guideBuffers) {
            if (guideBuffer.Pixels.empty()) {
                continue;
            }

            const std::string imageName{_toOidnImageName(guideBuffer.Name)};
            const auto pixels{ToOidnGuidePixels(guideBuffer.Pixels)};
            filter.setImage(imageName.c_str(), pixels.data(), oidn::Format::Float3, width, height);
        }

        filter.setImage("output", output.data(), oidn::Format::Float3, width, height);
        filter.set("hdr", true);
        filter.commit();
        filter.execute();

        const char* errorMessage{nullptr};
        if (device.getError(errorMessage) != oidn::Error::None) {
            std::cerr << "[Restir] OIDN Error: " << errorMessage << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Restir] OIDN Exception: " << e.what() << std::endl;
    }
#else
    (void)output;
    (void)prefiltered;
    (void)guideBuffers;
    (void)width;
    (void)height;
    (void)frameCount;
    (void)config;
#endif
}

void
Denoiser::_applyDenoiserAlgorithm(
    std::vector<float>& color,
    gsl::span<const GuideBufferView> guideBuffers,
    unsigned int width,
    unsigned int height,
    int frameCount,
    const Config& config)
{
    std::vector<float> prefiltered(width * height * 3);
    std::vector<float> clampedColor{color};

    if (config.EnableChromaticityBlur) {
        _applyChromaticityBlur(prefiltered, clampedColor, width, height);
    } else {
        prefiltered = clampedColor;
    }

    std::vector<float> output{prefiltered};

#ifdef HdRestir_HAS_OIDN
    _runOidnDenoiser(output, prefiltered, guideBuffers, width, height, frameCount, config);
#endif

    color = std::move(output);
}

void
Denoiser::Run(
    gsl::span<pxr::GfVec4f> framebuffer,
    gsl::span<const GuideBufferView> guideBuffers,
    int width,
    int height,
    int frameCount,
    const Config& config)
{
    if (width == 0 || height == 0) {
        return;
    }

    std::vector<float> color{_packRgb(gsl::span<const pxr::GfVec4f>{framebuffer.data(), framebuffer.size()})};

    _applyDenoiserAlgorithm(
        color,
        guideBuffers,
        static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
        frameCount,
        config);

    _writeRgb(framebuffer, color);
}

void
Denoiser::Run(
    gsl::span<pxr::GfVec4f> framebuffer,
    int width,
    int height,
    const Config& config)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    Run(framebuffer, gsl::span<const GuideBufferView>{}, width, height, 0, config);
}