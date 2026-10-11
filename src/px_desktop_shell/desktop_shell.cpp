#include "px_desktop_shell/desktop_shell.h"

#include "desktop_renderer.h"
#include "frame_refresh_signal.h"
#include "imgui_session.h"
#include "live_resize_refresh.h"
#include "px_common/log.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/product_brand.h"
#include "title_bar.h"
#include "window_host.h"
#include "windows_title_bar_behavior.h"

#if PX_DESKTOP_SHELL_MEDIA
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#endif

#include <SDL3/SDL.h>
#include <Windows.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace px::desktop {
namespace {

constexpr Uint32 kShowWindowEvent{SDL_EVENT_USER + 41};
constexpr Uint32 kExitApplicationEvent{SDL_EVENT_USER + 42};
constexpr Uint32 kVideoFrameReadyEvent{SDL_EVENT_USER + 43};

BOOL CALLBACK RestoreCurrentProcessWindow(HWND window, LPARAM) {  // NOLINT(pixels-raw-pointer-boundary): Win32 enumeration callback ABI.
    DWORD ownerProcessId{};
    static_cast<void>(GetWindowThreadProcessId(window, &ownerProcessId));
    std::array<wchar_t, 64> className{};
    static_cast<void>(GetClassNameW(window, className.data(), static_cast<int>(className.size())));
    if (ownerProcessId != GetCurrentProcessId() || GetWindow(window, GW_OWNER) != nullptr || std::wstring_view{className.data()} != L"SDL_app")
        return TRUE;
    static_cast<void>(PostMessageW(window, kShowAndRaiseWindowMessage, 0, 0));
    return FALSE;
}

struct SdlTrayDeleter final {
    void operator()(SDL_Tray* tray) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): SDL owned handle boundary
        SDL_DestroyTray(tray);
    }
};

using SdlTray = std::unique_ptr<SDL_Tray, SdlTrayDeleter>;

struct SdlSurfaceDeleter final {
    void operator()(SDL_Surface* surface) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): SDL owned handle boundary
        SDL_DestroySurface(surface);
    }
};

using SdlSurface = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter>;

void SDLCALL OnTrayEntry(void*, SDL_TrayEntry* entry) {  // NOLINT(pixels-raw-pointer-boundary): SDL callback ABI
    const std::string_view label{SDL_GetTrayEntryLabel(entry)};
    SDL_Event event{};
    event.type = label == "Exit " + std::string{px::ui::ApplicationName()} ? kExitApplicationEvent : kShowWindowEvent;
    SDL_PushEvent(&event);
}

SdlTray CreateTray() {
    const std::string basePath{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path iconPath{std::filesystem::path{basePath} / "resources" / "icons" / "brand" / "px_icon.png"};
    const std::string applicationName{px::ui::ApplicationName()};
    const std::string showLabel{"Show " + applicationName};
    const std::string exitLabel{"Exit " + applicationName};
    SdlSurface icon{SDL_LoadPNG(iconPath.string().c_str())};
    SdlTray tray{SDL_CreateTray(icon.get(), applicationName.c_str())};
    if (!tray || !SDL_CreateTrayMenu(tray.get())) {
        return {};
    }
    SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(SDL_GetTrayMenu(tray.get()), -1, showLabel.c_str(), SDL_TRAYENTRY_BUTTON), OnTrayEntry, nullptr);
    SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(SDL_GetTrayMenu(tray.get()), -1, exitLabel.c_str(), SDL_TRAYENTRY_BUTTON), OnTrayEntry, nullptr);
    return tray;
}

}  // namespace

struct DesktopShell::Impl final {
    std::shared_ptr<FrameRefreshSignal> videoRefreshSignal{std::make_shared<FrameRefreshSignal>()};
    Impl(std::unique_ptr<WindowHost> windowValue, DesktopRenderer rendererValue, WindowChromeConfig chromeValue, std::string titleBarTitleValue,
         const bool minimizeToTrayValue, const bool continuousTextInputValue, const bool continuousRenderingValue, const bool edgeToEdgeContentValue)
        : window{std::move(windowValue)},
          renderer{std::move(rendererValue)},
          minimizeToTray{minimizeToTrayValue},
          continuousTextInput{continuousTextInputValue},
          continuousRendering{continuousRenderingValue},
          edgeToEdgeContent{edgeToEdgeContentValue},
          chrome{chromeValue},
          titleBarTitle{std::move(titleBarTitleValue)} {}

