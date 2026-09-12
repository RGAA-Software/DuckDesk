#include "client_toolbar.h"

#include "client_file_transfer_panel.h"
#include "client_session.h"
#include "client_text.h"
#include "px_desktop_shell/desktop_shell.h"

#include "px_common/log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace px::client::imgui {
namespace {

constexpr ImGuiWindowFlags kOverlayFlags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing};

ClientText StatusText(const ClientConnectionState state) noexcept {
    switch (state) {
    case ClientConnectionState::Connecting:
        return ClientText::Connecting;
    case ClientConnectionState::Connected:
        return ClientText::Connected;
    case ClientConnectionState::MediaUnavailable:
        return ClientText::MediaUnavailable;
    case ClientConnectionState::Rejected:
        return ClientText::Rejected;
    case ClientConnectionState::Disconnected:
        return ClientText::Disconnected;
    }
    return ClientText::Disconnected;
}

float ClampMenuY(const ImGuiViewport& viewport, const float desired, const float estimatedHeight) noexcept {
    const float minimum{viewport.WorkPos.y + ImGui::GetFontSize()};
    const float maximum{viewport.WorkPos.y + viewport.WorkSize.y - estimatedHeight - ImGui::GetFontSize()};
    return std::clamp(desired, minimum, std::max(minimum, maximum));
}

} // namespace

ClientToolbar::ClientToolbar(std::shared_ptr<ClientFileTransferPanel> fileTransfer) : fileTransfer_{std::move(fileTransfer)} {}

bool ClientToolbar::Bounds::Contains(const float pointX, const float pointY) const noexcept {
    return width > 0.0F && height > 0.0F && pointX >= x && pointY >= y && pointX < x + width && pointY < y + height;
}

bool ClientToolbar::CapturesPointer(const float x, const float y) const noexcept {
    return launcherBounds_.Contains(x, y) || navigationBounds_.Contains(x, y) || sectionBounds_.Contains(x, y);
}

bool ClientToolbar::HandlePointerEvent(const px::desktop::DesktopInputEvent& event) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.mouseButton == SDL_BUTTON_LEFT) {
        if (launcherBounds_.Contains(event.x, event.y)) {
            launcherPointerDown_ = true;
            launcherDragged_ = false;
            dragOriginX_ = event.x;
            dragOriginY_ = event.y;
            dragOriginLauncherX_ = launcherX_;
            dragOriginLauncherY_ = launcherY_;
            LOGI("Client input route: floating controller press x={:.1f} y={:.1f}", event.x, event.y);
            return true;
        }
        if (expanded_ && !navigationBounds_.Contains(event.x, event.y) && !sectionBounds_.Contains(event.x, event.y))
            expanded_ = false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && launcherPointerDown_) {
        const float deltaX{event.x - dragOriginX_};
        const float deltaY{event.y - dragOriginY_};
        launcherDragged_ = launcherDragged_ || deltaX * deltaX + deltaY * deltaY >= 16.0F;
        if (launcherDragged_) {
            launcherX_ = std::clamp(dragOriginLauncherX_ + deltaX, workX_, std::max(workX_, workX_ + workWidth_ - launcherDiameter_));
            launcherY_ = std::clamp(dragOriginLauncherY_ + deltaY, workY_, std::max(workY_, workY_ + workHeight_ - launcherDiameter_));
        }
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.mouseButton == SDL_BUTTON_LEFT && launcherPointerDown_) {
        if (!launcherDragged_) {
            expanded_ = !expanded_;
            LOGI("Client input route: floating controller click, menu_open={}", expanded_);
        } else {
            LOGI("Client input route: floating controller moved x={:.1f} y={:.1f}", launcherX_, launcherY_);
        }
        launcherPointerDown_ = false;
        launcherDragged_ = false;
        return true;
    }
    return launcherPointerDown_ || CapturesPointer(event.x, event.y);
}

ClientToolbarAction ClientToolbar::Draw(const std::shared_ptr<ClientSession>& session, const bool english, const bool darkTheme) {
    ClientToolbarAction action{};
    bool hovered{DrawLauncher()};
    if (expanded_) {
        const auto snapshot = session->Snapshot();
        hovered = DrawNavigation(snapshot, english) || hovered;
        hovered = DrawSection(session, snapshot, english, darkTheme, action) || hovered;
    } else {
        navigationBounds_ = {};
        sectionBounds_ = {};
    }
    return action;
}

