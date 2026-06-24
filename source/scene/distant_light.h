#pragma once

#include "light_interface.h"
#include "light_params.h"

namespace Restir
{

    class DistantLight final : public ILight
    {
      public:
        explicit DistantLight(const LightParams &params) noexcept : _params{params} {}

        void SetParams(const LightParams &params) override
        {
            _params = params;
        }

        [[nodiscard]] bool IsDeltaLight() const noexcept override
        {
            return true;
        }

        [[nodiscard]] std::optional<LightSample> SampleLight(const GfVec3f &hitPos, Rng &rng) const override;

        [[nodiscard]] Pdf EvalPdf(const GfVec3f &hitPos, const GfVec3f &dir, float dist,
                                  const GfVec3f &lightNormal) const override;

      private:
        LightParams _params;
    };

} // namespace Restir