    // Renderer references must remain valid when the shell moves into its final owner.
    std::unique_ptr<WindowHost> window;
    DesktopRenderer renderer;
    std::optional<ImGuiSession> imgui{};
    bool running{true};
    px::ui::Theme theme{px::ui::Theme::Dark};
    px::ui::Localizer localizer{};
    bool enhancedVisualEffects{true};
    SdlTray tray{};
    bool minimizeToTray{false};
    bool continuousTextInput{false};
    bool continuousRendering{false};
    bool edgeToEdgeContent{false};
    bool cancelCloseRequest{};
    WindowChromeConfig chrome{};
    std::string titleBarTitle{};
    bool drawing_frame{};
    int drawing_failure{};
    int renderer_pixel_width{};
    int renderer_pixel_height{};

    bool ResizeRenderer(int pixel_width, int pixel_height) {
        if (pixel_width <= 0 || pixel_height <= 0 || (pixel_width == renderer_pixel_width && pixel_height == renderer_pixel_height)) return true;
        if (!renderer.Resize(pixel_width, pixel_height)) return false;
        renderer_pixel_width = pixel_width;
        renderer_pixel_height = pixel_height;
        return true;
    }

    struct FrameDrawing final {
        bool interactive{};
        std::int64_t composition_us{};
        std::int64_t draw_us{};
    };

    std::expected<FrameDrawing, int> DrawFrame(const RenderCallback& render, bool refresh_size = false) {
        if (drawing_frame || !running || window->IsMinimized()) return FrameDrawing{};
        const auto composition_started = std::chrono::steady_clock::now();
        drawing_frame = true;
        struct DrawingGuard final {
            bool& drawing;
            ~DrawingGuard() { drawing = false; }
        };
        const DrawingGuard drawing_guard{drawing_frame};
        if (refresh_size) {
            int pixel_width{};
            int pixel_height{};
            if (!SDL_GetWindowSizeInPixels(&window->Native(), &pixel_width, &pixel_height) || !ResizeRenderer(pixel_width, pixel_height))
                return std::unexpected{2};
        }
        imgui->BeginFrame();
        if (continuousTextInput && !SDL_TextInputActive(&window->Native())) static_cast<void>(SDL_StartTextInput(&window->Native()));
        const ImGuiViewport& viewport = *ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport.Pos);
        ImGui::SetNextWindowSize(viewport.Size);
        const ImGuiWindowFlags root_flags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          (edgeToEdgeContent ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0)};
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, edgeToEdgeContent ? 0.0F : ImGui::GetStyle().WindowBorderSize);
        ImGui::Begin("PixelsRoot", nullptr, root_flags);
        ImGui::PopStyleVar(2);
        if (!DrawTitleBar(*window, chrome, imgui->Logo(), localizer, titleBarTitle)) {
            SDL_Event close_event{};
            close_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
            static_cast<void>(SDL_PushEvent(&close_event));
        }
        if (edgeToEdgeContent) {
            const float title_bar_height{window->IsFullscreen() ? 0.0F : px::ui::Scale(static_cast<float>(kTitleBarLogicalHeight))};
            ImGui::SetCursorScreenPos({viewport.Pos.x, viewport.Pos.y + title_bar_height});
        }
        render();
        ImGui::End();
        ImGui::PopStyleVar();
        const bool interactive_frame{imgui->NeedsInteractiveRefresh()};
        ImGui::Render();
        const auto composition_finished = std::chrono::steady_clock::now();
        if (!window->IsMinimized()) renderer.Render();
        const auto draw_finished = std::chrono::steady_clock::now();
        return FrameDrawing{
            .interactive = interactive_frame,
            .composition_us = std::chrono::duration_cast<std::chrono::microseconds>(composition_finished - composition_started).count(),
            .draw_us = std::chrono::duration_cast<std::chrono::microseconds>(draw_finished - composition_finished).count()};
    }
};

std::expected<DesktopShell, std::string> DesktopShell::Create(const WindowConfig& config) {
    const WindowChromeConfig chrome{.showMinimizeButton = config.showMinimizeButton,
                                    .showMaximizeButton = config.showMaximizeButton,
                                    .allowTitleBarMaximize = config.allowTitleBarMaximize,
                                    .useRoundedWindow = config.useRoundedWindow,
                                    .resizable = config.resizable};
    auto windowResult = WindowHost::Create(config.title, config.width, config.height, config.initiallyVisible, config.preferVulkanVideo,
                                           config.minimumWidth, config.minimumHeight, chrome);
    if (!windowResult) {
        return std::unexpected{windowResult.error()};
    }
    auto window = std::make_unique<WindowHost>(std::move(windowResult.value()));
    auto rendererResult = DesktopRenderer::Create(*window, config.preferVulkanVideo);
    if (!rendererResult) {
        return std::unexpected{rendererResult.error()};
    }

    auto impl = std::make_shared<Impl>(std::move(window), std::move(rendererResult.value()), chrome, config.titleBarTitle, config.minimizeToTray,
                                       config.continuousTextInput, config.continuousRendering, config.edgeToEdgeContent);
    if (config.minimizeToTray) {
        impl->tray = CreateTray();
    }
    auto imguiResult = ImGuiSession::Create(*impl->window, impl->renderer);
    if (!imguiResult) {
        return std::unexpected{imguiResult.error()};
    }
    impl->imgui.emplace(std::move(imguiResult.value()));
    static_cast<void>(SDL_GetWindowSizeInPixels(&impl->window->Native(), &impl->renderer_pixel_width, &impl->renderer_pixel_height));
    if (impl->continuousTextInput) static_cast<void>(SDL_StartTextInput(&impl->window->Native()));
    return DesktopShell{std::move(impl)};
}

