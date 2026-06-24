#ifndef HD_RESTIR_POST_PROCESS_H
#define HD_RESTIR_POST_PROCESS_H

#include "pxr/base/gf/vec4f.h"
#include "rendererInterface/frame_buffer_target.h"

#include <gsl/gsl>
#include <vector>

class PostProcess final
{
  public:
    struct Config
    {
        bool  EnableLensFlare{false};
        float ChromaticAberration{0.0f};
    };

    static void Run(Restir::IFrameBufferTarget &colorBuffer, unsigned int width, unsigned int height,
                    const Config &config);

    static void Run(gsl::span<GfVec4f> framebuffer, int width, int height, const Config &config);

  private:
    static std::vector<float> _packRgb(gsl::span<const GfVec4f> framebuffer);

    static void _applyLensFlare(std::vector<float> &finalColor, gsl::span<const float> color, unsigned int width,
                                unsigned int height);

    static void _applyChromaticAberration(std::vector<float> &finalColor, unsigned int width, unsigned int height,
                                          float chromaticAberration);

    static void _applyPostProcessAlgorithm(std::vector<float> &color, unsigned int width, unsigned int height,
                                           const Config &config);
};

#endif // HD_RESTIR_POST_PROCESS_H