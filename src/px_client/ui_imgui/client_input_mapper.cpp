#include "client_input_mapper.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <Windows.h>

#include <algorithm>
#include <cctype>

namespace px::client::imgui {
namespace {

std::uint32_t VirtualKeyFromScanCode(const SDL_Scancode scanCode) noexcept {
    if (scanCode >= SDL_SCANCODE_A && scanCode <= SDL_SCANCODE_Z)
        return static_cast<std::uint32_t>('A' + (scanCode - SDL_SCANCODE_A));
    if (scanCode >= SDL_SCANCODE_1 && scanCode <= SDL_SCANCODE_9)
        return static_cast<std::uint32_t>('1' + (scanCode - SDL_SCANCODE_1));
    if (scanCode >= SDL_SCANCODE_F1 && scanCode <= SDL_SCANCODE_F12)
        return VK_F1 + static_cast<std::uint32_t>(scanCode - SDL_SCANCODE_F1);
    if (scanCode >= SDL_SCANCODE_F13 && scanCode <= SDL_SCANCODE_F24)
        return VK_F13 + static_cast<std::uint32_t>(scanCode - SDL_SCANCODE_F13);
    if (scanCode >= SDL_SCANCODE_KP_1 && scanCode <= SDL_SCANCODE_KP_9)
        return VK_NUMPAD1 + static_cast<std::uint32_t>(scanCode - SDL_SCANCODE_KP_1);

    switch (scanCode) {
    case SDL_SCANCODE_0:
        return '0';
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_RETURN2:
        return VK_RETURN;
    case SDL_SCANCODE_ESCAPE:
        return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_KP_BACKSPACE:
        return VK_BACK;
    case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_KP_TAB:
        return VK_TAB;
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_KP_SPACE:
        return VK_SPACE;
    case SDL_SCANCODE_SEMICOLON:
        return VK_OEM_1;
    case SDL_SCANCODE_EQUALS:
    case SDL_SCANCODE_KP_EQUALS:
        return VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA:
        return VK_OEM_COMMA;
    case SDL_SCANCODE_MINUS:
        return VK_OEM_MINUS;
    case SDL_SCANCODE_PERIOD:
        return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH:
        return VK_OEM_2;
    case SDL_SCANCODE_GRAVE:
        return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET:
        return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH:
        return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET:
        return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE:
        return VK_OEM_7;
    case SDL_SCANCODE_NONUSBACKSLASH:
        return VK_OEM_102;
    case SDL_SCANCODE_CAPSLOCK:
        return VK_CAPITAL;
    case SDL_SCANCODE_PRINTSCREEN:
        return VK_SNAPSHOT;
    case SDL_SCANCODE_SCROLLLOCK:
        return VK_SCROLL;
    case SDL_SCANCODE_PAUSE:
        return VK_PAUSE;
    case SDL_SCANCODE_INSERT:
        return VK_INSERT;
    case SDL_SCANCODE_HOME:
        return VK_HOME;
    case SDL_SCANCODE_PAGEUP:
        return VK_PRIOR;
    case SDL_SCANCODE_DELETE:
        return VK_DELETE;
    case SDL_SCANCODE_END:
        return VK_END;
    case SDL_SCANCODE_PAGEDOWN:
        return VK_NEXT;
    case SDL_SCANCODE_RIGHT:
        return VK_RIGHT;
    case SDL_SCANCODE_LEFT:
        return VK_LEFT;
    case SDL_SCANCODE_DOWN:
        return VK_DOWN;
    case SDL_SCANCODE_UP:
        return VK_UP;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return VK_NUMLOCK;
    case SDL_SCANCODE_KP_DIVIDE:
        return VK_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY:
        return VK_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS:
        return VK_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS:
        return VK_ADD;
    case SDL_SCANCODE_KP_ENTER:
        return VK_RETURN;
    case SDL_SCANCODE_KP_0:
        return VK_NUMPAD0;
    case SDL_SCANCODE_KP_PERIOD:
    case SDL_SCANCODE_DECIMALSEPARATOR:
        return VK_DECIMAL;
    case SDL_SCANCODE_KP_COMMA:
    case SDL_SCANCODE_SEPARATOR:
        return VK_SEPARATOR;
    case SDL_SCANCODE_APPLICATION:
    case SDL_SCANCODE_MENU:
        return VK_APPS;
    case SDL_SCANCODE_EXECUTE:
        return VK_EXECUTE;
    case SDL_SCANCODE_HELP:
        return VK_HELP;
    case SDL_SCANCODE_SELECT:
        return VK_SELECT;
    case SDL_SCANCODE_STOP:
        return VK_CANCEL;
    case SDL_SCANCODE_CLEAR:
    case SDL_SCANCODE_KP_CLEAR:
    case SDL_SCANCODE_KP_CLEARENTRY:
        return VK_CLEAR;
    case SDL_SCANCODE_LCTRL:
        return VK_LCONTROL;
    case SDL_SCANCODE_LSHIFT:
        return VK_LSHIFT;
    case SDL_SCANCODE_LALT:
        return VK_LMENU;
    case SDL_SCANCODE_LGUI:
        return VK_LWIN;
    case SDL_SCANCODE_RCTRL:
        return VK_RCONTROL;
    case SDL_SCANCODE_RSHIFT:
        return VK_RSHIFT;
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_MODE:
        return VK_RMENU;
    case SDL_SCANCODE_RGUI:
        return VK_RWIN;
    case SDL_SCANCODE_SLEEP:
        return VK_SLEEP;
    case SDL_SCANCODE_MUTE:
        return VK_VOLUME_MUTE;
    case SDL_SCANCODE_VOLUMEUP:
        return VK_VOLUME_UP;
    case SDL_SCANCODE_VOLUMEDOWN:
        return VK_VOLUME_DOWN;
    case SDL_SCANCODE_MEDIA_NEXT_TRACK:
        return VK_MEDIA_NEXT_TRACK;
    case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK:
        return VK_MEDIA_PREV_TRACK;
    case SDL_SCANCODE_MEDIA_STOP:
        return VK_MEDIA_STOP;
    case SDL_SCANCODE_MEDIA_PLAY:
    case SDL_SCANCODE_MEDIA_PAUSE:
    case SDL_SCANCODE_MEDIA_PLAY_PAUSE:
        return VK_MEDIA_PLAY_PAUSE;
    case SDL_SCANCODE_MEDIA_SELECT:
        return VK_LAUNCH_MEDIA_SELECT;
    case SDL_SCANCODE_AC_SEARCH:
        return VK_BROWSER_SEARCH;
    case SDL_SCANCODE_AC_HOME:
        return VK_BROWSER_HOME;
    case SDL_SCANCODE_AC_BACK:
        return VK_BROWSER_BACK;
    case SDL_SCANCODE_AC_FORWARD:
        return VK_BROWSER_FORWARD;
    case SDL_SCANCODE_AC_STOP:
        return VK_BROWSER_STOP;
    case SDL_SCANCODE_AC_REFRESH:
        return VK_BROWSER_REFRESH;
    case SDL_SCANCODE_AC_BOOKMARKS:
        return VK_BROWSER_FAVORITES;
    case SDL_SCANCODE_INTERNATIONAL3:
        return VK_OEM_5;
    case SDL_SCANCODE_LANG1:
        return VK_HANGUL;
    case SDL_SCANCODE_LANG2:
        return VK_HANJA;
    case SDL_SCANCODE_LANG3:
        return VK_KANA;
    case SDL_SCANCODE_LANG5:
        return VK_KANJI;
    default:
        return 0U;
    }
}

std::uint32_t VirtualKeyFromLogicalKey(const SDL_Keycode key) noexcept {
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
    default:
        return key > 0 && key <= 0xFF ? static_cast<std::uint32_t>(std::toupper(static_cast<unsigned char>(key))) : 0U;
    }
}

} // namespace