DesktopShell::DesktopShell(std::shared_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
DesktopShell::DesktopShell(DesktopShell&&) noexcept = default;
DesktopShell& DesktopShell::operator=(DesktopShell&&) noexcept = default;
DesktopShell::~DesktopShell() = default;

DesktopShell::RenderCallback DesktopShell::VideoFrameReadyCallback() const {
    return [signal = std::weak_ptr{impl_->videoRefreshSignal}] {
        const auto refresh = signal.lock();
        if (!refresh) return;
        refresh->Request([] {
            SDL_Event event{};
            event.type = kVideoFrameReadyEvent;
            return SDL_PushEvent(&event);
        });
    };
}

int DesktopShell::Run(const RenderCallback& render, const InputCallback& input) {
    auto live_resize = LiveResizeRefresh::Create(SDL_GetWindowID(&impl_->window->Native()), [shell = std::weak_ptr{impl_}, render] {
        const auto active = shell.lock();
        if (!active) return;
        if (const auto result = active->DrawFrame(render, true); !result) {
            active->drawing_failure = result.error();
            active->running = false;
        }
    });
    if (!live_resize) {
        LOGE("Live-resize registration failed: {}", live_resize.error());
        return 2;
    }
    bool firstFrame{true};
    bool interactiveFrame{false};
    std::optional<SDL_WindowFlags> previous_visibility{};
    std::optional<std::chrono::steady_clock::time_point> previous_iteration{};
    while (impl_->running) {
        const auto loop_started = std::chrono::steady_clock::now();
        if (previous_iteration && loop_started - *previous_iteration >= std::chrono::milliseconds(100)) {
            LOGI("event=video.ui_schedule gap_us={}",
                 std::chrono::duration_cast<std::chrono::microseconds>(loop_started - *previous_iteration).count());
        }
        previous_iteration = loop_started;
        std::chrono::steady_clock::time_point wait_finished{};
        std::uint64_t processed_events{};
        SDL_Event event{};
        const int waitMilliseconds{firstFrame ? 0 : (impl_->continuousRendering || interactiveFrame ? 16 : 200)};
        if (SDL_WaitEventTimeout(&event, waitMilliseconds)) {
            wait_finished = std::chrono::steady_clock::now();
            do {
                ++processed_events;
                const auto event_started = std::chrono::steady_clock::now();
                if (event.type == kVideoFrameReadyEvent) {
                    impl_->videoRefreshSignal->Consume();
                    continue;
                }
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (input) {
                    DesktopInputEvent translated{.type = event.type};
                    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                        translated.key = event.key.key;
                        translated.scanCode = event.key.scancode;
                        translated.platformScanCode = event.key.raw;
                    } else if (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING) {
                        translated.text = event.text.text;
                    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        translated.x = event.motion.x;
                        translated.y = event.motion.y;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        translated.mouseButton = event.button.button;
                        translated.x = event.button.x;
                        translated.y = event.button.y;
                    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                        translated.wheelX = event.wheel.x;
                        translated.wheelY = event.wheel.y;
                    } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {  // NOLINT(pixels-raw-pointer-boundary): SDL event ABI
                        translated.text = event.drop.data;
                    }
                    input(translated);
                }
                if (event.type == kShowWindowEvent) {
                    impl_->window->ShowAndRaise();
                } else if (event.type == kExitApplicationEvent || event.type == SDL_EVENT_QUIT) {
                    if (impl_->cancelCloseRequest)
                        impl_->cancelCloseRequest = false;
                    else
                        impl_->running = false;
                } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    if (impl_->cancelCloseRequest) {
                        impl_->cancelCloseRequest = false;
                    } else if (impl_->minimizeToTray) {
                        impl_->window->Hide();
                    } else {
                        impl_->running = false;
                    }
                }
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && !impl_->ResizeRenderer(event.window.data1, event.window.data2)) {
                    return 2;
                }
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                    if (!impl_->imgui->ApplyAppearance(impl_->theme, impl_->window->DisplayScale(), impl_->enhancedVisualEffects)) {
                        return 3;
                    }
                }
                const auto event_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - event_started).count();
                if (event_us >= 50000) LOGI("event=video.ui_event phase=slow type={} duration_us={}", event.type, event_us);
            } while (SDL_PollEvent(&event));
        } else {
            wait_finished = std::chrono::steady_clock::now();
        }
        const auto events_finished = std::chrono::steady_clock::now();
        if (!impl_->running) {
            break;
        }
        const auto visibility = SDL_GetWindowFlags(&impl_->window->Native()) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED);
        if (!previous_visibility || *previous_visibility != visibility) {
            LOGI("event=video.window_render flags={} minimized={} hidden={} occluded={}", visibility,
                 (visibility & SDL_WINDOW_MINIMIZED) != 0, (visibility & SDL_WINDOW_HIDDEN) != 0, (visibility & SDL_WINDOW_OCCLUDED) != 0);
            previous_visibility = visibility;
        }
        if ((visibility & SDL_WINDOW_MINIMIZED) != 0) {
            firstFrame = false;
            interactiveFrame = false;
            continue;
        }

        const auto drawing = impl_->DrawFrame(render);
        if (!drawing) return drawing.error();
        interactiveFrame = drawing->interactive;
        const auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(wait_finished - loop_started).count();
        const auto events_us = std::chrono::duration_cast<std::chrono::microseconds>(events_finished - wait_finished).count();
        const auto composition_us = drawing->composition_us;
        const auto draw_us = drawing->draw_us;
        if (wait_us > (waitMilliseconds + 50) * 1000 || events_us >= 50000 || composition_us >= 50000 || draw_us >= 50000) {
            LOGI("event=video.ui_loop phase=slow wait_us={} allowed_wait_ms={} events={} events_us={} composition_us={} draw_us={}", wait_us,
                 waitMilliseconds, processed_events, events_us, composition_us, draw_us);
        }
        firstFrame = false;
    }
    return impl_->drawing_failure;
}

