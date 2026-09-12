#include "client_window.h"
#include "client_file_transfer_panel.h"
#include "client_text.h"
#include "client_toolbar.h"

#include <Windows.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace px::client::imgui {
namespace {

ClientText FailureText(const ClientConnectionFailure failure) noexcept {
    switch (failure) {
    case ClientConnectionFailure::Authorization:
        return ClientText::AuthorizationRejected;
    case ClientConnectionFailure::Occupied:
        return ClientText::DeviceOccupied;
    case ClientConnectionFailure::SessionPolicy:
        return ClientText::SessionPolicyRejected;
    case ClientConnectionFailure::TakenOver:
        return ClientText::SessionTakenOver;
    case ClientConnectionFailure::Transport:
        return ClientText::TransportRejected;
    case ClientConnectionFailure::None:
    default:
        return ClientText::ConnectionRejected;
    }
}

} // namespace

ClientWindow::ClientWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session, const bool english)
    : shell_{shell}, session_{std::move(session)}, fileTransfer_{std::make_shared<ClientFileTransferPanel>()},
      toolbar_{std::make_unique<ClientToolbar>(fileTransfer_)}, english_{english} {}

ClientWindow::~ClientWindow() = default;

void ClientWindow::Draw() {
    const bool english = english_;
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    SynchronizeClipboard();
    const auto snapshot = session_->Snapshot();
    if (terminalErrorShown_ && (snapshot.state == ClientConnectionState::Connected || snapshot.state == ClientConnectionState::MediaUnavailable)) {
        terminalErrorShown_ = false;
        terminalErrorPopupOpened_ = false;
    }
    if (!windowVisible_ &&
        (snapshot.frame || snapshot.state == ClientConnectionState::Connected || snapshot.state == ClientConnectionState::MediaUnavailable)) {
        windowVisible_ = true;
        shell_.get().RequestShowAndRaise();
    }
    if (!terminalErrorShown_ && snapshot.state == ClientConnectionState::Rejected) {
        terminalErrorShown_ = true;
        windowVisible_ = true;
        shell_.get().RequestShowAndRaise();
    }
    if (snapshot.frame && snapshot.frame != uploadedFrame_ &&
        shell_.get().UpdateVideoTexture(snapshot.frame->width, snapshot.frame->height, snapshot.frame->bgra)) {
        uploadedFrame_ = snapshot.frame;
    }
    toolbar_->Draw(session_, english_);
    ImGui::SameLine();
    if (ImGui::SmallButton(english_ ? "中文" : "EN"))
        english_ = !english_;
    ImGui::SameLine();
    if (ImGui::SmallButton(text(darkTheme_ ? ClientText::Light : ClientText::Dark))) {
        darkTheme_ = !darkTheme_;
        static_cast<void>(shell_.get().SetTheme(darkTheme_ ? px::ui::Theme::Dark : px::ui::Theme::Light));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(ClientText::Fullscreen)))
        static_cast<void>(shell_.get().ToggleFullscreen());
    fileTransfer_->Draw(session_, english_);

    if (terminalErrorShown_) {
        const std::string popupTitle{std::string{text(ClientText::ConnectionFailed)} + "###client-rejected"};
        if (!terminalErrorPopupOpened_) {
            ImGui::OpenPopup(popupTitle.c_str());
            terminalErrorPopupOpened_ = true;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5F, 0.5F});
        ImGui::SetNextWindowSize({540.0F, 0.0F}, ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings};
        if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, flags)) {
            ImGui::TextWrapped("%s", text(FailureText(snapshot.failure)));
            if (snapshot.failure == ClientConnectionFailure::None && !snapshot.status.empty()) {
                ImGui::TextWrapped("%s", snapshot.status.c_str());
            }
            ImGui::Spacing();
            constexpr float buttonWidth{150.0F};
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F);
            if (ImGui::Button(text(ClientText::Ok), {buttonWidth, 0.0F}))
                shell_.get().RequestExit();
            ImGui::EndPopup();
        }
        return;
    }

    if (snapshot.state == ClientConnectionState::MediaUnavailable) {
        ImGui::TextColored({0.95F, 0.65F, 0.20F, 1.0F}, "%s", text(ClientText::MediaUnavailableDetail));
    } else if (snapshot.state == ClientConnectionState::Disconnected) {
        ImGui::TextColored({0.95F, 0.65F, 0.20F, 1.0F}, "%s", text(ClientText::DisconnectedDetail));
    }

    const ImVec2 available{ImGui::GetContentRegionAvail()};
    const ImVec2 position{ImGui::GetCursorScreenPos()};
    if (!uploadedFrame_ || shell_.get().VideoTextureId() == 0U) {
        videoLeft_ = position.x;
        videoTop_ = position.y;
        videoWidth_ = available.x;
        videoHeight_ = available.y;
        ImGui::SetCursorScreenPos({position.x + available.x * 0.35F, position.y + available.y * 0.45F});
        ImGui::TextDisabled("%s", text(ClientText::WaitingForFrame));
        return;
    }
    const float frameAspect{static_cast<float>(uploadedFrame_->width) / static_cast<float>(uploadedFrame_->height)};
    float width{available.x};
    float height{width / frameAspect};
    if (height > available.y) {
        height = available.y;
        width = height * frameAspect;
    }
    videoLeft_ = position.x + (available.x - width) * 0.5F;
    videoTop_ = position.y + (available.y - height) * 0.5F;
    videoWidth_ = width;
    videoHeight_ = height;
    ImGui::SetCursorScreenPos({videoLeft_, videoTop_});
    ImGui::Image(ImTextureRef{static_cast<ImTextureID>(shell_.get().VideoTextureId())}, {width, height});
}

