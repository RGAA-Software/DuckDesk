#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <optional>
#include <thread>

#include "../live_resize_refresh.h"

namespace {
void Expose(std::uint32_t window_id, int live_resize = 1) {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_EXPOSED;
    event.window.windowID = window_id;
    event.window.data1 = live_resize;
    static_cast<void>(SDL_PushEvent(&event));
}
}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    const auto refreshes = std::make_shared<int>();
    for (int cycle_index{}; cycle_index < 3; ++cycle_index) {
        auto registration = px::desktop::LiveResizeRefresh::Create(42, [refreshes] {
            ++*refreshes;
            Expose(42);  // A nested refresh must not recurse into a second frame.
        });
        if (!registration) return 2;
        Expose(41);
        Expose(42, 0);
        std::thread worker{[] { Expose(42); }};
        worker.join();
        if (*refreshes != cycle_index) return 3;
        Expose(42);
        if (*refreshes != cycle_index + 1) return 4;
        registration->Reset();
        registration->Reset();
        Expose(42);
        if (*refreshes != cycle_index + 1) return 5;
    }

    const auto registration_owner = std::make_shared<std::optional<px::desktop::LiveResizeRefresh>>();
    auto registration = px::desktop::LiveResizeRefresh::Create(42, [refreshes, owner = std::weak_ptr{registration_owner}] {
        ++*refreshes;
        if (const auto active = owner.lock()) active->reset();  // Unregister and destroy during dispatch.
    });
    if (!registration) return 6;
    registration_owner->emplace(std::move(*registration));
    Expose(42);
    if (*registration_owner || *refreshes != 4) return 7;
    Expose(42);
    if (*refreshes != 4) return 8;
    SDL_Event queued_event{};
    while (SDL_PollEvent(&queued_event)) {
    }
    SDL_Quit();
    std::cout << "Live resize refresh, main-thread filtering, reentrancy, shutdown during dispatch and repeated registration passed\n";
    return 0;
}
