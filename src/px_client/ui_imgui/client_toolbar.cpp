#include "client_toolbar.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "client_session.h"
#include "client_recording_feedback.h"
#include "client_text.h"
#include "px_common/log.h"
#include "px_common/folder_util.h"
#include "px_common/file_util.h"
#include "px_desktop_shell/brand_logo.h"
#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/product_brand.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

namespace px::client::imgui {
namespace {

constexpr ImGuiWindowFlags kOverlayFlags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing};

constexpr std::array<px::ui::SelectOption, 5> kFrameRateOptions{{{15, "15"}, {30, "30"}, {60, "60"}, {90, "90"}, {120, "120"}}};

float ClampMenuY(const ImGuiViewport& viewport, const float desired, const float estimatedHeight) noexcept {
    const float minimum{viewport.WorkPos.y + ImGui::GetFontSize()};
    const float maximum{viewport.WorkPos.y + viewport.WorkSize.y - estimatedHeight - ImGui::GetFontSize()};
    return std::clamp(desired, minimum, std::max(minimum, maximum));
}

px::ui::ToastMessage ScreenshotToast(const ScreenshotResult& result, const bool english) {
    const bool saved{result.status == ScreenshotStatus::Saved};
    ClientText detailKey{ClientText::ScreenshotWriteFailed};
    switch (result.status) {
        case ScreenshotStatus::Saved: detailKey = ClientText::ScreenshotRevealFile; break;
        case ScreenshotStatus::NoFrame: detailKey = ClientText::ScreenshotNoFrame; break;
        case ScreenshotStatus::ReadbackFailed: detailKey = ClientText::ScreenshotReadbackFailed; break;
        case ScreenshotStatus::DirectoryUnavailable: detailKey = ClientText::ScreenshotDirectoryFailed; break;
        case ScreenshotStatus::WriteFailed: break;
    }
    px::ui::ToastMessage message{.title = std::string{ClientTextValue(saved ? ClientText::ScreenshotSaved : ClientText::ScreenshotFailed, english)},
                                .description = std::string{ClientTextValue(detailKey, english)},
                                .variant = saved ? px::ui::FeedbackVariant::Success : px::ui::FeedbackVariant::Error,
                                .duration = std::chrono::seconds{6}};
    const auto directory = result.path.parent_path();
    std::error_code directoryError{};
    if (!directory.empty() && std::filesystem::is_directory(directory, directoryError)) {
        const auto encodedPath = result.path.u8string();
        message.description += "\n" + std::string{encodedPath.begin(), encodedPath.end()};
        if (!saved) message.description += "\n" + std::string{ClientTextValue(ClientText::ScreenshotOpenFolder, english)};
        if (saved) message.onClick = [savedFile = result.path] { px::FileUtil::SelectFileInExplorer(savedFile); };
        else message.onClick = [directory] { px::FolderUtil::OpenDir(directory); };
    }
    return message;
}

}  // namespace

ClientToolbar::ClientToolbar(ClientUiSettings settings)
    : settings_{std::move(settings)}, launcherPosition_{settings_.LoadControllerPosition()} {}

bool ClientToolbar::Bounds::Contains(const float pointX, const float pointY) const noexcept {
    return width > 0.0F && height > 0.0F && pointX >= x && pointY >= y && pointX < x + width && pointY < y + height;
}

bool ClientToolbar::CapturesPointer(const float x, const float y) const noexcept {
    return launcherBounds_.Contains(x, y) || navigationBounds_.Contains(x, y) || sectionBounds_.Contains(x, y) ||
           captureToasts_.CapturesPointer(x, y);
}

bool ClientToolbar::HandlePointerEvent(const px::desktop::DesktopInputEvent& event) {
    if (captureToasts_.CapturesPointer(event.x, event.y)) return true;
    if (dismissPointerDown_) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.mouseButton == SDL_BUTTON_LEFT) dismissPointerDown_ = false;
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
            const auto position = controllerArea_.Clamp({dragOriginLauncherX_ + deltaX, dragOriginLauncherY_ + deltaY});
            launcherX_ = position.x;
            launcherY_ = position.y;
            launcherPosition_ = controllerArea_.Normalize(position);
            launcherBounds_.x = launcherX_;
            launcherBounds_.y = launcherY_;
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
            if (launcherPosition_ && !settings_.SaveControllerPosition(*launcherPosition_)) LOGW("Could not save floating controller position");
            LOGI("Client input route: floating controller moved x={:.1f} y={:.1f}", launcherX_, launcherY_);
        }
        launcherPointerDown_ = false;
        launcherDragged_ = false;
        return true;
    }
    return launcherPointerDown_ || CapturesPointer(event.x, event.y);
}

