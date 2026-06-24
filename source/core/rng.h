#pragma once

#include <cstdint>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <random>

namespace Restir
{
    class Rng
    {
      public:
        explicit Rng(uint32_t seed = 1234) noexcept : _seed{seed} {}

        void ResetSeed(uint32_t seed) noexcept
        {
            _seed.seed(seed);
        }

        [[nodiscard]] int NextInt() noexcept
        {
            return _distInt(_seed);
        }

        [[nodiscard]] uint32_t NextUint() noexcept
        {
            return _distUint(_seed);
        }

        [[nodiscard]] uint32_t NextUint(uint32_t bound) noexcept
        {
            std::uniform_int_distribution<uint32_t> dist{0u, bound - 1u};
            return dist(_seed);
        }

        [[nodiscard]] float NextFloat() noexcept
        {
            return _distFloat(_seed);
        }

        [[nodiscard]] pxr::GfVec2f NextVec2f() noexcept
        {
            return pxr::GfVec2f{NextFloat(), NextFloat()};
        }

        [[nodiscard]] pxr::GfVec3f NextVec3f() noexcept
        {
            return pxr::GfVec3f{NextFloat(), NextFloat(), NextFloat()};
        }

      private:
        std::mt19937_64                         _seed;
        std::uniform_int_distribution<int>      _distInt;
        std::uniform_int_distribution<uint32_t> _distUint;
        std::uniform_real_distribution<float>   _distFloat;
    };

} // namespace Restir