bool ClientToolbar::DrawLauncher() {
    const auto& viewport = *ImGui::GetMainViewport();
    const float fontSize{ImGui::GetFontSize()};
    const float diameter{fontSize * 2.8F};
    workX_ = viewport.WorkPos.x;
    workY_ = viewport.WorkPos.y;
    workWidth_ = viewport.WorkSize.x;
    workHeight_ = viewport.WorkSize.y;
    launcherDiameter_ = diameter;
    if (!launcherPositionInitialized_) {
        launcherX_ = viewport.WorkPos.x + viewport.WorkSize.x - diameter - fontSize;
        launcherY_ = viewport.WorkPos.y + (viewport.WorkSize.y - diameter) * 0.5F;
        launcherPositionInitialized_ = true;
        LOGI("Client floating controller initialized x={:.1f} y={:.1f} viewport=({:.1f},{:.1f},{:.1f},{:.1f})", launcherX_, launcherY_, workX_,
             workY_, workWidth_, workHeight_);
    }
    launcherX_ = std::clamp(launcherX_, workX_, std::max(workX_, workX_ + workWidth_ - diameter));
    launcherY_ = std::clamp(launcherY_, workY_, std::max(workY_, workY_ + workHeight_ - diameter));
    const ImVec2 center{launcherX_ + diameter * 0.5F, launcherY_ + diameter * 0.5F};
    const ImU32 color{ImGui::GetColorU32(launcherPointerDown_ ? ImVec4{0.08F, 0.30F, 0.72F, 1.00F} : ImVec4{0.10F, 0.38F, 0.86F, 0.94F})};
    ImGui::GetForegroundDrawList()->AddCircleFilled(center, diameter * 0.5F, color, 48);
    const ImVec2 textSize{ImGui::CalcTextSize("P")};
    ImGui::GetForegroundDrawList()->AddText({launcherX_ + (diameter - textSize.x) * 0.5F, launcherY_ + (diameter - textSize.y) * 0.5F},
                                            ImGui::GetColorU32(ImVec4{1.0F, 1.0F, 1.0F, 1.0F}), "P");
    launcherBounds_ = {.x = launcherX_, .y = launcherY_, .width = diameter, .height = diameter};
    const ImVec2 mouse{ImGui::GetIO().MousePos};
    const bool hovered{launcherBounds_.Contains(mouse.x, mouse.y)};
    if (hovered)
        ImGui::SetTooltip("Pixels");
    return hovered || launcherPointerDown_;
}