ClientToolbarAction ClientToolbar::Draw(const std::shared_ptr<ClientSession>& session, const px::desktop::BrandLogo& logo, const bool english,
                                        const bool darkTheme, const bool fullscreen) {
    ClientToolbarAction action{};
    bool hovered{DrawLauncher(logo)};
    const auto snapshot = session->Snapshot();
    if (expanded_) {
        hovered = DrawNavigation(snapshot, logo, english, action) || hovered;
        if (sectionExpanded_) {
            hovered = DrawSection(session, snapshot, english, darkTheme, fullscreen, action) || hovered;
        } else {
            sectionBounds_ = {};
        }
    } else {
        navigationBounds_ = {};
        sectionBounds_ = {};
    }
    statisticsOverlay_.SetVisible(showStatistics_);
    statisticsOverlay_.SetLanguage(english);
    statisticsOverlay_.Draw(*session, controllerArea_);
    for (const auto& recordingResult : session->TakeRecordingResults()) {
        if (recordingResult.status == RecordingResultStatus::Failed) LOGW("Recording failed: {}", recordingResult.error);
        captureToasts_.Push(RecordingToast(recordingResult, english));
    }
    // Re-read after menu actions so the indicator switches immediately when Record/Stop is clicked.
    const bool recording{session->Snapshot().recording};
    const auto now = std::chrono::steady_clock::now();
    if (recording && !recordingStartedAt_) recordingStartedAt_ = now;
    if (!recording) recordingStartedAt_.reset();
    const double elapsedSeconds{recordingStartedAt_ ? std::chrono::duration<double>(now - *recordingStartedAt_).count() : 0.0};
    const float recordingInset{DrawRecordingIndicator(recording, controllerArea_, elapsedSeconds, english)};
    captureToasts_.Draw(px::ui::ToastPlacement::TopRight, controllerArea_.top - ImGui::GetMainViewport()->WorkPos.y + recordingInset);
    return action;
}

bool ClientToolbar::DrawLauncher(const px::desktop::BrandLogo& logo) {
    const float fontSize{ImGui::GetFontSize()};
    const float diameter{fontSize * 2.8F};
    // Draw runs at the root content origin, after its title bar and before the video. Fullscreen has no title bar.
    const ImVec2 contentOrigin{ImGui::GetCursorScreenPos()};
    const ImVec2 contentSize{ImGui::GetContentRegionAvail()};
    controllerArea_ = {contentOrigin.x, contentOrigin.y, contentSize.x, contentSize.y, diameter};
    const auto position = controllerArea_.Restore(launcherPosition_, fontSize);
    launcherX_ = position.x;
    launcherY_ = position.y;
    launcherBounds_ = {.x = launcherX_, .y = launcherY_, .width = diameter, .height = diameter};
    const ImVec2 mouse{ImGui::GetIO().MousePos};
    const bool hovered{launcherBounds_.Contains(mouse.x, mouse.y)};
    const ImVec2 center{launcherX_ + diameter * 0.5F, launcherY_ + diameter * 0.5F};
    const float radius{diameter * 0.5F};
    // The controller must remain above the root video window even after the root receives focus.
    // Pointer routing is handled from SDL events, so a focusable ImGui overlay window is neither needed nor desirable here.
    ImDrawList& drawList{*ImGui::GetForegroundDrawList()};
    // Clip the shadow and border as well, so no part of the ball paints over the title bar.
    drawList.PushClipRect(contentOrigin, {contentOrigin.x + contentSize.x, contentOrigin.y + contentSize.y}, true);
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    if (px::ui::EnhancedVisualEffectsEnabled()) {
        ImVec4 shadowColor{tokens.floatingControllerShadow};
        shadowColor.w *= 0.4375F;
        drawList.AddCircleFilled(center, radius + 8.0F, ImGui::GetColorU32(shadowColor), 48);
        shadowColor.w = tokens.floatingControllerShadow.w * 0.6875F;
        drawList.AddCircleFilled(center, radius + 5.0F, ImGui::GetColorU32(shadowColor), 48);
        drawList.AddCircleFilled(center, radius + 3.0F, ImGui::GetColorU32(tokens.floatingControllerShadow), 48);
    }
    const ImVec4 surface{launcherPointerDown_ ? tokens.accent : tokens.popover};
    drawList.AddCircleFilled(center, radius, ImGui::GetColorU32(surface), 48);
    drawList.AddCircle(center, radius, ImGui::GetColorU32(hovered || launcherPointerDown_ ? tokens.ring : tokens.border), 48,
                       std::max(1.0F, ImGui::GetStyle().FontScaleDpi));
    const float logoSize{diameter * 0.62F};
    logo.Draw(drawList, {center.x - logoSize * 0.5F, center.y - logoSize * 0.5F}, logoSize);
    drawList.PopClipRect();
    if (hovered) {
        px::ui::ShowTooltip(px::ui::ApplicationName());
    }
    return hovered || launcherPointerDown_;
}

