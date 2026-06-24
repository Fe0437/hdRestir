#pragma once

#include "rng.h"

#include <concepts>
#include <gsl/gsl>
#include <optional>

namespace Restir
{
    namespace RIS
    {

        template <typename T>
        concept HasWeight = requires(const T &t) {
            { t.GetWeight() } -> std::convertible_to<double>;
        };

        struct ChosenCandidate
        {
            int    Index{-1};
            double WeightSum{0.0};
        };

        // Algorithm 1 helper: choose one index proportionally to GetWeight() on each element.
        // Returns nullopt when every weight is non-positive.
        template <HasWeight T>
        [[nodiscard]] std::optional<ChosenCandidate> RandomIndexFromWeights(gsl::span<const T> candidates,
                                                                            Rng               &rng) noexcept
        {
            double weightSum{0.0};
            for (const T &candidate : candidates)
            {
                const double weight{candidate.GetWeight()};
                if (weight > 0.0)
                {
                    weightSum += weight;
                }
            }

            if (weightSum <= 0.0)
            {
                return std::nullopt;
            }

            double remainingWeight{static_cast<double>(rng.NextFloat()) * weightSum};
            for (int i{0}; i < gsl::narrow<int>(candidates.size()); ++i)
            {
                const double weight{candidates[static_cast<std::size_t>(i)].GetWeight()};
                if (weight <= 0.0)
                {
                    continue;
                }

                remainingWeight -= weight;
                if (remainingWeight <= 0.0)
                {
                    return ChosenCandidate{.Index = i, .WeightSum = weightSum};
                }
            }

            return std::nullopt;
        }

    } // namespace RIS
} // namespace Restir