#pragma once

#include <Windows.h>
#include <cstdint>
#include <array>

namespace px {
// A value-only envelope, not a pointer to queued storage. RegisterWindowMessage
// supplies a collision-free process message identity and needs no unregister.
[[nodiscard]] inline UINT QueuedKeyboardMessage() {
    static const UINT message{RegisterWindowMessageW(L"GammaRay.OwnedGame.KeyboardState.v1")};
    return message;
}

struct WindowKeyboardSnapshot final {
    std::uint32_t key{};
    bool down{};
    bool shift{};
    bool control{};
    bool alt{};
    bool caps{};

    [[nodiscard]] constexpr WPARAM Pack() const noexcept {
        return (key & 0xffU) | (static_cast<WPARAM>(down) << 8) | (static_cast<WPARAM>(shift) << 9) | (static_cast<WPARAM>(control) << 10) |
               (static_cast<WPARAM>(alt) << 11) | (static_cast<WPARAM>(caps) << 12);
    }
    [[nodiscard]] static constexpr WindowKeyboardSnapshot Unpack(WPARAM value) noexcept {
        return {static_cast<std::uint32_t>(value & 0xffU),
                (value & (1U << 8)) != 0,
                (value & (1U << 9)) != 0,
                (value & (1U << 10)) != 0,
                (value & (1U << 11)) != 0,
                (value & (1U << 12)) != 0};
    }
};

// SetKeyboardState affects only this GUI thread. Restore the exact original
// state after synchronous dispatch, including on early return or nested work.
class ScopedWindowKeyboardState final {
  public:
    explicit ScopedWindowKeyboardState(const WindowKeyboardSnapshot& snapshot) {
        if (!GetKeyboardState(previous_.data())) {
            return;
        }
        auto state = previous_;
        for (const auto key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT}) {
            state[key] = snapshot.shift ? 0x80 : 0;
        }
        for (const auto key : {VK_CONTROL, VK_LCONTROL, VK_RCONTROL}) {
            state[key] = snapshot.control ? 0x80 : 0;
        }
        // Right Alt is not invented: Godot interprets it as AltGr.
        state[VK_MENU] = snapshot.alt ? 0x80 : 0;
        state[VK_LMENU] = snapshot.alt && snapshot.key != VK_RMENU ? 0x80 : 0;
        state[VK_RMENU] = snapshot.alt && snapshot.key == VK_RMENU ? 0x80 : 0;
        state[VK_CAPITAL] = snapshot.caps ? 1 : 0;
        active_ = SetKeyboardState(state.data()) != FALSE;
    }
    ~ScopedWindowKeyboardState() {
        if (active_) {
            SetKeyboardState(previous_.data());
        }
    }
    ScopedWindowKeyboardState(const ScopedWindowKeyboardState&) = delete;
    ScopedWindowKeyboardState& operator=(const ScopedWindowKeyboardState&) = delete;
    [[nodiscard]] bool Active() const noexcept {
        return active_;
    }

  private:
    std::array<BYTE, 256> previous_{};
    bool active_{};
};

// WM_KEYDOWN/UP use generic modifier VKs. The scan code and extended bit
// retain the side; Raw Input and the protocol retain their original key.
[[nodiscard]] constexpr std::uint32_t WindowMessageKey(std::uint32_t key) noexcept {
    switch (key) {
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    default:
        return key;
    }
}
} // namespace px