bool ClientToolbar::DrawNavigation(const ClientSessionSnapshot& snapshot, const px::desktop::BrandLogo& logo, const bool english,
                                   ClientToolbarAction& action) {
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto& viewport = *ImGui::GetMainViewport();
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float menuWidth{220.0F * metrics.scale};
    const float menuHeight{242.0F * metrics.scale};
    const float spacing{metrics.spacingMd};
    const bool openLeft{launcherBounds_.x + launcherBounds_.width * 0.5F > viewport.WorkPos.x + viewport.WorkSize.x * 0.5F};
    const float navigationX{openLeft ? launcherBounds_.x - menuWidth - spacing : launcherBounds_.x + launcherBounds_.width + spacing};
    // Clamp both menus as one group so a taller section cannot break their top alignment.
    const float menuGroupHeight{sectionExpanded_ ? std::max(menuHeight, sectionBounds_.height) : menuHeight};
    const float navigationY{ClampMenuY(viewport, launcherBounds_.y + launcherBounds_.height * 0.5F - menuHeight * 0.5F, menuGroupHeight)};
    ImGui::SetNextWindowPos({navigationX, navigationY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({menuWidth, 0.0F}, ImGuiCond_Always);
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    ImGui::SetNextWindowBgAlpha(px::ui::EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F);
    if (navigationNeedsFocus_) {
        ImGui::SetNextWindowFocus();
        navigationNeedsFocus_ = false;
    }
    const px::ui::ScopedStyleColor background{ImGuiCol_WindowBg, tokens.popover};
    const px::ui::ScopedStyleColor border{ImGuiCol_Border, tokens.border};
    const px::ui::ScopedStyleVar rounding{ImGuiStyleVar_WindowRounding, metrics.popupRadius};
    const px::ui::ScopedStyleVar padding{ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingMd, metrics.spacingMd}};
    const px::ui::ScopedStyleVar spacingStyle{ImGuiStyleVar_ItemSpacing, ImVec2{metrics.spacingSm, metrics.spacingSm}};
    ImGui::Begin("##pixels-controller-navigation", {}, kOverlayFlags);
    static_cast<void>(logo);

    auto& selectedSection = section_;
    auto& sectionExpanded = sectionExpanded_;
    auto& sectionNeedsFocus = sectionNeedsFocus_;
    auto& menuExpanded = expanded_;
    const auto navigationItem = [&selectedSection, &sectionExpanded, &sectionNeedsFocus, &menuExpanded, &metrics, &action](
                                    const std::string_view label, const Section section, const bool enabled = true) {
        const px::ui::VectorIcon icon{section == Section::Display    ? px::ui::VectorIcon::Monitor
                                      : section == Section::Control  ? px::ui::VectorIcon::Connect
                                      : section == Section::Tools    ? px::ui::VectorIcon::FileTransfer
                                      : section == Section::Voice    ? px::ui::VectorIcon::Phone
                                      : section == Section::Settings ? px::ui::VectorIcon::Settings
                                                                     : px::ui::VectorIcon::LogOut};
        ImGui::BeginDisabled(!enabled);
        const bool isSection{section != Section::Exit};
        const bool pressed{px::ui::NavigationItem({label}, icon, label, isSection && selectedSection == section, -1.0F, px::ui::WidgetSize::Sm,
                                                  metrics.controlDefault, metrics.spacingMd, false)};
        const ImVec2 itemMinimum{ImGui::GetItemRectMin()};
        const ImVec2 itemMaximum{ImGui::GetItemRectMax()};
        const px::ui::ThemeTokens itemTokens{px::ui::CurrentThemeTokens()};
        const bool itemHovered{ImGui::IsItemHovered()};
        const ImVec4 arrowColor{isSection && selectedSection == section ? itemTokens.accentForeground
                                                                        : (itemHovered ? itemTokens.foreground : itemTokens.mutedForeground)};
        px::ui::DrawVectorIcon(
            px::ui::VectorIcon::ChevronRight,
            {itemMaximum.x - metrics.spacingMd - metrics.iconDefault, itemMinimum.y + (itemMaximum.y - itemMinimum.y - metrics.iconDefault) * 0.5F},
            metrics.iconDefault, ImGui::GetColorU32(arrowColor));
        if (enabled && section == Section::Exit && pressed) {
            action.requestExit = true;
            menuExpanded = false;
            sectionExpanded = false;
        } else if (enabled && isSection && pressed) {
            if (!sectionExpanded || selectedSection != section) sectionNeedsFocus = true;
            selectedSection = section;
            sectionExpanded = true;
        }
        ImGui::EndDisabled();
    };
    navigationItem(text(ClientText::Display), Section::Display);
    navigationItem(text(ClientText::Control), Section::Control);
    navigationItem(text(ClientText::Tools), Section::Tools);
    // Voice implementation is retained; its menu entry is temporarily hidden by product decision.
    navigationItem(text(ClientText::Settings), Section::Settings);
    navigationItem(text(ClientText::ExitControl), Section::Exit);

    const ImVec2 windowPosition{ImGui::GetWindowPos()};
    const ImVec2 windowSize{ImGui::GetWindowSize()};
    navigationBounds_ = {.x = windowPosition.x, .y = windowPosition.y, .width = windowSize.x, .height = windowSize.y};
    const bool hovered{ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)};
    ImGui::End();
    return hovered;
}

