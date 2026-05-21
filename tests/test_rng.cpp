#include "rng.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>

namespace {

void TestSameSeedSameSequence() {
    Restir::Rng a;
    Restir::Rng b;
    a.ResetSeed(1234 ^ 56);
    b.ResetSeed(1234 ^ 56);

    for (int i = 0; i < 1000; ++i) {
        assert(a.NextUint() == b.NextUint());
    }
}

void TestDifferentFrameDifferentSequence() {
    Restir::Rng a;
    Restir::Rng b;
    a.ResetSeed(1234 ^ 56);
    b.ResetSeed(1234 ^ 57);

    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        if (a.NextUint() != b.NextUint()) {
            differs = true;
            break;
        }
    }
    assert(differs);
}

void TestBoundedRange() {
    Restir::Rng rng;
    rng.ResetSeed(99 ^ 3);

    std::array<std::size_t, 7> bins{};
    for (int i = 0; i < 10000; ++i) {
        const std::uint32_t value = rng.NextUint(7);
        assert(value < 7U);
        ++bins[value];
    }

    const double expected = 10000.0 / 7.0;
    double chi2 = 0.0;
    for (std::size_t count : bins) {
        const double delta = static_cast<double>(count) - expected;
        chi2 += (delta * delta) / expected;
    }

    assert(chi2 < 18.475);
}

} // namespace

int main() {
    TestSameSeedSameSequence();
    TestDifferentFrameDifferentSequence();
    TestBoundedRange();
    return 0;
}
