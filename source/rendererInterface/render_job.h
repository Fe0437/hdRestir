#pragma once

namespace Restir {

// Abstract cancellation interface for render jobs.
// Replaces dependency on HdRenderThread* to decouple the engine from Hydra.
// The Hydra integration layer provides the concrete adapter wrapping HdRenderThread.
class IRenderJob {
public:
    IRenderJob() = default;
    virtual ~IRenderJob() = default;

    // Returns true if rendering should stop.
    [[nodiscard]] virtual bool IsCancelled() const noexcept = 0;
};

// No-op implementation for non-interactive queries.
// Used when rendering is not happening (e.g., during IntersectScene calls).
class MainThreadRenderJob final : public IRenderJob {
public:
    [[nodiscard]] bool IsCancelled() const noexcept override {
        return false;
    }
};

// Global instance for use outside render loop.
inline const MainThreadRenderJob& GetMainThreadRenderJob() noexcept {
    static const MainThreadRenderJob instance{};
    return instance;
}

}  // namespace Restir
