#include "controller_settings_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <array>
#include <utility>

namespace px::panel::ui {

ControllerSettingsPage::ControllerSettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void ControllerSettingsPage::Reload() {
    draft_ = port_->Snapshot().controller;
    recordingPath_ = draft_.recordingPath;
    loaded_ = true;
}

void ControllerSettingsPage::Draw(const px::ui::Localizer& localizer) {
    if (!loaded_) {
        Reload();
    }
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::ClientWindowSettings));
    px::ui::HorizontalSeparator();
    static_cast<void>(px::ui::ToggleSwitch({"controller-maximize"}, localizer.Text(px::ui::TextId::MaximizeClient), draft_.maximizeClient));
    static_cast<void>(px::ui::ToggleSwitch({"controller-logo"}, localizer.Text(px::ui::TextId::DisplayClientLogo), draft_.displayClientLogo));
    static_cast<void>(px::ui::ToggleSwitch({"controller-title"}, localizer.Text(px::ui::TextId::ColorfulTitleBar), draft_.colorfulTitleBar));
    const std::array screenOptions{px::ui::SelectOption{2, "2"}, px::ui::SelectOption{4, "4"}, px::ui::SelectOption{6, "6"},
                                   px::ui::SelectOption{8, "8"}};
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::MaximumScreens));
    static_cast<void>(px::ui::SelectField({"controller-screens"}, draft_.maximumScreens, screenOptions, px::ui::Scale(360.0F)));
    int decoder{static_cast<int>(draft_.preferredDecoder)};
    const std::array decoderOptions{px::ui::SelectOption{0, localizer.Text(px::ui::TextId::Automatic)},
                                    px::ui::SelectOption{1, localizer.Text(px::ui::TextId::HardwareDecoder)},
                                    px::ui::SelectOption{2, localizer.Text(px::ui::TextId::SoftwareDecoder)}};
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::PreferredDecoder));
    if (px::ui::SelectField({"controller-decoder"}, decoder, decoderOptions, px::ui::Scale(360.0F))) {
        draft_.preferredDecoder = static_cast<PreferredDecoder>(decoder);
    }
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::RecordingPath));
    static_cast<void>(px::ui::TextField({"controller-recording"}, recordingPath_));
    if (px::ui::ActionButton({"controller-save"}, localizer.Text(px::ui::TextId::Save))) {
        draft_.recordingPath = recordingPath_;
        port_->SaveController(draft_);
    }
}

} // namespace px::panel::ui