bool DesktopShell::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
    return impl_->renderer.UpdateVideoTexture(width, height, bgra);
}

bool DesktopShell::UpdateVideoFrame(const std::shared_ptr<RawImage>& image) {
    const auto upload_started = std::chrono::steady_clock::now();
    const bool uploaded = impl_->renderer.UpdateVideoFrame(image);
    const auto upload_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - upload_started).count();
    if (upload_us >= 50000) LOGI("event=video.upload phase=slow duration_us={} uploaded={}", upload_us, uploaded);
    return uploaded;
}

std::uint64_t DesktopShell::VideoTextureId() const noexcept { return impl_->renderer.VideoTextureId(); }

std::shared_ptr<WindowsVideoResources> DesktopShell::VideoResources(const std::string& decoderPreference) {
    return impl_->renderer.VideoResources(decoderPreference);
}

const PlatformIconAtlas& DesktopShell::PlatformIcons() const noexcept { return impl_->imgui->PlatformIcons(); }

const BrandLogo& DesktopShell::Logo() const noexcept { return impl_->imgui->Logo(); }

bool DesktopShell::SetTheme(const px::ui::Theme theme) {
    impl_->theme = theme;
    return impl_->imgui->ApplyAppearance(theme, impl_->window->DisplayScale(), impl_->enhancedVisualEffects);
}

void DesktopShell::SetLanguage(const px::ui::Language language) { impl_->localizer.SetLanguage(language); }

bool DesktopShell::SetEnhancedVisualEffects(const bool enabled) {
    impl_->enhancedVisualEffects = enabled;
    return impl_->imgui->ApplyAppearance(impl_->theme, impl_->window->DisplayScale(), enabled);
}

bool DesktopShell::ToggleFullscreen() { return impl_->window->ToggleFullscreen(); }
bool DesktopShell::IsFullscreen() const noexcept { return impl_->window->IsFullscreen(); }

void DesktopShell::RequestExit() noexcept { impl_->running = false; }

void DesktopShell::CancelCloseRequest() noexcept { impl_->cancelCloseRequest = true; }

void DesktopShell::RequestShowAndRaise() noexcept { PostShowAndRaiseRequest(); }

void DesktopShell::PostShowAndRaiseRequest() noexcept { static_cast<void>(EnumWindows(RestoreCurrentProcessWindow, 0)); }

}  // namespace px::desktop
