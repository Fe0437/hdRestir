#include "reservoir.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace
{

    struct TestSample
    {
        int   id{};
        float targetFunction{1.0f};
    };

    using Reservoir = Restir::WeightedReservoir<TestSample>;

    void TestUpdateZeroWeightDoesNotSelectOrChangeWeightSum()
    {
        Reservoir   r{};
        Restir::Rng rng{1234u};
        r.Update(TestSample{7, 1.0f}, 0.0f, rng);
        assert(!r.ChosenSample.has_value());
        assert(r.WSum == 0.0f);
        assert(r.CandidateCount == 1);
    }

    void TestFinalizeNoChosenProducesZeroWeight()
    {
        Reservoir r{};
        r.Finalize(1);
        assert(r.W == 0.0f);
    }

    void TestFinalizeZeroTargetFunctionProducesZeroWeight()
    {
        Reservoir   r{};
        Restir::Rng rng{4321u};
        r.Update(TestSample{3, 0.0f}, 2.0f, rng);
        r.Finalize(1);
        assert(r.W == 0.0f);
    }

    void TestResetClearsState()
    {
        Reservoir   r{};
        Restir::Rng rng{999u};
        r.Update(TestSample{4, 1.0f}, 1.0f, rng);
        r.Finalize(1);
        r.Reset();
        assert(!r.ChosenSample.has_value());
        assert(r.WSum == 0.0f);
        assert(r.CandidateCount == 0);
        assert(r.W == 0.0f);
    }

    void TestCountMissIncrementsCountOnly()
    {
        Reservoir r{};
        r.CountMiss();
        r.CountMiss();
        assert(!r.ChosenSample.has_value());
        assert(r.WSum == 0.0f);
        assert(r.CandidateCount == 2);
    }

    void TestFinalizeWeightFormula()
    {
        // W = WSum / (M * p̂(chosen)).  1 candidate, weight=6, phat=3 → W = 6 / (1*3) = 2.
        Reservoir   r{};
        Restir::Rng rng{777u};
        r.Update(TestSample{0, 3.0f}, 6.0f, rng);
        r.Finalize(1);
        assert(r.ChosenSample.has_value());
        assert(std::abs(r.W - 2.0f) < 1e-5f);
    }

    void TestEqualWeightsProduceUniformSelection()
    {
        constexpr int kTrials{10000};
        constexpr int kCandidateCount{4};

        std::array<std::size_t, kCandidateCount> bins{};
        Restir::Rng                              rng{2024u};

        for (int trial{0}; trial < kTrials; ++trial)
        {
            Reservoir r{};
            for (int i{0}; i < kCandidateCount; ++i)
            {
                r.Update(TestSample{i, 1.0f}, 1.0f, rng);
            }
            assert(r.ChosenSample.has_value());
            ++bins[static_cast<std::size_t>(r.ChosenSample->id)];
        }

        const double expected{static_cast<double>(kTrials) / static_cast<double>(kCandidateCount)};
        double       chi2{0.0};
        for (std::size_t count : bins)
        {
            const double delta{static_cast<double>(count) - expected};
            chi2 += (delta * delta) / expected;
        }
        assert(chi2 < 11.345); // χ² < 11.345 @ α=0.01, df=3
    }

} // namespace

int main()
{
    TestUpdateZeroWeightDoesNotSelectOrChangeWeightSum();
    TestFinalizeNoChosenProducesZeroWeight();
    TestFinalizeZeroTargetFunctionProducesZeroWeight();
    TestResetClearsState();
    TestCountMissIncrementsCountOnly();
    TestFinalizeWeightFormula();
    TestEqualWeightsProduceUniformSelection();
    return 0;
}
