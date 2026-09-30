#include <SDL3/SDL.h>
#include <Windows.h>

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

#include "../title_bar.h"
#include "../window_host.h"

namespace {

bool ExerciseCaptionClick(const HWND nativeWindow, const bool expectedMaximized) {
    RECT bounds{};
    if (!GetClientRect(nativeWindow, &bounds)) return false;
    const UINT dpi{GetDpiForWindow(nativeWindow)};
    const int buttonWidth{MulDiv(px::desktop::kCaptionButtonLogicalWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
    POINT clickPosition{bounds.right - buttonWidth * 3 / 2, buttonWidth / 2};
    if (!ClientToScreen(nativeWindow, &clickPosition)) return false;
    SetCursorPos(clickPosition.x, clickPosition.y);
    std::array<INPUT, 2> click{};
    click[0].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].type = INPUT_MOUSE;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(static_cast<UINT>(click.size()), click.data(), sizeof(INPUT)) != click.size()) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{400};
    do {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < deadline);
    return (IsZoomed(nativeWindow) != FALSE) == expectedMaximized;
}

bool ExerciseMaximize() {
    auto creation = px::desktop::WindowHost::Create("Pixels maximize regression", 960, 640, true, false, 900, 600, {});
    if (!creation) return false;
    auto window = std::move(creation.value());
    // Borrowed Win32 handle; the SDL WindowHost remains its owner for the entire exercise.
    const HWND nativeWindow{
        reinterpret_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(&window.Native()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))};
    if (!nativeWindow) return false;
    window.ShowAndRaise();
    SDL_PumpEvents();
    const bool maximized{ExerciseCaptionClick(nativeWindow, true)};
    if (!maximized) {
        std::cerr << "Caption click did not maximize the window\n";
        return false;
    }
    const bool restored{ExerciseCaptionClick(nativeWindow, false)};
    if (!restored) std::cerr << "Caption click did not restore the window\n";
    return restored;
}

}  // namespace

int main() {
    POINT initialCursor{};
    GetCursorPos(&initialCursor);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    const bool succeeded{ExerciseMaximize()};
    SDL_Quit();
    SetCursorPos(initialCursor.x, initialCursor.y);
    return succeeded ? 0 : 1;
}
