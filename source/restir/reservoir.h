#pragma once

#include "rng.h"

#include <cmath>
#include <optional>

namespace Restir
{

/**
 * Concept for types that can be stored in a WeightedReservoir.
 * The sample must expose `targetFunction` (p̂) used by Finalize() to compute
 * W = WSum / (frameCount × p̂(chosen)).
 */
#if defined(__cpp_concepts)
    template <typename T>
    concept ReservoirSample = requires(const T &s) {
        { s.targetFunction } -> std::convertible_to<float>;
    };

    /**
     * Concept for the reservoir itself.
     * Requires the full protocol used by Scoped<T> and merge callers.
     */
    template <typename T>
    concept Reservoir = requires(T &r, const T &cr, int n, float w, Rng &rng) {
        r.Merge(cr, rng);
        r.Merge(cr, w, rng);
        r.Finalize(n); // n = candidatesPerFrame
        r.CountMiss();
    };
#endif

    /**
     * Streaming weighted reservoir (WRS) for resampled importance sampling (RIS)
     * with temporal reuse.
     *
     * risWeights should NOT include a 1/N factor: risWeight = misWeight × p̂ / p
     * where N = candidates per technique (_candidateCount).
     *
     * Finalize produces the correct unbiased weight:
     *
     *   frameCount = CandidateCount / candidatesPerFrame          (= K after K temporal merges)
     *   W          = WSum / (frameCount × p̂(chosen))
     *
     * After K temporal merges CandidateCount = K × candidatesPerFrame, so frameCount = K and
     * W stays stable (WSum accumulates K frames, division by frameCount normalizes).
     * Adding new techniques increases allCandidates.size() (= candidatesPerFrame) automatically
     * — no callsite change needed.
     *
     * Per-frame usage (Bitterli et al. 2020, Algorithm 2/4):
     *
     *   WeightedReservoir r_prev = buffer[pixelId];
     *   Scoped<WeightedReservoir> r{candidatesPerFrame};    // = allCandidates.size()
     *   for (auto& c : newCandidates)
     *       r->Update(c, c.risWeight, rng);                // risWeight = misWeight × p̂/p
     *   r->Merge(r_prev, r_prev.WSum * ratio, rng);       // temporal reuse
     *   buffer[pixelId] = r.Release();                     // Finalize called inside Release
     *   use(buffer[pixelId].ChosenSample, buffer[pixelId].W);
     */
    template <
#if defined(__cpp_concepts)
        ReservoirSample SampleT
#else
        typename SampleT
#endif
        >
    struct WeightedReservoir
    {
        std::optional<SampleT> ChosenSample{};
        float                  WSum{0.0f};
        int                    CandidateCount{0};
        float                  W{0.0f};

        void Update(const SampleT &sample, float weight, Rng &rng)
        {
            ++CandidateCount;
            if (weight <= 0.0f)
                return;
            WSum += weight;
            if (rng.NextFloat() <= weight / WSum)
                ChosenSample = sample;
        }

        void CountMiss()
        {
            ++CandidateCount;
        }

        /**
         * Compute W = WSum / (frameCount × p̂(chosen)), where
         *   frameCount = CandidateCount / candidatesPerFrame.
         *
         * candidatesPerFrame is allCandidates.size() — the total candidates across
         * all techniques in one frame. After K temporal merges,
         * CandidateCount = K × candidatesPerFrame and frameCount = K, so W stays
         * stable regardless of accumulation depth.
         *
         * Adding a new technique increases allCandidates.size() automatically,
         * so the callsite never needs updating.
         *
         * risWeights do NOT need their own 1/N factor for a single frame (frameCount=1 makes
         * this division a no-op there); this frameCount division is what keeps W stable
         * specifically across K *temporal* merges, where CandidateCount grows to
         * K × candidatesPerFrame.
         */
        void Finalize(int candidatesPerFrame)
        {
            if (!ChosenSample.has_value() || CandidateCount <= 0)
            {
                W = 0.0f;
                return;
            }
            const float phat{static_cast<float>(ChosenSample->targetFunction)};
            if (phat <= 0.0f)
            {
                W = 0.0f;
                return;
            }
            const int frameCount{std::max(1, CandidateCount / std::max(1, candidatesPerFrame))};
            W = WSum / (static_cast<float>(frameCount) * phat);
            if (!std::isfinite(W))
                W = 0.0f;
        }

        /**
         * Same-context merge (Algorithm 4, Bitterli et al. 2020).
         * mergeWeight = other.WSum — use when both reservoirs were built under
         * the same target function (no re-evaluation needed).
         */
        void Merge(const WeightedReservoir &other, Rng &rng)
        {
            Merge(other, other.WSum, rng);
        }

        /**
         * Cross-context merge — use for temporal/spatial reuse where the
         * previous sample was re-evaluated under the current shading point:
         *
         *   mergeWeight = other.WSum × (p̂_curr / p̂_gen)
         *
         * Update other.ChosenSample to reflect the current context before
         * calling so whatever gets copied is already correct.
         * CandidateCount is always accumulated regardless of selection.
         */
        void Merge(const WeightedReservoir &other, float mergeWeight, Rng &rng)
        {
            CandidateCount += other.CandidateCount;
            if (!other.ChosenSample.has_value() || mergeWeight <= 0.0f)
                return;
            WSum += mergeWeight;
            if (rng.NextFloat() <= mergeWeight / WSum)
                ChosenSample = other.ChosenSample;
        }

        void Reset()
        {
            *this = WeightedReservoir{};
        }
    };

    /**
     * RAII wrapper that calls Finalize(numTechniques) automatically.
     *
     * Usage:
     *   Scoped<WeightedReservoir<MySample>> reservoir{numTechniques};
     *   reservoir->Update(...);
     *   reservoir->CountMiss();
     *   buffer[id] = reservoir.Release();   // Finalize called here
     *
     * If Release() is never called the reservoir is finalized on destruction
     * and the result is discarded — no leaks, no forgotten Finalize().
     */
#if defined(__cpp_concepts)
    template <Reservoir T>
#else
    template <typename T>
#endif
    class Scoped
    {
      public:
        explicit Scoped(int candidatesPerFrame) : _candidatesPerFrame{candidatesPerFrame} {}

        Scoped(const Scoped &)            = delete;
        Scoped &operator=(const Scoped &) = delete;

        ~Scoped()
        {
            _reservoir.Finalize(_candidatesPerFrame);
        }

        T *operator->()
        {
            return &_reservoir;
        }
        const T *operator->() const
        {
            return &_reservoir;
        }

        /// Finalize and move the reservoir out. The destructor will call
        /// Finalize a second time on the moved-from object — a safe no-op.
        [[nodiscard]] T Release()
        {
            _reservoir.Finalize(_candidatesPerFrame);
            return std::move(_reservoir);
        }

      private:
        T   _reservoir{};
        int _candidatesPerFrame;
    };

} // namespace Restir
