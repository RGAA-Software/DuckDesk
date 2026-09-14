#include "general_settings_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <array>
#include <utility>

namespace px::panel::ui {
namespace {

px::ui::TextId ResultText(const GeneralSaveResult result) {
    switch (result) {
    case GeneralSaveResult::InvalidBitrate:
        return px::ui::TextId::InvalidBitrate;
    case GeneralSaveResult::InvalidFrameRate:
        return px::ui::TextId::InvalidFrameRate;
    case GeneralSaveResult::InvalidResolution:
        return px::ui::TextId::InvalidResolution;
    case GeneralSaveResult::UnsupportedResolution:
        return px::ui::TextId::UnsupportedResolution;
    case GeneralSaveResult::InvalidAspectRatio:
        return px::ui::TextId::InvalidAspectRatio;
    case GeneralSaveResult::Saved:
        return px::ui::TextId::Saved;
    }
    return px::ui::TextId::OperationFailed;
}

} // namespace

GeneralSettingsPage::GeneralSettingsPage(std::shared_ptr<SettingsPort> port, std::shared_ptr<ServerStatusPort> serverStatusPort)
    : port_{std::move(port)}, serverStatusPort_{std::move(serverStatusPort)} {}

void GeneralSettingsPage::Reload() {
    draft_ = port_->Snapshot().general;
    loaded_ = true;
}

std::optional<px::ui::Theme> GeneralSettingsPage::Draw(px::ui::Localizer& localizer, px::ui::Theme& theme) {
    if (!loaded_) {
        Reload();
    }
    std::optional<px::ui::Theme> selectedTheme{};
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::General));
    if (ImGui::BeginTable("GeneralAppearanceForm", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(120.0F));
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::Language));
        ImGui::TableNextColumn();
        if (px::ui::SegmentedItem({"language-zh-cn"}, localizer.Text(px::ui::TextId::SimplifiedChinese),
                                  localizer.CurrentLanguage() == px::ui::Language::SimplifiedChinese, 0.0F, px::ui::WidgetSize::Xs)) {
            localizer.SetLanguage(px::ui::Language::SimplifiedChinese);
            port_->SetLanguage(px::ui::Language::SimplifiedChinese);
        }
        ImGui::SameLine();
        if (px::ui::SegmentedItem({"language-en"}, localizer.Text(px::ui::TextId::English), localizer.CurrentLanguage() == px::ui::Language::English,
                                  0.0F, px::ui::WidgetSize::Xs)) {
            localizer.SetLanguage(px::ui::Language::English);
            port_->SetLanguage(px::ui::Language::English);
        }

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::Theme));
        ImGui::TableNextColumn();
        if (px::ui::SegmentedItem({"theme-dark"}, localizer.Text(px::ui::TextId::DarkTheme), theme == px::ui::Theme::Dark, 0.0F,
                                  px::ui::WidgetSize::Xs)) {
            theme = px::ui::Theme::Dark;
            selectedTheme = theme;
            port_->SetTheme(theme);
        }
        ImGui::SameLine();
        if (px::ui::SegmentedItem({"theme-light"}, localizer.Text(px::ui::TextId::LightTheme), theme == px::ui::Theme::Light, 0.0F,
                                  px::ui::WidgetSize::Xs)) {
            theme = px::ui::Theme::Light;
            selectedTheme = theme;
            port_->SetTheme(theme);
        }

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::EnhancedVisualEffects));
        ImGui::TableNextColumn();
        bool enhancedVisualEffects{port_->Snapshot().enhancedVisualEffects};
        if (px::ui::ToggleSwitch({"general-effects"}, {}, enhancedVisualEffects)) {
            port_->SetEnhancedVisualEffects(enhancedVisualEffects);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    const bool gamepadDriverReady{serverStatusPort_->Snapshot().controllerDriverReady};
    if (ImGui::BeginTable("GeneralGamepadDriver", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(120.0F));
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::ControllerDriver));
        ImGui::TableNextColumn();
        ImGui::SetWindowFontScale(0.875F);
        px::ui::StatusBadge(localizer.Text(gamepadDriverReady ? px::ui::TextId::Ready : px::ui::TextId::Unavailable),
                            gamepadDriverReady ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Destructive);
        if (!gamepadDriverReady) {
            ImGui::SameLine();
            if (px::ui::ActionButton({"general-install-gamepad-driver"}, localizer.Text(px::ui::TextId::Install),
                                     {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .circular = true})) {
                serverStatusPort_->InstallControllerDriver();
            }
        }
        ImGui::SetWindowFontScale(1.0F);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::EncoderSettings));
    px::ui::HorizontalSeparator();
    const float fieldWidth{px::ui::Scale(190.0F)};
    const std::array frameRateOptions{px::ui::SelectOption{15, "15"}, px::ui::SelectOption{30, "30"},   px::ui::SelectOption{60, "60"},
                                      px::ui::SelectOption{90, "90"}, px::ui::SelectOption{120, "120"}, px::ui::SelectOption{144, "144"}};
    int codec{draft_.codec == VideoCodec::H265 ? 1 : 0};
    const std::array codecOptions{px::ui::SelectOption{0, "H.264"}, px::ui::SelectOption{1, "H.265 (HEVC)"}};
    if (ImGui::BeginTable("GeneralSettingsForm", 3, ImGuiTableFlags_SizingFixedFit, {0.0F, 0.0F})) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(120.0F));
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(210.0F));
        ImGui::TableSetupColumn("suffix", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::BitrateMbps));
        ImGui::TableNextColumn();
        static_cast<void>(px::ui::NumberField({"general-bitrate"}, draft_.bitrateMbps, 1, 10, {.width = fieldWidth}));

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::FrameRate));
        ImGui::TableNextColumn();
        static_cast<void>(px::ui::SelectField({"general-frame-rate"}, draft_.frameRate, frameRateOptions, fieldWidth));

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::VideoCodec));
        ImGui::TableNextColumn();
        if (px::ui::SelectField({"general-codec"}, codec, codecOptions, fieldWidth)) {
            draft_.codec = codec == 1 ? VideoCodec::H265 : VideoCodec::H264;
        }

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::ResizeResolution));
        ImGui::TableNextColumn();
        static_cast<void>(px::ui::ToggleSwitch({"general-resize"}, {}, draft_.resizeEnabled));

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::Width));
        ImGui::TableNextColumn();
        const float dimensionWidth{px::ui::Scale(88.0F)};
        static_cast<void>(px::ui::NumberField({"general-width"}, draft_.width, 1, 100, {.width = dimensionWidth, .disabled = !draft_.resizeEnabled}));
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        static_cast<void>(
            px::ui::NumberField({"general-height"}, draft_.height, 1, 100, {.width = dimensionWidth, .disabled = !draft_.resizeEnabled}));
        ImGui::TableNextColumn();
        px::ui::MutedText(localizer.Text(px::ui::TextId::Height));

        ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        px::ui::MutedText(localizer.Text(px::ui::TextId::CaptureAudio));
        ImGui::TableNextColumn();
        static_cast<void>(px::ui::CheckboxField({"general-audio"}, "##capture-audio", draft_.captureAudio));

        ImGui::EndTable();
    }
    ImGui::Dummy({0.0F, px::ui::Scale(4.0F)});
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - px::ui::Scale(74.0F));
    if (px::ui::ActionButton({"general-save"}, localizer.Text(px::ui::TextId::Save))) {
        lastResult_ = port_->SaveGeneral(draft_);
        showRestartPrompt_ = lastResult_ == GeneralSaveResult::Saved;
    }
    if (lastResult_ != GeneralSaveResult::Saved) {
        ImGui::SameLine();
        px::ui::FieldError(localizer.Text(ResultText(lastResult_)));
    }
    if (showRestartPrompt_) {
        px::ui::OpenModal({"RestartRenderAfterGeneralSave"});
        showRestartPrompt_ = false;
    }
    px::ui::ModalScope dialog{{"RestartRenderAfterGeneralSave"}, 440.0F};
    if (dialog.Open()) {
        static_cast<void>(px::ui::DialogHeader({"general-restart-close"}, localizer.Text(px::ui::TextId::Restart),
                                               localizer.Text(px::ui::TextId::RestartRenderPrompt),
                                               {.icon = px::ui::VectorIcon::Refresh, .closeable = false}));
        const float buttonWidth{px::ui::Scale(120.0F)};
        px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
        if (px::ui::ActionButton({"restart-render-later"}, localizer.Text(px::ui::TextId::Later),
                                 {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"restart-render-now"}, localizer.Text(px::ui::TextId::RestartNow), {.width = buttonWidth})) {
            port_->RestartRender();
            ImGui::CloseCurrentPopup();
        }
    }
    return selectedTheme;
}

} // namespace px::panel::ui
