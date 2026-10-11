#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace px::desktop {

// SDL emits live-resize expose events from Windows' nested modal message loop.
// The registration owns its callback only while Run is active, on the UI thread.
class LiveResizeRefresh final {
public:
    static std::expected<LiveResizeRefresh, std::string> Create(std::uint32_t window_id, std::function<void()> refresh);
    LiveResizeRefresh(LiveResizeRefresh&&) noexcept;
    LiveResizeRefresh& operator=(LiveResizeRefresh&&) noexcept;
    ~LiveResizeRefresh();
    LiveResizeRefresh(const LiveResizeRefresh&) = delete;
    LiveResizeRefresh& operator=(const LiveResizeRefresh&) = delete;
    void Reset() noexcept;

private:
    struct State;
    explicit LiveResizeRefresh(std::shared_ptr<State> state) noexcept;
    std::shared_ptr<State> state_{};
};

}  // namespace px::desktop
