#ifndef PX_STREAM_RESOURCE_REFRESH_GATE_H
#define PX_STREAM_RESOURCE_REFRESH_GATE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace px {

// Protects asynchronous Console catalog refreshes from overlap, identity
// changes, and late completion after the owning UI has been destroyed.
class StreamResourceRefreshGate final {
public:
    static std::shared_ptr<StreamResourceRefreshGate> Create();

    // A superseding request invalidates any older in-flight completion. This is
    // used when the Console identity changes; periodic/manual refreshes simply
    // coalesce while another request is active.
    [[nodiscard]] std::optional<std::uint64_t> Begin(bool supersede = false);
    [[nodiscard]] bool RunIfCurrent(
        std::uint64_t generation, std::function<void()> operation);
    [[nodiscard]] bool Complete(std::uint64_t generation);
    void Stop();

private:
    mutable std::mutex mutex_;
    std::mutex execution_mutex_;
    std::uint64_t generation_ = 0;
    bool active_ = false;
    bool stopped_ = false;
};

} // namespace px

#endif // PX_STREAM_RESOURCE_REFRESH_GATE_H
