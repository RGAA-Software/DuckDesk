#include "live_resize_refresh.h"

#include <SDL3/SDL.h>

#include <exception>
#include <utility>

#include "px_common/log.h"

namespace px::desktop {

struct LiveResizeRefresh::State final {
    std::weak_ptr<State> observer{};
    std::uint32_t window_id{};
    std::function<void()> refresh{};
    bool registered{};
    bool dispatching{};

    static bool SDLCALL OnEvent(void* context, SDL_Event* event) {         // NOLINT(pixels-raw-pointer-boundary): borrowed SDL event-watch ABI.
        const auto state = static_cast<State*>(context)->observer.lock();  // NOLINT(pixels-raw-pointer-boundary): transient SDL context view.
        if (!state || !SDL_IsMainThread() || !state->registered || state->dispatching || event->type != SDL_EVENT_WINDOW_EXPOSED ||
            event->window.windowID != state->window_id || event->window.data1 != 1) {
            return true;
        }
        // data1=1 is emitted by this pinned SDL's SDL_OnWindowLiveResizeUpdate.
        // Ordinary expose events and decoder-thread event pushes do not draw here.
        state->dispatching = true;
        try {
            state->refresh();
        } catch (const std::exception& failure) {
            LOGE("event=video.live_resize phase=failed reason={}", failure.what());
        } catch (...) {
            LOGE("event=video.live_resize phase=failed reason=unknown_exception");
        }
        state->dispatching = false;
        return true;
    }
};

std::expected<LiveResizeRefresh, std::string> LiveResizeRefresh::Create(std::uint32_t window_id, std::function<void()> refresh) {
    if (!window_id || !refresh || !SDL_IsMainThread()) return std::unexpected{"Invalid live-resize refresh registration"};
    auto state = std::make_shared<State>();
    state->observer = state;
    state->window_id = window_id;
    state->refresh = std::move(refresh);
    state->registered = SDL_AddEventWatch(State::OnEvent, state.get());  // NOLINT(pixels-raw-pointer-boundary): SDL borrows until Reset.
    if (!state->registered) return std::unexpected{std::string{SDL_GetError()}};
    return LiveResizeRefresh{std::move(state)};
}

LiveResizeRefresh::LiveResizeRefresh(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
LiveResizeRefresh::LiveResizeRefresh(LiveResizeRefresh&&) noexcept = default;
LiveResizeRefresh& LiveResizeRefresh::operator=(LiveResizeRefresh&& other) noexcept {
    if (this != &other) {
        Reset();
        state_ = std::move(other.state_);
    }
    return *this;
}
LiveResizeRefresh::~LiveResizeRefresh() { Reset(); }
void LiveResizeRefresh::Reset() noexcept {
    if (state_ && std::exchange(state_->registered, false)) {
        SDL_RemoveEventWatch(State::OnEvent, state_.get());  // NOLINT(pixels-raw-pointer-boundary): unregister before releasing owned context.
    }
    state_.reset();
}

}  // namespace px::desktop
