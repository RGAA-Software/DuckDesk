#include "client_toolbar.h"

#include "client_file_transfer_panel.h"
#include "client_session.h"
#include "client_text.h"
#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/surface.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include "px_common/log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

ClientToolbar::ClientToolbar(std::shared_ptr<ClientFileTransferPanel> fileTransfer, const bool enhancedVisualEffects)
    : fileTransfer_{std::move(fileTransfer)}, enhancedVisualEffects_{enhancedVisualEffects} {}

bool ClientToolbar::Bounds::Contains(const float pointX, const float pointY) const noexcept {
    return width > 0.0F && height > 0.0F && pointX >= x && pointY >= y && pointX < x + width && pointY < y + height;
}

bool ClientToolbar::CapturesPointer(const float x, const float y) const noexcept {
    return launcherBounds_.Contains(x, y) || navigationBounds_.Contains(x, y) || sectionBounds_.Contains(x, y);
}

bool ClientToolbar::HandlePointerEvent(const px::desktop::DesktopInputEvent& event) {
    if (dismissPointerDown_) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.mouseButton == SDL_BUTTON_LEFT)
            dismissPointerDown_ = false;
        return true;
    }
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
        if (expanded_ && !navigationBounds_.Contains(event.x, event.y) && !sectionBounds_.Contains(event.x, event.y)) {
            expanded_ = false;
            sectionExpanded_ = false;
            navigationNeedsFocus_ = false;
            sectionNeedsFocus_ = false;
            dismissPointerDown_ = true;
            return true;
        }
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
        if (!launcherDragged_ && launcherBounds_.Contains(event.x, event.y)) {
            expanded_ = !expanded_;
            sectionExpanded_ = false;
            navigationNeedsFocus_ = expanded_;
            sectionNeedsFocus_ = false;
            LOGI("Client input route: floating controller click, menu_open={}", expanded_);
        } else if (launcherDragged_) {
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
        if (sectionExpanded_) {
            hovered = DrawSection(session, snapshot, english, darkTheme, action) || hovered;
        } else {
            sectionBounds_ = {};
        }
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
    launcherBounds_ = {.x = launcherX_, .y = launcherY_, .width = diameter, .height = diameter};
    const ImVec2 mouse{ImGui::GetIO().MousePos};
    const bool hovered{launcherBounds_.Contains(mouse.x, mouse.y)};
    const ImVec2 center{launcherX_ + diameter * 0.5F, launcherY_ + diameter * 0.5F};
    const float radius{diameter * 0.5F};
    // The controller must remain above the root video window even after the root receives focus.
    // Pointer routing is handled from SDL events, so a focusable ImGui overlay window is neither needed nor desirable here.
    ImDrawList& drawList{*ImGui::GetForegroundDrawList()};
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    if (px::ui::EnhancedVisualEffectsEnabled()) {
        drawList.AddCircleFilled(center, radius + 10.0F, ImGui::GetColorU32(ImVec4{0.0F, 0.0F, 0.0F, 0.05F}), 48);
        drawList.AddCircleFilled(center, radius + 7.0F, ImGui::GetColorU32(ImVec4{0.0F, 0.0F, 0.0F, 0.08F}), 48);
        drawList.AddCircleFilled(center, radius + 4.0F, ImGui::GetColorU32(ImVec4{0.0F, 0.0F, 0.0F, 0.12F}), 48);
    }
    const ImU32 color{ImGui::GetColorU32(launcherPointerDown_ ? tokens.ring : tokens.primary)};
    drawList.AddCircleFilled(center, radius, color, 48);
    const ImVec2 textSize{ImGui::CalcTextSize("P")};
    drawList.AddText({center.x - textSize.x * 0.5F, center.y - textSize.y * 0.5F}, ImGui::GetColorU32(tokens.primaryForeground), "P");
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
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    ImGui::SetNextWindowBgAlpha(px::ui::EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F);
    if (navigationNeedsFocus_) {
        ImGui::SetNextWindowFocus();
        navigationNeedsFocus_ = false;
    }
    const px::ui::ScopedStyleColor background{ImGuiCol_WindowBg, tokens.popover};
    const px::ui::ScopedStyleColor border{ImGuiCol_Border, tokens.border};
    const px::ui::ScopedStyleVar rounding{ImGuiStyleVar_WindowRounding, px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi).popupRadius};
    ImGui::Begin("##pixels-controller-navigation", {}, kOverlayFlags);
    ImGui::TextUnformatted("Pixels");
    ImGui::SameLine();
    px::ui::StatusBadge(text(StatusText(snapshot.state)),
                        snapshot.state == ClientConnectionState::Connected ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Secondary);
    px::ui::HorizontalSeparator();

    auto& selectedSection = section_;
    auto& sectionExpanded = sectionExpanded_;
    auto& sectionNeedsFocus = sectionNeedsFocus_;
    const auto navigationItem = [&selectedSection, &sectionExpanded, &sectionNeedsFocus](const std::string_view label, const Section section,
                                                                                         const bool enabled = true) {
        const px::ui::VectorIcon icon{section == Section::Display   ? px::ui::VectorIcon::Monitor
                                      : section == Section::Control ? px::ui::VectorIcon::Connect
                                      : section == Section::Tools   ? px::ui::VectorIcon::FileTransfer
                                      : section == Section::Voice   ? px::ui::VectorIcon::User
                                                                    : px::ui::VectorIcon::Settings};
        ImGui::BeginDisabled(!enabled);
        const bool selected{px::ui::NavigationItem({label}, icon, label, selectedSection == section, -1.0F)};
        if (enabled && (selected || ImGui::IsItemHovered())) {
            if (!sectionExpanded || selectedSection != section)
                sectionNeedsFocus = true;
            selectedSection = section;
            sectionExpanded = true;
        }
        ImGui::EndDisabled();
    };
    navigationItem(text(ClientText::Display), Section::Display);
    navigationItem(text(ClientText::Control), Section::Control);
    navigationItem(text(ClientText::Tools), Section::Tools);
    navigationItem(text(ClientText::Voice), Section::Voice, snapshot.voiceAvailable);
    navigationItem(text(ClientText::Settings), Section::Settings);

    px::ui::HorizontalSeparator();
    const std::string statistics{"FPS " + std::to_string(snapshot.framesPerSecond) + "  " + std::to_string(snapshot.latencyMilliseconds) + " ms"};
    px::ui::MutedText(statistics);
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
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    ImGui::SetNextWindowBgAlpha(px::ui::EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F);
    if (sectionNeedsFocus_) {
        ImGui::SetNextWindowFocus();
        sectionNeedsFocus_ = false;
    }
    const px::ui::ScopedStyleColor background{ImGuiCol_WindowBg, tokens.popover};
    const px::ui::ScopedStyleColor border{ImGuiCol_Border, tokens.border};
    const px::ui::ScopedStyleVar rounding{ImGuiStyleVar_WindowRounding, px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi).popupRadius};
    ImGui::Begin("##pixels-controller-section", {}, kOverlayFlags);

    if (section_ == Section::Display) {
        px::ui::SectionTitle(text(ClientText::Display));
        if (!snapshot.monitorName.empty()) {
            px::ui::FieldLabel(text(ClientText::Monitor));
            std::vector<px::ui::SelectOption> options{};
            options.reserve(snapshot.monitors.size());
            int selected{};
            for (std::size_t index{}; index < snapshot.monitors.size(); ++index) {
                options.push_back({static_cast<int>(index), snapshot.monitors[index]});
                if (snapshot.monitors[index] == snapshot.monitorName)
                    selected = static_cast<int>(index);
            }
            if (px::ui::SelectField({"client-monitor"}, selected, options) && selected >= 0 &&
                static_cast<std::size_t>(selected) < snapshot.monitors.size())
                static_cast<void>(session->SwitchMonitor(snapshot.monitors[static_cast<std::size_t>(selected)]));
        }
        if (!snapshot.resolutions.empty()) {
            px::ui::FieldLabel(text(ClientText::Resolution));
            std::vector<std::string> labels{};
            std::vector<px::ui::SelectOption> options{};
            labels.reserve(snapshot.resolutions.size());
            options.reserve(snapshot.resolutions.size());
            int selected{};
            for (std::size_t index{}; index < snapshot.resolutions.size(); ++index) {
                const auto& resolution = snapshot.resolutions[index];
                labels.push_back(std::to_string(resolution.width) + "x" + std::to_string(resolution.height));
                options.push_back({static_cast<int>(index), labels.back()});
                if (resolution.width == resolutionWidth_ && resolution.height == resolutionHeight_)
                    selected = static_cast<int>(index);
            }
            if (px::ui::SelectField({"client-resolution"}, selected, options) && selected >= 0 &&
                static_cast<std::size_t>(selected) < snapshot.resolutions.size()) {
                const auto& resolution = snapshot.resolutions[static_cast<std::size_t>(selected)];
                resolutionWidth_ = resolution.width;
                resolutionHeight_ = resolution.height;
                static_cast<void>(session->ChangeResolution(resolution.width, resolution.height));
            }
        }
        px::ui::FieldLabel(text(ClientText::FrameRate));
        if (px::ui::SliderIntField({"client-frame-rate"}, frameRate_, 15, 120))
            static_cast<void>(session->SetFrameRate(frameRate_));
        if (px::ui::ToggleSwitch({"client-audio"}, text(ClientText::Audio), audioEnabled_))
            static_cast<void>(session->SetAudioEnabled(audioEnabled_));
        if (px::ui::ActionButton({"client-fullscreen"}, text(ClientText::Fullscreen), {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F}))
            action.toggleFullscreen = true;
        if (snapshot.virtualDisplayAvailable) {
            px::ui::HorizontalSeparator();
            const std::string displayCount{std::string{text(ClientText::VirtualDisplays)} + " " + std::to_string(snapshot.virtualDisplayCount) + "/" +
                                           std::to_string(snapshot.virtualDisplayMaximum)};
            px::ui::MutedText(displayCount);
            if (px::ui::ActionButton({"virtual-display-add"}, "+",
                                     {.variant = px::ui::ButtonVariant::Outline,
                                      .width = fontSize * 3.0F,
                                      .disabled = snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount >= snapshot.virtualDisplayMaximum}))
                static_cast<void>(session->CreateVirtualDisplay());
            ImGui::SameLine();
            if (px::ui::ActionButton({"virtual-display-remove"}, "-",
                                     {.variant = px::ui::ButtonVariant::Outline,
                                      .width = fontSize * 3.0F,
                                      .disabled = snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount == 0U}))
                static_cast<void>(session->RemoveVirtualDisplay());
        }
    } else if (section_ == Section::Control) {
        px::ui::SectionTitle(text(ClientText::Control));
        if (px::ui::ActionButton({"client-secure-attention"}, text(ClientText::SecureAttention), {.width = -1.0F}))
            static_cast<void>(session->SendSecureAttention());
        px::ui::FieldDescription(text(ClientText::ControlDescription));
    } else if (section_ == Section::Tools) {
        px::ui::SectionTitle(text(ClientText::Tools));
        if (px::ui::ActionButton({"client-files"}, text(ClientText::Files),
                                 {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F, .disabled = !snapshot.fileTransferAvailable}))
            fileTransfer_->Open();
        if (px::ui::ActionButton({"client-screenshot"}, text(ClientText::Screenshot), {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F}))
            screenshotStatus_ = session->SaveScreenshot().value_or(text(ClientText::ScreenshotFailed));
        if (snapshot.recording) {
            if (px::ui::ActionButton({"client-record-stop"}, text(ClientText::StopRecording),
                                     {.variant = px::ui::ButtonVariant::Destructive, .width = -1.0F}))
                static_cast<void>(session->StopRecording());
        } else if (px::ui::ActionButton({"client-record"}, text(ClientText::Record), {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F})) {
            static_cast<void>(session->StartRecording());
        }
        static_cast<void>(px::ui::ToggleSwitch({"client-statistics"}, text(ClientText::Statistics), showStatistics_));
        if (showStatistics_) {
            const std::string statistics{"FPS " + std::to_string(snapshot.framesPerSecond) + " | " + std::to_string(snapshot.latencyMilliseconds) +
                                         " ms | " + std::to_string(snapshot.bitrateKbps) + " Kbps | " + snapshot.decoder};
            px::ui::FieldDescription(statistics);
        }
        if (!screenshotStatus_.empty())
            px::ui::FieldDescription(screenshotStatus_);
    } else if (section_ == Section::Voice) {
        px::ui::SectionTitle(text(ClientText::Voice));
        if (snapshot.voiceStatus == "Connected" || snapshot.voiceStatus == "Calling") {
            if (px::ui::ActionButton({"client-voice-stop"}, text(ClientText::HangUp),
                                     {.variant = px::ui::ButtonVariant::Destructive, .width = -1.0F}))
                static_cast<void>(session->StopVoiceCall());
        } else if (px::ui::ActionButton({"client-voice-start"}, text(ClientText::Voice), {.width = -1.0F})) {
            static_cast<void>(session->StartVoiceCall());
        }
        if (px::ui::ToggleSwitch({"client-microphone"}, text(ClientText::MuteMicrophone), microphoneMuted_))
            static_cast<void>(session->SetVoiceMicrophoneMuted(microphoneMuted_));
        if (px::ui::ToggleSwitch({"client-speaker"}, text(ClientText::MuteSpeaker), speakerMuted_))
            static_cast<void>(session->SetVoiceSpeakerMuted(speakerMuted_));
    } else {
        px::ui::SectionTitle(text(ClientText::Settings));
        if (px::ui::ActionButton({"client-language"}, english ? "简体中文" : "English",
                                 {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F}))
            action.toggleLanguage = true;
        if (px::ui::ActionButton({"client-theme"}, text(darkTheme ? ClientText::Light : ClientText::Dark),
                                 {.variant = px::ui::ButtonVariant::Secondary, .width = -1.0F}))
            action.toggleTheme = true;
        if (px::ui::ToggleSwitch({"client-effects"}, text(ClientText::EnhancedVisualEffects), enhancedVisualEffects_))
            action.toggleEnhancedVisualEffects = true;
    }

    const ImVec2 windowPosition{ImGui::GetWindowPos()};
    const ImVec2 windowSize{ImGui::GetWindowSize()};
    sectionBounds_ = {.x = windowPosition.x, .y = windowPosition.y, .width = windowSize.x, .height = windowSize.y};
    const bool hovered{ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)};
    ImGui::End();
    return hovered;
}

} // namespace px::client::imgui