bool ClientToolbar::DrawNavigation(const ClientSessionSnapshot& snapshot, const bool english) {
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto& viewport = *ImGui::GetMainViewport();
    const float fontSize{ImGui::GetFontSize()};
    const float menuWidth{fontSize * 13.0F};
    const float menuHeight{fontSize * 18.0F};
    const float spacing{fontSize * 0.6F};
    const bool openLeft{launcherBounds_.x + launcherBounds_.width * 0.5F > viewport.WorkPos.x + viewport.WorkSize.x * 0.5F};
    const float navigationX{openLeft ? launcherBounds_.x - menuWidth - spacing : launcherBounds_.x + launcherBounds_.width + spacing};
    const float y{ClampMenuY(viewport, launcherBounds_.y + launcherBounds_.height * 0.5F - menuHeight * 0.5F, menuHeight)};
    ImGui::SetNextWindowPos({navigationX, y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({menuWidth, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96F);
    ImGui::Begin("##pixels-controller-navigation", {}, kOverlayFlags);
    ImGui::TextUnformatted("Pixels");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text(StatusText(snapshot.state)));
    ImGui::Separator();

    auto& selectedSection = section_;
    const auto navigationItem = [&selectedSection](const std::string_view label, const Section section, const bool enabled = true) {
        ImGui::BeginDisabled(!enabled);
        const bool selected{ImGui::Selectable(label.data(), selectedSection == section)};
        if (enabled && (selected || ImGui::IsItemHovered()))
            selectedSection = section;
        ImGui::EndDisabled();
    };
    navigationItem(english ? "Display  >" : "显示  >", Section::Display);
    navigationItem(english ? "Control  >" : "控制  >", Section::Control);
    navigationItem(english ? "Tools  >" : "工具  >", Section::Tools);
    navigationItem(english ? "Voice  >" : "语音  >", Section::Voice, snapshot.voiceAvailable);
    navigationItem(english ? "Settings  >" : "设置  >", Section::Settings);

    ImGui::Separator();
    ImGui::TextDisabled("FPS %d  %d ms", snapshot.framesPerSecond, snapshot.latencyMilliseconds);
    const ImVec2 windowPosition{ImGui::GetWindowPos()};
    const ImVec2 windowSize{ImGui::GetWindowSize()};
    navigationBounds_ = {.x = windowPosition.x, .y = windowPosition.y, .width = windowSize.x, .height = windowSize.y};
    const bool hovered{ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)};
    ImGui::End();
    return hovered;
}

bool ClientToolbar::DrawSection(const std::shared_ptr<ClientSession>& session, const ClientSessionSnapshot& snapshot, const bool english,
                                const bool darkTheme, ClientToolbarAction& action) {
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto& viewport = *ImGui::GetMainViewport();
    const float fontSize{ImGui::GetFontSize()};
    const float navigationWidth{fontSize * 13.0F};
    const float sectionWidth{fontSize * 18.0F};
    const float menuHeight{fontSize * 18.0F};
    const float spacing{fontSize * 0.4F};
    const bool openLeft{launcherBounds_.x + launcherBounds_.width * 0.5F > viewport.WorkPos.x + viewport.WorkSize.x * 0.5F};
    const float navigationX{openLeft ? launcherBounds_.x - navigationWidth - fontSize * 0.6F
                                     : launcherBounds_.x + launcherBounds_.width + fontSize * 0.6F};
    const float sectionX{openLeft ? navigationX - sectionWidth - spacing : navigationX + navigationWidth + spacing};
    const float y{ClampMenuY(viewport, launcherBounds_.y + launcherBounds_.height * 0.5F - menuHeight * 0.5F, menuHeight)};
    ImGui::SetNextWindowPos({sectionX, y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({sectionWidth, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96F);
    ImGui::Begin("##pixels-controller-section", {}, kOverlayFlags);

    if (section_ == Section::Display) {
        ImGui::TextUnformatted(english ? "Display" : "显示");
        ImGui::Separator();
        if (!snapshot.monitorName.empty()) {
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##monitor", snapshot.monitorName.c_str())) {
                for (const auto& monitor : snapshot.monitors) {
                    if (ImGui::Selectable(monitor.c_str(), monitor == snapshot.monitorName))
                        static_cast<void>(session->SwitchMonitor(monitor));
                }
                ImGui::EndCombo();
            }
        }
        if (!snapshot.resolutions.empty()) {
            const auto label = std::to_string(resolutionWidth_) + "x" + std::to_string(resolutionHeight_);
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##resolution", resolutionWidth_ > 0 ? label.c_str() : text(ClientText::Resolution))) {
                for (const auto& resolution : snapshot.resolutions) {
                    const auto value = std::to_string(resolution.width) + "x" + std::to_string(resolution.height);
                    if (ImGui::Selectable(value.c_str(), resolution.width == resolutionWidth_ && resolution.height == resolutionHeight_)) {
                        resolutionWidth_ = resolution.width;
                        resolutionHeight_ = resolution.height;
                        static_cast<void>(session->ChangeResolution(resolution.width, resolution.height));
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::SliderInt("FPS", &frameRate_, 15, 120))
            static_cast<void>(session->SetFrameRate(frameRate_));
        if (ImGui::Checkbox(text(ClientText::Audio), &audioEnabled_))
            static_cast<void>(session->SetAudioEnabled(audioEnabled_));
        if (ImGui::Button(text(ClientText::Fullscreen), {-1.0F, 0.0F}))
            action.toggleFullscreen = true;
        if (snapshot.virtualDisplayAvailable) {
            ImGui::Separator();
            ImGui::TextDisabled("%s %u/%u", text(ClientText::VirtualDisplays), snapshot.virtualDisplayCount, snapshot.virtualDisplayMaximum);
            ImGui::BeginDisabled(snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount >= snapshot.virtualDisplayMaximum);
            if (ImGui::Button("+##virtual-display", {fontSize * 3.0F, 0.0F}))
                static_cast<void>(session->CreateVirtualDisplay());
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount == 0U);
            if (ImGui::Button("-##virtual-display", {fontSize * 3.0F, 0.0F}))
                static_cast<void>(session->RemoveVirtualDisplay());
            ImGui::EndDisabled();
        }
    } else if (section_ == Section::Control) {
        ImGui::TextUnformatted(english ? "Control" : "控制");
        ImGui::Separator();
        if (ImGui::Button(text(ClientText::SecureAttention), {-1.0F, 0.0F}))
            static_cast<void>(session->SendSecureAttention());
        ImGui::TextWrapped("%s", english ? "Mouse, keyboard, wheel, text input and clipboard follow the remote session."
                                         : "鼠标、键盘、滚轮、文本输入和剪贴板随远程会话工作。");
    } else if (section_ == Section::Tools) {
        ImGui::TextUnformatted(english ? "Tools" : "工具");
        ImGui::Separator();
        ImGui::BeginDisabled(!snapshot.fileTransferAvailable);
        if (ImGui::Button(text(ClientText::Files), {-1.0F, 0.0F}))
            fileTransfer_->Open();
        ImGui::EndDisabled();
        if (ImGui::Button(text(ClientText::Screenshot), {-1.0F, 0.0F}))
            screenshotStatus_ = session->SaveScreenshot().value_or(text(ClientText::ScreenshotFailed));
        if (snapshot.recording) {
            if (ImGui::Button(text(ClientText::StopRecording), {-1.0F, 0.0F}))
                static_cast<void>(session->StopRecording());
        } else if (ImGui::Button(text(ClientText::Record), {-1.0F, 0.0F})) {
            static_cast<void>(session->StartRecording());
        }
        ImGui::Checkbox(text(ClientText::Statistics), &showStatistics_);
        if (showStatistics_) {
            ImGui::TextWrapped("FPS %d | %d ms | %d Kbps | %s", snapshot.framesPerSecond, snapshot.latencyMilliseconds, snapshot.bitrateKbps,
                               snapshot.decoder.c_str());
        }
        if (!screenshotStatus_.empty())
            ImGui::TextWrapped("%s", screenshotStatus_.c_str());
    } else if (section_ == Section::Voice) {
        ImGui::TextUnformatted(english ? "Voice" : "语音");
        ImGui::Separator();
        if (snapshot.voiceStatus == "Connected" || snapshot.voiceStatus == "Calling") {
            if (ImGui::Button(text(ClientText::HangUp), {-1.0F, 0.0F}))
                static_cast<void>(session->StopVoiceCall());
        } else if (ImGui::Button(text(ClientText::Voice), {-1.0F, 0.0F})) {
            static_cast<void>(session->StartVoiceCall());
        }
        if (ImGui::Checkbox(text(ClientText::MuteMicrophone), &microphoneMuted_))
            static_cast<void>(session->SetVoiceMicrophoneMuted(microphoneMuted_));
        if (ImGui::Checkbox(text(ClientText::MuteSpeaker), &speakerMuted_))
            static_cast<void>(session->SetVoiceSpeakerMuted(speakerMuted_));
    } else {
        ImGui::TextUnformatted(english ? "Settings" : "设置");
        ImGui::Separator();
        if (ImGui::Button(english ? "简体中文" : "English", {-1.0F, 0.0F}))
            action.toggleLanguage = true;
        if (ImGui::Button(text(darkTheme ? ClientText::Light : ClientText::Dark), {-1.0F, 0.0F}))
            action.toggleTheme = true;
    }

    const ImVec2 windowPosition{ImGui::GetWindowPos()};
    const ImVec2 windowSize{ImGui::GetWindowSize()};
    sectionBounds_ = {.x = windowPosition.x, .y = windowPosition.y, .width = windowSize.x, .height = windowSize.y};
    const bool hovered{ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)};
    ImGui::End();
    return hovered;
}

} // namespace px::client::imgui