bool ClientToolbar::DrawSection(const std::shared_ptr<ClientSession>& session, const ClientSessionSnapshot& snapshot, const bool english,
                                const bool darkTheme, const bool fullscreen, ClientToolbarAction& action) {
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto& viewport = *ImGui::GetMainViewport();
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float sectionWidth{navigationBounds_.width};
    const float spacing{metrics.spacingSm};
    const bool openLeft{launcherBounds_.x + launcherBounds_.width * 0.5F > viewport.WorkPos.x + viewport.WorkSize.x * 0.5F};
    const float sectionX{openLeft ? navigationBounds_.x - sectionWidth - spacing : navigationBounds_.x + navigationBounds_.width + spacing};
    ImGui::SetNextWindowPos({sectionX, navigationBounds_.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({sectionWidth, 0.0F}, ImGuiCond_Always);
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    ImGui::SetNextWindowBgAlpha(px::ui::EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F);
    if (sectionNeedsFocus_) {
        ImGui::SetNextWindowFocus();
        sectionNeedsFocus_ = false;
    }
    const px::ui::ScopedStyleColor background{ImGuiCol_WindowBg, tokens.popover};
    const px::ui::ScopedStyleColor border{ImGuiCol_Border, tokens.border};
    const px::ui::ScopedStyleVar rounding{ImGuiStyleVar_WindowRounding, metrics.popupRadius};
    const px::ui::ScopedStyleVar padding{ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingMd, metrics.spacingMd}};
    const px::ui::ScopedStyleVar spacingStyle{ImGuiStyleVar_ItemSpacing, ImVec2{metrics.spacingSm, metrics.spacingSm}};
    ImGui::Begin("##pixels-controller-section", {}, kOverlayFlags);

    if (section_ == Section::Display) {
        px::ui::SectionTitle(text(ClientText::Display));
        px::ui::HorizontalSeparator();
        if (!snapshot.monitorName.empty()) {
            px::ui::FieldLabel(text(ClientText::Monitor));
            std::vector<px::ui::SelectOption> options{};
            options.reserve(snapshot.monitors.size());
            int selected{};
            for (std::size_t index{}; index < snapshot.monitors.size(); ++index) {
                options.push_back({static_cast<int>(index), snapshot.monitors[index]});
                if (snapshot.monitors[index] == snapshot.monitorName) selected = static_cast<int>(index);
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
                if (resolution.width == resolutionWidth_ && resolution.height == resolutionHeight_) selected = static_cast<int>(index);
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
        if (px::ui::SelectField({"client-frame-rate"}, frameRate_, kFrameRateOptions)) static_cast<void>(session->SetFrameRate(frameRate_));
        // Read actual window state every frame; a failed transition must not leave the switch in a fictional state.
        bool requestedFullscreen{fullscreen};
        if (px::ui::ToggleSwitch({"client-fullscreen"}, text(ClientText::Fullscreen), requestedFullscreen, false,
                                 fullscreen ? px::ui::VectorIcon::Restore : px::ui::VectorIcon::Maximize))
            action.toggleFullscreen = true;
        if (snapshot.virtualDisplayAvailable) {
            px::ui::HorizontalSeparator();
            const std::string displayCount{std::string{text(ClientText::VirtualDisplays)} + " " + std::to_string(snapshot.virtualDisplayCount) + "/" +
                                           std::to_string(snapshot.virtualDisplayMaximum)};
            px::ui::MutedText(displayCount);
            if (px::ui::ActionButton({"virtual-display-add"}, text(ClientText::AddVirtualDisplay),
                                     {.variant = px::ui::ButtonVariant::Outline,
                                      .size = px::ui::WidgetSize::Sm,
                                      .icon = px::ui::VectorIcon::Plus,
                                      .width = (sectionWidth - metrics.spacingMd * 2.0F - metrics.spacingSm) * 0.5F,
                                      .disabled = snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount >= snapshot.virtualDisplayMaximum}))
                static_cast<void>(session->CreateVirtualDisplay());
            ImGui::SameLine();
            if (px::ui::ActionButton({"virtual-display-remove"}, text(ClientText::RemoveVirtualDisplay),
                                     {.variant = px::ui::ButtonVariant::Outline,
                                      .size = px::ui::WidgetSize::Sm,
                                      .icon = px::ui::VectorIcon::Minus,
                                      .width = (sectionWidth - metrics.spacingMd * 2.0F - metrics.spacingSm) * 0.5F,
                                      .disabled = snapshot.virtualDisplayBusy || snapshot.virtualDisplayCount == 0U}))
                static_cast<void>(session->RemoveVirtualDisplay());
        }
    } else if (section_ == Section::Control) {
        px::ui::SectionTitle(text(ClientText::Control));
        px::ui::HorizontalSeparator();
        if (px::ui::ToggleSwitch({"client-audio"}, text(ClientText::Audio), audioEnabled_, false,
                                 audioEnabled_ ? px::ui::VectorIcon::Volume : px::ui::VectorIcon::VolumeOff))
            static_cast<void>(session->SetAudioEnabled(audioEnabled_));
        if (px::ui::ActionButton({"client-secure-attention"}, text(ClientText::SecureAttention),
                                 {.variant = px::ui::ButtonVariant::Secondary,
                                  .icon = px::ui::VectorIcon::Shield,
                                  .width = -1.0F,
                                  .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                  .contentInset = metrics.spacingMd}))
            static_cast<void>(session->SendSecureAttention());
    } else if (section_ == Section::Tools) {
        px::ui::SectionTitle(text(ClientText::Tools));
        px::ui::HorizontalSeparator();
        if (px::ui::ActionButton({"client-screenshot"}, text(ClientText::Screenshot),
                                 {.variant = px::ui::ButtonVariant::Secondary,
                                  .icon = px::ui::VectorIcon::Camera,
                                  .width = -1.0F,
                                  .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                  .contentInset = metrics.spacingMd})) {
            const auto result = session->SaveScreenshot();
            if (result.status != ScreenshotStatus::Saved) {
                LOGW("Screenshot failed, status={}, error={}", static_cast<int>(result.status), result.error);
            }
            captureToasts_.Push(ScreenshotToast(result, english));
        }
        if (snapshot.recording) {
            if (px::ui::ActionButton({"client-record-stop"}, text(ClientText::StopRecording),
                                     {.variant = px::ui::ButtonVariant::Destructive,
                                      .icon = px::ui::VectorIcon::Stop,
                                      .width = -1.0F,
                                      .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                      .contentInset = metrics.spacingMd}))
                static_cast<void>(session->StopRecording());
        } else if (px::ui::ActionButton({"client-record"}, text(ClientText::Record),
                                        {.variant = px::ui::ButtonVariant::Secondary,
                                         .icon = px::ui::VectorIcon::Video,
                                         .width = -1.0F,
                                         .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                         .contentInset = metrics.spacingMd})) {
            if (!session->StartRecording()) {
                captureToasts_.Push({.title = text(ClientText::RecordingStartFailed),
                                     .description = text(ClientText::RecordingStartFailedDetail),
                                     .variant = px::ui::FeedbackVariant::Error,
                                     .duration = std::chrono::seconds{6}});
            }
        }
        static_cast<void>(
            px::ui::ToggleSwitch({"client-statistics"}, text(ClientText::Statistics), showStatistics_, false, px::ui::VectorIcon::Activity));
    } else if (section_ == Section::Voice) {
        px::ui::SectionTitle(text(ClientText::Voice));
        px::ui::HorizontalSeparator();
        if (snapshot.voiceStatus == "Connected" || snapshot.voiceStatus == "Calling") {
            if (px::ui::ActionButton({"client-voice-stop"}, text(ClientText::HangUp),
                                     {.variant = px::ui::ButtonVariant::Destructive,
                                      .icon = px::ui::VectorIcon::PhoneOff,
                                      .width = -1.0F,
                                      .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                      .contentInset = metrics.spacingMd}))
                static_cast<void>(session->StopVoiceCall());
        } else if (px::ui::ActionButton({"client-voice-start"}, text(ClientText::Voice),
                                        {.icon = px::ui::VectorIcon::Phone,
                                         .width = -1.0F,
                                         .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                         .contentInset = metrics.spacingMd})) {
            static_cast<void>(session->StartVoiceCall());
        }
        if (px::ui::ToggleSwitch({"client-microphone"}, text(ClientText::MuteMicrophone), microphoneMuted_, false,
                                 microphoneMuted_ ? px::ui::VectorIcon::MicrophoneOff : px::ui::VectorIcon::Microphone))
            static_cast<void>(session->SetVoiceMicrophoneMuted(microphoneMuted_));
        if (px::ui::ToggleSwitch({"client-speaker"}, text(ClientText::MuteSpeaker), speakerMuted_, false,
                                 speakerMuted_ ? px::ui::VectorIcon::VolumeOff : px::ui::VectorIcon::Volume))
            static_cast<void>(session->SetVoiceSpeakerMuted(speakerMuted_));
    } else {
        px::ui::SectionTitle(text(ClientText::Settings));
        px::ui::HorizontalSeparator();
        if (px::ui::ActionButton({"client-language"}, english ? "简体中文" : "English",
                                 {.variant = px::ui::ButtonVariant::Secondary,
                                  .icon = px::ui::VectorIcon::Languages,
                                  .width = -1.0F,
                                  .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                  .contentInset = metrics.spacingMd}))
            action.toggleLanguage = true;
        if (px::ui::ActionButton({"client-theme"}, text(darkTheme ? ClientText::Light : ClientText::Dark),
                                 {.variant = px::ui::ButtonVariant::Secondary,
                                  .icon = px::ui::VectorIcon::Palette,
                                  .width = -1.0F,
                                  .contentAlignment = px::ui::ButtonContentAlignment::Leading,
                                  .contentInset = metrics.spacingMd}))
            action.toggleTheme = true;
    }

    const ImVec2 windowPosition{ImGui::GetWindowPos()};
    const ImVec2 windowSize{ImGui::GetWindowSize()};
    sectionBounds_ = {.x = windowPosition.x, .y = windowPosition.y, .width = windowSize.x, .height = windowSize.y};
    const bool hovered{ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)};
    ImGui::End();
    return hovered;
}

}  // namespace px::client::imgui
