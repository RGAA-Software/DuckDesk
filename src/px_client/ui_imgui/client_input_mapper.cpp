#include "client_input_mapper.h"

#include <SDL3/SDL_keycode.h>
#include <Windows.h>

#include <algorithm>
#include <cctype>

namespace px::client::imgui {

bool ContainsNonAscii(const std::string_view text) noexcept {
    return std::ranges::any_of(text, [](const unsigned char value) { return value >= 0x80U; });
}

std::uint32_t WindowsVirtualKey(const std::int32_t sdlKey) noexcept {
    const auto key = static_cast<SDL_Keycode>(sdlKey);
    if (key >= SDLK_A && key <= SDLK_Z)
        return static_cast<std::uint32_t>('A' + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9)
        return static_cast<std::uint32_t>('0' + (key - SDLK_0));
    switch (key) {
    case SDLK_RETURN:
        return VK_RETURN;
    case SDLK_ESCAPE:
        return VK_ESCAPE;
    case SDLK_BACKSPACE:
        return VK_BACK;
    case SDLK_TAB:
        return VK_TAB;
    case SDLK_SPACE:
        return VK_SPACE;
    case SDLK_DELETE:
        return VK_DELETE;
    case SDLK_INSERT:
        return VK_INSERT;
    case SDLK_HOME:
        return VK_HOME;
    case SDLK_END:
        return VK_END;
    case SDLK_PAGEUP:
        return VK_PRIOR;
    case SDLK_PAGEDOWN:
        return VK_NEXT;
    case SDLK_LEFT:
        return VK_LEFT;
    case SDLK_RIGHT:
        return VK_RIGHT;
    case SDLK_UP:
        return VK_UP;
    case SDLK_DOWN:
        return VK_DOWN;
    case SDLK_LCTRL:
        return VK_LCONTROL;
    case SDLK_RCTRL:
        return VK_RCONTROL;
    case SDLK_LSHIFT:
        return VK_LSHIFT;
    case SDLK_RSHIFT:
        return VK_RSHIFT;
    case SDLK_LALT:
        return VK_LMENU;
    case SDLK_RALT:
        return VK_RMENU;
    case SDLK_LGUI:
        return VK_LWIN;
    case SDLK_RGUI:
        return VK_RWIN;
    case SDLK_CAPSLOCK:
        return VK_CAPITAL;
    case SDLK_NUMLOCKCLEAR:
        return VK_NUMLOCK;
    case SDLK_SCROLLLOCK:
        return VK_SCROLL;
    case SDLK_PRINTSCREEN:
        return VK_SNAPSHOT;
    case SDLK_PAUSE:
        return VK_PAUSE;
    case SDLK_APPLICATION:
        return VK_APPS;
    case SDLK_SEMICOLON:
        return VK_OEM_1;
    case SDLK_EQUALS:
        return VK_OEM_PLUS;
    case SDLK_COMMA:
        return VK_OEM_COMMA;
    case SDLK_MINUS:
        return VK_OEM_MINUS;
    case SDLK_PERIOD:
        return VK_OEM_PERIOD;
    case SDLK_SLASH:
        return VK_OEM_2;
    case SDLK_GRAVE:
        return VK_OEM_3;
    case SDLK_LEFTBRACKET:
        return VK_OEM_4;
    case SDLK_BACKSLASH:
        return VK_OEM_5;
    case SDLK_RIGHTBRACKET:
        return VK_OEM_6;
    case SDLK_APOSTROPHE:
        return VK_OEM_7;
    case SDLK_KP_0:
        return VK_NUMPAD0;
    case SDLK_KP_1:
    case SDLK_KP_2:
    case SDLK_KP_3:
    case SDLK_KP_4:
    case SDLK_KP_5:
    case SDLK_KP_6:
    case SDLK_KP_7:
    case SDLK_KP_8:
    case SDLK_KP_9:
        return VK_NUMPAD1 + static_cast<std::uint32_t>(key - SDLK_KP_1);
    case SDLK_KP_MULTIPLY:
        return VK_MULTIPLY;
    case SDLK_KP_PLUS:
        return VK_ADD;
    case SDLK_KP_MINUS:
        return VK_SUBTRACT;
    case SDLK_KP_PERIOD:
        return VK_DECIMAL;
    case SDLK_KP_DIVIDE:
        return VK_DIVIDE;
    case SDLK_KP_ENTER:
        return VK_RETURN;
    case SDLK_F1:
        return VK_F1;
    case SDLK_F2:
        return VK_F2;
    case SDLK_F3:
        return VK_F3;
    case SDLK_F4:
        return VK_F4;
    case SDLK_F5:
        return VK_F5;
    case SDLK_F6:
        return VK_F6;
    case SDLK_F7:
        return VK_F7;
    case SDLK_F8:
        return VK_F8;
    case SDLK_F9:
        return VK_F9;
    case SDLK_F10:
        return VK_F10;
    case SDLK_F11:
        return VK_F11;
    case SDLK_F12:
        return VK_F12;
    default:
        return key > 0 && key <= 0xFF ? static_cast<std::uint32_t>(std::toupper(static_cast<unsigned char>(key))) : 0U;
    }
}

} // namespace px::client::imgui
