#pragma once

#include "debug.h"
#include "render_pipeline.h"

#include "pxr/base/gf/vec4f.h"

#include <gsl/gsl>
#include <memory>
#include <string_view>

namespace Restir {

class SplitScreenCompositor final {
public:
    SplitScreenCompositor(std::unique_ptr<RenderPipeline> left,
                          std::unique_ptr<RenderPipeline> right,
                          int leftResolutionLevel = 0,
                          int rightResolutionLevel = 0);

    void execute(RenderContext& baseCtx);

    void swapSides() noexcept;
    void setSplitPosition(float t) noexcept;

    static void debugBlit(gsl::span<GfVec4f> dst,
                          gsl::span<const GfVec4f> srcL,
                          gsl::span<const GfVec4f> srcR,
                          int width,
                          int height,
                          float splitT) {
        blit(dst, srcL, srcR, width, height, splitT);
    }

#if DEBUG_ENABLED
    [[nodiscard]] std::string_view leftName() const noexcept {
        return _left ? _left->name() : std::string_view{};
    }

    [[nodiscard]] std::string_view rightName() const noexcept {
        return _right ? _right->name() : std::string_view{};
    }
#endif

private:
    std::unique_ptr<RenderPipeline> _left;
    std::unique_ptr<RenderPipeline> _right;
    int _leftResolutionLevel{0};
    int _rightResolutionLevel{0};
    float _splitT{0.5f};

    static void blit(gsl::span<GfVec4f> dst,
                     gsl::span<const GfVec4f> srcL,
                     gsl::span<const GfVec4f> srcR,
                     int width,
                     int height,
                     float splitT);
};

}  // namespace Restir
