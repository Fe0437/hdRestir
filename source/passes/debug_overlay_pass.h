#pragma once

#include "debug.h"

#if DEBUG_ENABLED

#include "output_names.h"
#include "pxr/base/gf/vec4f.h"
#include "render_pass.h"

#include <gsl/gsl>
#include <string>
#include <string_view>
#include <vector>

namespace Restir
{

    // One text entry for the debug overlay.
    // X: normalized horizontal position (0=left, 1=right); ignored when centered.
    // Y: normalized vertical position where 0=visual bottom, 1=visual top.
    //    The Hydra AOV buffer is displayed with y=0 at the GL bottom, so large
    //    buffer-y values appear at the visual top of the viewport.
    // Scale multiplies the base 8x8 glyph (scale=2 -> 16 px tall).
    struct DebugOverlayTextEntry
    {
        std::string Text{};
        float       X{0.02f};
        float       Y{0.05f};
        int         Scale{2};
        GfVec4f     Color{1.0f, 1.0f, 0.0f, 1.0f};
    };

    struct DebugOverlayConfig
    {
        bool                               Enable{false};
        std::vector<DebugOverlayTextEntry> Entries{};
#if METRICS_ENABLED
        // Independent toggle for the per-pass timing breakdown (bar chart of
        // each pass's share of the session's cumulative render time, plus
        // "Other" for time outside the pipeline, e.g. scene construction).
        // Can be enabled without `Enable`. Reuses kPassSumTimingOutputName
        // (already accumulated for the "Total:" stat below).
        bool EnableProfiling{false};
#endif
    };

    class DebugOverlayPass final : public RenderPass
    {
      public:
        using Config = DebugOverlayConfig;

        explicit DebugOverlayPass(Config config = {}) : RenderPass{"DebugOverlayPass"}, _config{config} {}

        [[nodiscard]] static std::vector<std::string> StaticInputs()
        {
            return {
                std::string{kColorOutputName},
#if METRICS_ENABLED
                std::string{kVarianceOutputName},
#endif
            };
        }

        [[nodiscard]] static std::vector<std::string> StaticOutputs()
        {
            return {std::string{kColorOutputName}};
        }

        [[nodiscard]] std::vector<std::string> Inputs() const override
        {
            return StaticInputs();
        }
        [[nodiscard]] std::vector<std::string> Outputs() const override
        {
            return StaticOutputs();
        }

        void SetConfig(Config config)
        {
            _config = config;
        }

      protected:
        void _execute(RenderContext &ctx) override;

      private:
        void _drawText(gsl::span<GfVec4f> fb, int w, int h, int x, int y, std::string_view text, GfVec4f color,
                       int scale = 1);
        void _drawChar(gsl::span<GfVec4f> fb, int w, int h, int x, int y, char c, GfVec4f color, int scale);
#if METRICS_ENABLED
        void _drawBar(gsl::span<GfVec4f> fb, int w, int h, int x, int y, int barWidth, int barHeight, GfVec4f color);
        void _drawProfilingOverlay(RenderContext &ctx, gsl::span<GfVec4f> fb, int bufW, int bufH, int visMinX,
                                   int visMinY, int visW, int visH);
#endif

        Config _config{};
    };

} // namespace Restir

#endif // DEBUG_ENABLED
