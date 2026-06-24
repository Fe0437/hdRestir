#ifndef HD_RESTIR_DENOISER_H
#define HD_RESTIR_DENOISER_H

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include <gsl/gsl>
#include <string_view>
#include <vector>

class Denoiser final
{
  public:
    struct Config
    {
        bool EnableDenoiser{true};
        bool EnableFireflyFilter{true};
        bool EnableChromaticityBlur{true};
    };

    struct GuideBufferView
    {
        std::string_view              Name{};
        gsl::span<const pxr::GfVec3f> Pixels{};
    };

    static void Run(gsl::span<pxr::GfVec4f> framebuffer, gsl::span<const GuideBufferView> guideBuffers, int width,
                    int height, int frameCount, const Config &config);

    static void Run(gsl::span<pxr::GfVec4f> framebuffer, int width, int height, const Config &config);

  private:
    static std::vector<float> _packRgb(gsl::span<const pxr::GfVec4f> framebuffer);

    static void _writeRgb(gsl::span<pxr::GfVec4f> framebuffer, gsl::span<const float> color);

    [[nodiscard]] static std::string _toOidnImageName(std::string_view name);

    static void _applyChromaticityBlur(std::vector<float> &prefiltered, gsl::span<const float> color,
                                       unsigned int width, unsigned int height);

    static void _runOidnDenoiser(std::vector<float> &output, gsl::span<const float> prefiltered,
                                 gsl::span<const GuideBufferView> guideBuffers, unsigned int width, unsigned int height,
                                 int frameCount, const Config &config);

    static void _applyDenoiserAlgorithm(std::vector<float> &color, gsl::span<const GuideBufferView> guideBuffers,
                                        unsigned int width, unsigned int height, int frameCount, const Config &config);
};

#endif // HD_RESTIR_DENOISER_H