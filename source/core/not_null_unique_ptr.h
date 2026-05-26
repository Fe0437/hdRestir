#pragma once

#include <gsl/gsl>

#include <memory>

namespace Restir {

template <typename T>
class NotNullUniquePtr {
public:
    template <typename>
    friend class NotNullUniquePtr;

    explicit NotNullUniquePtr(std::unique_ptr<T>&& ptr)
        : _ptr{std::move(ptr)}
    {
        Expects(_ptr != nullptr);
    }

    template <typename U>
    requires std::is_convertible_v<U*, T*>
    NotNullUniquePtr(NotNullUniquePtr<U>&& other) noexcept
        : _ptr{std::move(other._ptr)}
    {}

    NotNullUniquePtr(NotNullUniquePtr&&) noexcept = default;
    NotNullUniquePtr& operator=(NotNullUniquePtr&&) noexcept = default;

    NotNullUniquePtr(const NotNullUniquePtr&) = delete;
    NotNullUniquePtr& operator=(const NotNullUniquePtr&) = delete;

    [[nodiscard]] T* get() const noexcept { return _ptr.get(); }
    [[nodiscard]] T& operator*() const noexcept { return *_ptr; }
    [[nodiscard]] T* operator->() const noexcept { return _ptr.get(); }

private:
    std::unique_ptr<T> _ptr;
};

}  // namespace Restir
