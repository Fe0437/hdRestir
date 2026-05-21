#pragma once

#include "debug.h"

#include <gsl/gsl>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Restir {

// A typed CPU array with a name. Stride is fixed at construction;
// Resize() can change count but not stride.
class FrameBuffer final {
public:
    explicit FrameBuffer(std::string name, std::size_t stride, std::size_t count);

    template<typename T> [[nodiscard]] gsl::span<T>       As();
    template<typename T> [[nodiscard]] gsl::span<const T> As() const;

    void Reconfigure(std::size_t newStride, std::size_t newCount);
    void Resize(std::size_t newCount);

    [[nodiscard]] unsigned char* Data() noexcept { return _storage.data(); }
    [[nodiscard]] const unsigned char* Data() const noexcept { return _storage.data(); }
    [[nodiscard]] gsl::span<unsigned char> Bytes() noexcept { return _storage; }
    [[nodiscard]] gsl::span<const unsigned char> Bytes() const noexcept { return _storage; }

    [[nodiscard]] std::size_t      Count() const noexcept { return _count; }
    [[nodiscard]] std::size_t      Stride() const noexcept { return _stride; }
    [[nodiscard]] std::size_t      SizeBytes() const noexcept { return _storage.size(); }
    [[nodiscard]] bool             Empty() const noexcept { return _count == 0; }
    [[nodiscard]] std::string_view Name() const noexcept { return _name; }

private:
    std::string            _name;
    std::size_t            _stride{0};
    std::size_t            _count{0};
    std::vector<unsigned char> _storage;
};

template<typename T>
gsl::span<T> FrameBuffer::As() {
    Expects(sizeof(T) == _stride);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<T*>(_storage.data()), _count};
}

template<typename T>
gsl::span<const T> FrameBuffer::As() const {
    Expects(sizeof(T) == _stride);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const T*>(_storage.data()), _count};
}

} // namespace Restir