bool ContainsNonAscii(const std::string_view text) noexcept {
    return std::ranges::any_of(text, [](const unsigned char value) { return value >= 0x80U; });
}

WindowsKey WindowsKeyFromSdl(const std::int32_t sdlKey, const std::int32_t sdlScanCode, const std::uint16_t platformScanCode) noexcept {
    const auto key = static_cast<SDL_Keycode>(sdlKey);
    auto scanCode = static_cast<SDL_Scancode>(sdlScanCode);
    if (scanCode == SDL_SCANCODE_UNKNOWN && (key & SDLK_SCANCODE_MASK) != 0U)
        scanCode = static_cast<SDL_Scancode>(key & ~SDLK_SCANCODE_MASK);
    std::uint32_t virtualKey{VirtualKeyFromScanCode(scanCode)};
    std::uint32_t nativeScanCode{platformScanCode};
    if (nativeScanCode != 0U) {
        const auto mappedVirtualKey = static_cast<std::uint32_t>(MapVirtualKeyW(nativeScanCode, MAPVK_VSC_TO_VK_EX));
        if (mappedVirtualKey != 0U)
            virtualKey = mappedVirtualKey;
    }

    // The physical SDL scancode is authoritative for modifier sides. Windows
    // can otherwise collapse synthetic modifier events to the generic VK.
    switch (scanCode) {
    case SDL_SCANCODE_LCTRL:
        virtualKey = VK_LCONTROL;
        break;
    case SDL_SCANCODE_RCTRL:
        virtualKey = VK_RCONTROL;
        break;
    case SDL_SCANCODE_LSHIFT:
        virtualKey = VK_LSHIFT;
        break;
    case SDL_SCANCODE_RSHIFT:
        virtualKey = VK_RSHIFT;
        break;
    case SDL_SCANCODE_LALT:
        virtualKey = VK_LMENU;
        break;
    case SDL_SCANCODE_RALT:
        virtualKey = VK_RMENU;
        break;
    case SDL_SCANCODE_LGUI:
        virtualKey = VK_LWIN;
        break;
    case SDL_SCANCODE_RGUI:
        virtualKey = VK_RWIN;
        break;
    case SDL_SCANCODE_PAUSE:
        virtualKey = VK_PAUSE;
        nativeScanCode = 0x45U;
        break;
    case SDL_SCANCODE_KP_ENTER:
        virtualKey = VK_RETURN;
        nativeScanCode = 0xE01CU;
        break;
    default:
        break;
    }

    if (virtualKey == 0U)
        virtualKey = VirtualKeyFromLogicalKey(key);
    if (nativeScanCode == 0U && virtualKey != 0U)
        nativeScanCode = static_cast<std::uint32_t>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX));
    return {.virtualKey = virtualKey, .scanCode = nativeScanCode};
}

std::uint32_t WindowsVirtualKey(const std::int32_t sdlKey, const std::int32_t sdlScanCode) noexcept {
    return WindowsKeyFromSdl(sdlKey, sdlScanCode).virtualKey;
}

} // namespace px::client::imgui
