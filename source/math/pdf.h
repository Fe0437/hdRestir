#pragma once

#include <cmath>

namespace Restir
{
    enum class PdfSpace
    {
        Area,
        SolidAngle
    };

    struct Pdf
    {
        float    value{0.f};
        PdfSpace space{PdfSpace::Area};

        // dist2 = |x - y|^2,  cosY = dot(-wi, n_y)  (cosine at the light surface)
        // cosY must be > 0 — caller is responsible for checking before calling
        [[nodiscard]] Pdf ConvertTo(PdfSpace target, float dist2, float cosY) const
        {
            if (space == target)
                return *this;
            if (space == PdfSpace::Area)
                return {value * dist2 / cosY, PdfSpace::SolidAngle};
            return {value * cosY / dist2, PdfSpace::Area};
        }
    };
} // namespace Restir
