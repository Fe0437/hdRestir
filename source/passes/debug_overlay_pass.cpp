#include "debug_overlay_pass.h"

#if DEBUG_ENABLED

#include "render_context.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        // Public-domain 8x8 bitmap font (CC0, from dhepper/font8x8).
        // Each entry: 8 bytes, one per scanline row (top to bottom).
        // Bit 0 (LSB) = leftmost pixel.
        static constexpr uint8_t kFont8x8[128][8] = {
            // 0x00 - 0x1F: control characters (empty glyphs)
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x00
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x01
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x02
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x03
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x04
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x05
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x06
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x07
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x08
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x09
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0A
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0B
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0C
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0D
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0E
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x0F
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x10
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x11
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x12
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x13
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x14
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x15
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x16
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x17
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x18
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x19
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1A
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1B
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1C
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1D
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1E
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x1F
            // 0x20 - 0x7F: printable ASCII
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 space
            {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // 0x21 !
            {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x22 "
            {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // 0x23 #
            {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // 0x24 $
            {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // 0x25 %
            {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // 0x26 &
            {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x27 '
            {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // 0x28 (
            {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // 0x29 )
            {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // 0x2A *
            {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // 0x2B +
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // 0x2C ,
            {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 0x2D -
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // 0x2E .
            {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // 0x2F /
            {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0x30 0
            {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 0x31 1
            {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 0x32 2
            {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 0x33 3
            {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 0x34 4
            {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 0x35 5
            {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 0x36 6
            {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 0x37 7
            {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 0x38 8
            {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 0x39 9
            {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // 0x3A :
            {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // 0x3B ;
            {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // 0x3C <
            {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // 0x3D =
            {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // 0x3E >
            {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // 0x3F ?
            {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // 0x40 @
            {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // 0x41 A
            {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // 0x42 B
            {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // 0x43 C
            {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // 0x44 D
            {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // 0x45 E
            {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // 0x46 F
            {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // 0x47 G
            {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // 0x48 H
            {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 0x49 I
            {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // 0x4A J
            {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // 0x4B K
            {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // 0x4C L
            {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // 0x4D M
            {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // 0x4E N
            {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // 0x4F O
            {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // 0x50 P
            {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // 0x51 Q
            {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // 0x52 R
            {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // 0x53 S
            {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 0x54 T
            {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // 0x55 U
            {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 0x56 V
            {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // 0x57 W
            {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // 0x58 X
            {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // 0x59 Y
            {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // 0x5A Z
            {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // 0x5B [
            {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // 0x5C '\'
            {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // 0x5D ]
            {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // 0x5E ^
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 0x5F _
            {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x60 `
            {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // 0x61 a
            {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // 0x62 b
            {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // 0x63 c
            {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, // 0x64 d
            {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // 0x65 e
            {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, // 0x66 f
            {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 0x67 g
            {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // 0x68 h
            {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 0x69 i
            {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // 0x6A j
            {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // 0x6B k
            {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 0x6C l
            {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // 0x6D m
            {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // 0x6E n
            {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // 0x6F o
            {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // 0x70 p
            {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // 0x71 q
            {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // 0x72 r
            {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // 0x73 s
            {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // 0x74 t
            {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // 0x75 u
            {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 0x76 v
            {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // 0x77 w
            {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // 0x78 x
            {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 0x79 y
            {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // 0x7A z
            {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // 0x7B {
            {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // 0x7C |
            {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // 0x7D }
            {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x7E ~
            {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // 0x7F DEL (filled block)
        };

    } // namespace

    void DebugOverlayPass::_execute(RenderContext &ctx)
    {
#if METRICS_ENABLED
        if (!_config.Enable && !_config.EnableProfiling)
        {
            return;
        }
#else
        if (!_config.Enable)
        {
            return;
        }
#endif

        DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "DebugOverlayPass requires Color buffer");

        auto fb{ctx.buf<GfVec4f>(kColorOutputName)};

        // bufW/bufH: full buffer dimensions (for pixel indexing stride and clipping).
        // visW/visH: the region this sub-pipeline actually contributes to after
        //            split-screen compositing (the debug overlay positions text
        //            within this narrower visible area).
        const int bufW{ctx.frame.RenderedWidth()};
        const int bufH{ctx.frame.RenderedHeight()};
        const int visMinX{ctx.frame.visibleMinX};
        const int visMinY{ctx.frame.visibleMinY};
        const int visW{ctx.frame.visibleMaxX - visMinX};
        const int visH{ctx.frame.visibleMaxY - visMinY};

#if METRICS_ENABLED
        if (_config.EnableProfiling)
        {
            _drawProfilingOverlay(ctx, fb, bufW, bufH, visMinX, visMinY, visW, visH);
        }
#endif

        if (!_config.Enable)
        {
            return;
        }

        // The Hydra AOV buffer is displayed by usdview with y=0 at the GL bottom,
        // so small buffer-y values appear at the visual bottom and large values at
        // the visual top. The anchor y passed to _drawText/_drawChar is the
        // visual-bottom edge of the glyph row (row 0 of the font, the top of the
        // character, is drawn at y + 7*scale in buffer space, which maps to the
        // visual top of the character).

        constexpr int margin{16};
        constexpr int builtinScale{2};
        constexpr int builtinLineH{8 * builtinScale + 4};
        const GfVec4f white{1.0f, 1.0f, 1.0f, 1.0f};

        // Sum of all TOP-LEVEL (parent == Metrics::kMetricNoParent) pass timings across
        // every rendered frame so far — a pass's own recorded time already
        // includes every phase nested under it, so summing every entry
        // (including nested phases) would double/triple count.
        float sumMs{0.0f};
        if (ctx.buffers.Has(Metrics::kMetricSumTimingOutputName) && ctx.buffers.Has(Metrics::kMetricParentOutputName))
        {
            const auto        sums{ctx.buf<float>(Metrics::kMetricSumTimingOutputName)};
            const auto        parents{ctx.buf<std::size_t>(Metrics::kMetricParentOutputName)};
            const std::size_t rootCount{std::min(sums.size(), parents.size())};
            for (std::size_t i{0}; i < rootCount; ++i)
            {
                if (parents[i] == Metrics::kMetricNoParent)
                {
                    sumMs += sums[i];
                }
            }
        }
        const float meanMs{sumMs / static_cast<float>(ctx.frameIndex + 1)};

        const int         msInt{static_cast<int>(meanMs)};
        const int         msFrac{static_cast<int>((meanMs - static_cast<float>(msInt)) * 10.0f)};
        const std::string msStr{std::to_string(msInt) + "." + std::to_string(msFrac) + "ms"};

        const int         tSec{static_cast<int>(sumMs / 1000.0f)};
        const int         tFrac{static_cast<int>((sumMs / 1000.0f - static_cast<float>(tSec)) * 10.0f)};
        const std::string totalStr{"Total: " + std::to_string(tSec) + "." + std::to_string(tFrac) + "s"};

        // Built-in stats centered within the visible area, anchored near the top.
        const int         topAnchor{visMinY + visH - margin - 7 * builtinScale};
        const std::string line0{"Frame: " + std::to_string(ctx.frameIndex)};
        const std::string line1{std::to_string(visW) + "x" + std::to_string(visH)};
        _drawText(fb, bufW, bufH, visMinX + (visW - static_cast<int>(line0.size()) * 8 * builtinScale) / 2, topAnchor,
                  line0, white, builtinScale);
        _drawText(fb, bufW, bufH, visMinX + (visW - static_cast<int>(line1.size()) * 8 * builtinScale) / 2,
                  topAnchor - builtinLineH, line1, white, builtinScale);
        _drawText(fb, bufW, bufH, visMinX + (visW - static_cast<int>(msStr.size()) * 8 * builtinScale) / 2,
                  topAnchor - 2 * builtinLineH, msStr, white, builtinScale);
        _drawText(fb, bufW, bufH, visMinX + (visW - static_cast<int>(totalStr.size()) * 8 * builtinScale) / 2,
                  topAnchor - 3 * builtinLineH, totalStr, white, builtinScale);

#if METRICS_ENABLED
        if (ctx.buffers.Has(kVarianceOutputName))
        {
            const auto &v{ctx.buf<VarianceStats>(kVarianceOutputName)[0]};
            char        varBuf[48];
            std::snprintf(varBuf, sizeof(varBuf), "Var %.2e", v.max);
            const std::string varStr{varBuf};
            _drawText(fb, bufW, bufH, visMinX + (visW - static_cast<int>(varStr.size()) * 8 * builtinScale) / 2,
                      topAnchor - 4 * builtinLineH, varStr, white, builtinScale);
        }
#endif

        // User-configured entries centered within the visible area.
        // Entry Y=0 → visual bottom, Y=1 → visual top (within the visible region).
        for (const auto &entry : _config.Entries)
        {
            const int py{visMinY + static_cast<int>(entry.Y * static_cast<float>(visH))};
            const int textWidth{static_cast<int>(entry.Text.size()) * 8 * entry.Scale};
            _drawText(fb, bufW, bufH, visMinX + (visW - textWidth) / 2, py, entry.Text, entry.Color, entry.Scale);
        }
    }

    void DebugOverlayPass::_drawText(gsl::span<GfVec4f> fb, int w, int h, int x, int y, std::string_view text,
                                     GfVec4f color, int scale)
    {
        int cx{x};
        for (const char c : text)
        {
            _drawChar(fb, w, h, cx, y, c, color, scale);
            cx += 8 * scale;
        }
    }

    void DebugOverlayPass::_drawChar(gsl::span<GfVec4f> fb, int w, int h, int x, int y, char c, GfVec4f color,
                                     int scale)
    {
        const auto idx{static_cast<unsigned char>(c)};
        if (idx >= 128)
        {
            return;
        }
        for (int row{0}; row < 8; ++row)
        {
            const uint8_t bits{kFont8x8[idx][row]};
            for (int col{0}; col < 8; ++col)
            {
                if (!(bits & (1 << col)))
                {
                    continue;
                }
                for (int sy{0}; sy < scale; ++sy)
                {
                    for (int sx{0}; sx < scale; ++sx)
                    {
                        const int px{x + col * scale + sx};
                        const int py{y + (7 - row) * scale + sy};
                        if (px < 0 || px >= w || py < 0 || py >= h)
                        {
                            continue;
                        }
                        const auto pidx{static_cast<std::size_t>(py) * static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(px)};
                        fb[pidx] = color;
                    }
                }
            }
        }
    }

#if METRICS_ENABLED
    void DebugOverlayPass::_drawBar(gsl::span<GfVec4f> fb, int w, int h, int x, int y, int barWidth, int barHeight,
                                    GfVec4f color)
    {
        for (int row{0}; row < barHeight; ++row)
        {
            const int py{y + row};
            if (py < 0 || py >= h)
            {
                continue;
            }
            for (int col{0}; col < barWidth; ++col)
            {
                const int px{x + col};
                if (px < 0 || px >= w)
                {
                    continue;
                }
                const auto pidx{static_cast<std::size_t>(py) * static_cast<std::size_t>(w) +
                                static_cast<std::size_t>(px)};
                fb[pidx] = color;
            }
        }
    }

    // Bar chart of the render-time tree recorded via Metrics::ScopedMetricTimer/
    // Metrics::RecordMetric (core/metrics_on_buffers.h): each pass is a root entry
    // (parent == Metrics::kMetricNoParent); passes and integrators nest their own
    // phase/sub-phase timings arbitrarily deep under their own entry. Each
    // bar is a percentage of ITS OWN PARENT's total (root passes are a
    // percentage of the grand pipeline total), and gets progressively
    // smaller/more indented with depth. Pipeline-only — usdview's own
    // frame-time readout already shows the grand total, so comparing the two
    // is enough to spot cost living outside any pass (e.g. scene
    // construction) without tracking it here too.
    void DebugOverlayPass::_drawProfilingOverlay(RenderContext &ctx, gsl::span<GfVec4f> fb, int bufW, int bufH,
                                                 int visMinX, int visMinY, int visW, int visH)
    {
        constexpr int margin{16};
        constexpr int maxRowScale{20};
        constexpr int minRowScale{1};
        constexpr int barGap{4};  // gap between a label and its bar
        constexpr int rowGap{10}; // gap between one row's bar and the next row's label
        const GfVec4f white{1.0f, 1.0f, 1.0f, 1.0f};
        const GfVec4f depthColors[]{
            {0.2f, 0.8f, 0.3f, 1.0f},
            {0.9f, 0.6f, 0.15f, 1.0f},
            {0.3f, 0.6f, 0.9f, 1.0f},
            {0.8f, 0.3f, 0.7f, 1.0f},
        };

        const int x{visMinX + margin};

        const auto barHeightAtScale = [](int scale) { return std::max(4, scale * 2); };
        const auto rowHAtScale      = [&](int scale) { return 8 * scale + barGap + barHeightAtScale(scale) + rowGap; };
        const auto colorAtDepth     = [&](int depth) -> const GfVec4f &
        { return depthColors[std::min<std::size_t>(static_cast<std::size_t>(depth), 3)]; };

        const bool haveSums{ctx.buffers.Has(Metrics::kMetricSumTimingOutputName)};
        const auto sumMs{haveSums ? ctx.buf<float>(Metrics::kMetricSumTimingOutputName) : gsl::span<float>{}};
        // Metrics::kMetricNameOutputName/Metrics::kMetricParentOutputName are recorded fresh
        // every frame by Metrics::ScopedMetricTimer/Metrics::RecordMetric, in the same index
        // order as Metrics::kMetricSumTimingOutputName — stable across frames since
        // the pipeline's pass/phase structure doesn't change frame to frame,
        // so this frame's names/parents correctly label the cumulative sums.
        const bool        haveNames{ctx.buffers.Has(Metrics::kMetricNameOutputName)};
        const auto        names{haveNames ? ctx.buf<Metrics::MetricNameEntry>(Metrics::kMetricNameOutputName)
                                          : gsl::span<Metrics::MetricNameEntry>{}};
        const bool        haveParents{ctx.buffers.Has(Metrics::kMetricParentOutputName)};
        const auto        parents{haveParents ? ctx.buf<std::size_t>(Metrics::kMetricParentOutputName)
                                              : gsl::span<std::size_t>{}};
        const std::size_t count{std::min({sumMs.size(), names.size(), parents.size()})};

        float rootTotalMs{0.0f};
        for (std::size_t i{0}; i < count; ++i)
        {
            if (parents[i] == Metrics::kMetricNoParent)
            {
                rootTotalMs += sumMs[i];
            }
        }

        if (rootTotalMs <= 0.0f)
        {
            _drawText(fb, bufW, bufH, x, visMinY + margin, "Profiling: no data yet", white, minRowScale);
            return;
        }

        // Pass 1: gather row count / max depth / longest formatted label, so
        // a single scale (uniform across every row — only indentation grows
        // with depth) can be picked such that the whole chart's bounding box
        // (maxDepth indents + longest label wide, rowCount rows tall) fits
        // within roughly a quarter of the visible screen area (half its
        // width times half its height).
        int                                   rowCount{0};
        int                                   maxDepth{0};
        int                                   maxLabelLen{0};
        std::function<void(std::size_t, int)> gather = [&](std::size_t parentIdx, int depth)
        {
            for (std::size_t i{0}; i < count; ++i)
            {
                if (parents[i] != parentIdx)
                {
                    continue;
                }
                ++rowCount;
                maxDepth = std::max(maxDepth, depth);
                char      label[64];
                const int len{std::snprintf(label, sizeof(label), "%.*s 100%%",
                                            static_cast<int>(sizeof(Metrics::MetricNameEntry::Name)), names[i].Name)};
                maxLabelLen = std::max(maxLabelLen, len);
                gather(i, depth + 1);
            }
        };
        gather(Metrics::kMetricNoParent, 0);

        const int targetW{std::max(32, visW / 2 - margin)};
        const int targetH{std::max(32, visH / 2 - margin)};
        int       scale{maxRowScale};
        while (scale > minRowScale)
        {
            const int totalH{rowCount * rowHAtScale(scale)};
            const int totalW{(maxDepth * 2 + maxLabelLen) * 8 * scale};
            if (totalH <= targetH && totalW <= targetW)
            {
                break;
            }
            --scale;
        }

        const int indentStep{2 * 8 * scale};
        const int barHeight{barHeightAtScale(scale)};
        const int rowH{rowHAtScale(scale)};
        const int barMaxWidthBase{targetW};

        int y{visMinY + margin + rowCount * rowH - rowH};

        // Pass 2: draw depth-first, each entry's bar as a percentage of ITS
        // OWN PARENT's total.
        std::function<void(std::size_t, float, int)> draw = [&](std::size_t parentIdx, float parentTotalMs, int depth)
        {
            for (std::size_t i{0}; i < count; ++i)
            {
                if (parents[i] != parentIdx)
                {
                    continue;
                }

                const int   indent{depth * indentStep};
                const int   barMaxWidth{std::max(32, barMaxWidthBase - indent)};
                const float pct{parentTotalMs > 0.0f ? 100.0f * sumMs[i] / parentTotalMs : 0.0f};

                char label[64];
                std::snprintf(label, sizeof(label), "%.*s %d%%",
                              static_cast<int>(sizeof(Metrics::MetricNameEntry::Name)), names[i].Name,
                              static_cast<int>(pct + 0.5f));
                _drawText(fb, bufW, bufH, x + indent, y, label, white, scale);

                const int barY{y - barGap - barHeight};
                const int barW{std::clamp(static_cast<int>(barMaxWidth * (pct / 100.0f)), 0, barMaxWidth)};
                _drawBar(fb, bufW, bufH, x + indent, barY, barW, barHeight, colorAtDepth(depth));
                y -= rowH;

                draw(i, sumMs[i], depth + 1);
            }
        };
        draw(Metrics::kMetricNoParent, rootTotalMs, 0);
    }
#endif // METRICS_ENABLED

} // namespace Restir

#endif // DEBUG_ENABLED
