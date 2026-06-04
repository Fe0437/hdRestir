#pragma once

#include <algorithm>
#include <cstddef>

namespace Restir {

// All pixel-sizing state for a render pass.
//
// windowWidth/windowHeight   — full camera frame (AOV buffer dimensions).
// resolutionLevel            — 0 = full res, N = render at ceil(window / 2^N).
// visibleMin/Max X/Y         — half-open pixel region [min, max) that will be
//                              visible in the final composited output.
//                              For a single-pipeline render this equals the
//                              full window. The SplitScreenCompositor narrows
//                              each side to the pixels it actually contributes.
//
// Passes doing per-pixel work (raycast, integration, …) use RenderedWidth/
// RenderedHeight so that resolution-level downscaling works transparently.
// Passes operating on the composed output (split-screen blit, debug overlay)
// use windowWidth/windowHeight and the visible bounds.
struct CameraFrame {
    int windowWidth{0};
    int windowHeight{0};
    int resolutionLevel{0};

    int visibleMinX{0};
    int visibleMinY{0};
    int visibleMaxX{0};
    int visibleMaxY{0};

    [[nodiscard]] int         RenderedWidth()  const { return _scaled(windowWidth);  }
    [[nodiscard]] int         RenderedHeight() const { return _scaled(windowHeight); }
    [[nodiscard]] std::size_t PixelCount()     const {
        return static_cast<std::size_t>(RenderedWidth()) * static_cast<std::size_t>(RenderedHeight());
    }
    [[nodiscard]] bool NeedsUpscale() const { return resolutionLevel > 0; }

    // Returns a frame at a different resolution level with visible bounds reset
    // to the full window (the compositor will override them as needed).
    [[nodiscard]] CameraFrame AtResolutionLevel(int level) const {
        return CameraFrame{
            .windowWidth    = windowWidth,
            .windowHeight   = windowHeight,
            .resolutionLevel = level,
            .visibleMinX    = 0,
            .visibleMinY    = 0,
            .visibleMaxX    = windowWidth,
            .visibleMaxY    = windowHeight,
        };
    }

    void MarkUpscaled() { resolutionLevel = 0; }

private:
    [[nodiscard]] int _scaled(int dim) const {
        const int divisor{1 << std::clamp(resolutionLevel, 0, 4)};
        return std::max(1, (dim + divisor - 1) / divisor);
    }
};

}  // namespace Restir
