#pragma once

#include <cstddef>

namespace Restir
{

    // Identifies a particular call site within the integrator, for use in buffer indexing.
    struct CallIndex
    {
        std::size_t id;
        std::size_t stride;
    };

} // namespace Restir