void ClientWindow::HandleInput(const px::desktop::DesktopInputEvent& event) {
    const auto& io = ImGui::GetIO();
    const bool mouseCaptured = io.WantCaptureMouse;
    const bool keyboardCaptured = io.WantCaptureKeyboard || io.WantTextInput;
    if (!mouseCaptured && event.type == SDL_EVENT_MOUSE_MOTION && InVideo(event.x, event.y)) {
        static_cast<void>(session_->SendMouseMove((event.x - videoLeft_) / videoWidth_, (event.y - videoTop_) / videoHeight_));
    } else if (!mouseCaptured && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
               InVideo(event.x, event.y)) {
        static_cast<void>(session_->SendMouseButton(event.mouseButton, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                                                    (event.x - videoLeft_) / videoWidth_, (event.y - videoTop_) / videoHeight_));
    } else if (!mouseCaptured && event.type == SDL_EVENT_MOUSE_WHEEL && InVideo(io.MousePos.x, io.MousePos.y)) {
        static_cast<void>(session_->SendMouseWheel(event.wheelX, event.wheelY));
    } else if (!keyboardCaptured && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)) {
        if (const auto key = VirtualKey(event); key != 0U) {
            static_cast<void>(session_->SendKey(key, event.type == SDL_EVENT_KEY_DOWN));
        }
    } else if (!keyboardCaptured && event.type == SDL_EVENT_TEXT_INPUT && !event.text.empty()) {
        static_cast<void>(session_->SendText(event.text));
    }
}

namespace {
struct SdlTextDeleter final {
    void operator()(char* value) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): SDL-owned text ABI.
        SDL_free(value);
    }
};
} // namespace

void ClientWindow::SynchronizeClipboard() {
    if (const auto remote = session_->TakeRemoteClipboardText()) {
        if (SDL_SetClipboardText(remote->c_str()))
            clipboardText_ = *remote;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < nextClipboardCheck_)
        return;
    nextClipboardCheck_ = now + std::chrono::milliseconds{250};
    if (!SDL_HasClipboardText())
        return;
    const std::unique_ptr<char, SdlTextDeleter> value{SDL_GetClipboardText()};
    if (!value || clipboardText_ == value.get())
        return;
    clipboardText_ = value.get();
    static_cast<void>(session_->SendClipboardText(clipboardText_));
}

std::uint32_t ClientWindow::VirtualKey(const px::desktop::DesktopInputEvent& event) const {
    const auto key = static_cast<SDL_Keycode>(event.key);
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
    case SDLK_RCTRL:
        return VK_CONTROL;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return VK_SHIFT;
    case SDLK_LALT:
    case SDLK_RALT:
        return VK_MENU;
    case SDLK_LGUI:
    case SDLK_RGUI:
        return VK_LWIN;
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

bool ClientWindow::InVideo(const float x, const float y) const noexcept {
    return videoWidth_ > 0.0F && videoHeight_ > 0.0F && x >= videoLeft_ && y >= videoTop_ && x < videoLeft_ + videoWidth_ &&
           y < videoTop_ + videoHeight_;
}

} // namespace px::client::imgui
