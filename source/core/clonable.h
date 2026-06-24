#pragma once

#include <memory>

namespace Restir
{

    template <typename T> class IClonableAs
    {
      public:
        virtual ~IClonableAs() = default;

        [[nodiscard]] virtual std::unique_ptr<T> CloneAs() const = 0;
    };

} // namespace Restir
