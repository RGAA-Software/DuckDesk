#include "client_window.h"
#include "client_file_transfer_panel.h"
#include "client_input_mapper.h"
#include "client_text.h"
#include "client_toolbar.h"
#include "px_common/log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <functional>
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
    if (snapshot.frame && snapshot.frame != uploadedFrame_) {
        const bool uploaded{snapshot.frame->native
                                ? shell_.get().UpdateVideoFrame(snapshot.frame->native)
                                : shell_.get().UpdateVideoTexture(snapshot.frame->width, snapshot.frame->height, snapshot.frame->bgra)};
        if (uploaded)
            uploadedFrame_ = snapshot.frame;
    }
    const auto toolbarAction = toolbar_->Draw(session_, english_, darkTheme_);
    if (toolbarAction.toggleLanguage)
        english_ = !english_;
    if (toolbarAction.toggleTheme) {
        darkTheme_ = !darkTheme_;
        static_cast<void>(shell_.get().SetTheme(darkTheme_ ? px::ui::Theme::Dark : px::ui::Theme::Light));
    }
    if (toolbarAction.toggleFullscreen)
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
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        ReleasePressedInput();
        localPointerButtons_.fill(false);
        textCompositionActive_ = false;
        return;
    }
    if (event.type == SDL_EVENT_TEXT_EDITING) {
        textCompositionActive_ = true;
        return;
    }
    const auto& io = ImGui::GetIO();
    const float pointerX{event.type == SDL_EVENT_MOUSE_WHEEL ? io.MousePos.x : event.x};
    const float pointerY{event.type == SDL_EVENT_MOUSE_WHEEL ? io.MousePos.y : event.y};
    const bool popupOpen{ImGui::IsPopupOpen({}, ImGuiPopupFlags_AnyPopupId)};
    const bool mouseEvent{event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                          event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL};
    const bool toolbarCaptured{mouseEvent && toolbar_->HandlePointerEvent(event)};
    const bool overLocalUi{toolbarCaptured || fileTransfer_->CapturesPointer(pointerX, pointerY) || popupOpen};
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && overLocalUi && event.mouseButton < localPointerButtons_.size())
        localPointerButtons_[event.mouseButton] = true;
    const bool localPointerGesture{std::ranges::any_of(localPointerButtons_, std::identity{})};
    const bool mouseCaptured{overLocalUi || localPointerGesture};
    const bool keyboardCaptured{fileTransfer_->CapturesKeyboard() || popupOpen};
    if (!mouseCaptured && event.type == SDL_EVENT_MOUSE_MOTION && InVideo(event.x, event.y)) {
        lastMouseXRatio_ = (event.x - videoLeft_) / videoWidth_;
        lastMouseYRatio_ = (event.y - videoTop_) / videoHeight_;
        const bool sent{session_->SendMouseMove(lastMouseXRatio_, lastMouseYRatio_)};
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextMouseRouteLog_) {
            const auto snapshot = session_->Snapshot();
            LOGI("Client input route: mouse move local=({:.1f},{:.1f}) remote=({:.3f},{:.3f}) sent={} state={} monitor=[{}]", event.x, event.y,
                 lastMouseXRatio_, lastMouseYRatio_, sent, static_cast<int>(snapshot.state), snapshot.monitorName);
            nextMouseRouteLog_ = now + std::chrono::milliseconds{500};
        }
    } else if (!mouseCaptured && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
               InVideo(event.x, event.y)) {
        lastMouseXRatio_ = (event.x - videoLeft_) / videoWidth_;
        lastMouseYRatio_ = (event.y - videoTop_) / videoHeight_;
        const bool down{event.type == SDL_EVENT_MOUSE_BUTTON_DOWN};
        if (event.mouseButton < pressedMouseButtons_.size())
            pressedMouseButtons_[event.mouseButton] = down;
        const bool sent{session_->SendMouseButton(event.mouseButton, down, lastMouseXRatio_, lastMouseYRatio_)};
        const auto snapshot = session_->Snapshot();
        LOGI("Client input route: mouse button={} down={} remote=({:.3f},{:.3f}) sent={} state={} monitor=[{}]", event.mouseButton, down,
             lastMouseXRatio_, lastMouseYRatio_, sent, static_cast<int>(snapshot.state), snapshot.monitorName);
    } else if (!mouseCaptured && event.type == SDL_EVENT_MOUSE_WHEEL && InVideo(io.MousePos.x, io.MousePos.y)) {
        const bool sent{session_->SendMouseWheel(event.wheelX, event.wheelY)};
        LOGI("Client input route: wheel horizontal={:.1f} vertical={:.1f} sent={}", event.wheelX, event.wheelY, sent);
    } else if (!keyboardCaptured && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)) {
        if (const auto key = WindowsVirtualKey(event.key); key != 0U) {
            const bool down{event.type == SDL_EVENT_KEY_DOWN};
            if (down)
                pressedKeys_.insert(key);
            else
                pressedKeys_.erase(key);
            const bool sent{session_->SendKey(key, down)};
            LOGI("Client input route: key={} down={} sent={}", key, down, sent);
        }
    } else if (!keyboardCaptured && event.type == SDL_EVENT_TEXT_INPUT && !event.text.empty()) {
        if (textCompositionActive_ || ContainsNonAscii(event.text)) {
            const bool sent{session_->SendText(event.text)};
            LOGI("Client input route: committed text bytes={} sent={}", event.text.size(), sent);
        }
        textCompositionActive_ = false;
    } else if (mouseEvent && mouseCaptured && event.type != SDL_EVENT_MOUSE_MOTION) {
        LOGI("Client input route: local UI captured type={} x={:.1f} y={:.1f} toolbar={} popup={}", event.type, pointerX, pointerY, toolbarCaptured,
             popupOpen);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.mouseButton < localPointerButtons_.size())
        localPointerButtons_[event.mouseButton] = false;
}

void ClientWindow::ReleasePressedInput() {
    for (const auto key : pressedKeys_)
        static_cast<void>(session_->SendKey(key, false));
    pressedKeys_.clear();
    for (std::size_t button = 1; button < pressedMouseButtons_.size(); ++button) {
        if (pressedMouseButtons_[button])
            static_cast<void>(session_->SendMouseButton(static_cast<std::uint8_t>(button), false, lastMouseXRatio_, lastMouseYRatio_));
    }
    pressedMouseButtons_.fill(false);
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

bool ClientWindow::InVideo(const float x, const float y) const noexcept {
    return videoWidth_ > 0.0F && videoHeight_ > 0.0F && x >= videoLeft_ && y >= videoTop_ && x < videoLeft_ + videoWidth_ &&
           y < videoTop_ + videoHeight_;
}

} // namespace px::client::imgui
