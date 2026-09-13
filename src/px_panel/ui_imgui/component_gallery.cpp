#include "component_gallery.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <array>

namespace px::panel::ui {

ComponentGalleryAction ComponentGallery::Draw() {
    ComponentGalleryAction action{.enhancedVisualEffects = enhancedVisualEffects_, .theme = theme_};
    px::ui::PageTitle("Pixels UI Components");
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"gallery-dark"}, "Dark", theme_ == px::ui::Theme::Dark)) {
        theme_ = px::ui::Theme::Dark;
        action.themeChanged = true;
        action.theme = theme_;
    }
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"gallery-light"}, "Light", theme_ == px::ui::Theme::Light)) {
        theme_ = px::ui::Theme::Light;
        action.themeChanged = true;
        action.theme = theme_;
    }
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"gallery-effects"}, enhancedVisualEffects_ ? "Enhanced" : "Basic", enhancedVisualEffects_)) {
        enhancedVisualEffects_ = !enhancedVisualEffects_;
        action.enhancedVisualEffectsChanged = true;
        action.enhancedVisualEffects = enhancedVisualEffects_;
    }

    const float columnWidth{(ImGui::GetContentRegionAvail().x - px::ui::Scale(12.0F)) * 0.5F};
    {
        px::ui::CardScope card{{"gallery-actions"}, {columnWidth, px::ui::Scale(292.0F)}};
        px::ui::SectionTitle("Actions and status");
        ImGui::Spacing();
        static_cast<void>(px::ui::ActionButton({"gallery-primary"}, "Primary"));
        ImGui::SameLine();
        static_cast<void>(px::ui::ActionButton({"gallery-secondary"}, "Secondary", {.variant = px::ui::ButtonVariant::Secondary}));
        ImGui::SameLine();
        static_cast<void>(px::ui::ActionButton({"gallery-outline"}, "Outline", {.variant = px::ui::ButtonVariant::Outline}));
        ImGui::SameLine();
        static_cast<void>(px::ui::ActionButton({"gallery-ghost"}, "Ghost", {.variant = px::ui::ButtonVariant::Ghost}));
        static_cast<void>(px::ui::ActionButton({"gallery-destructive"}, "Destructive", {.variant = px::ui::ButtonVariant::Destructive}));
        ImGui::SameLine();
        static_cast<void>(px::ui::ActionButton({"gallery-disabled"}, "Disabled", {.disabled = true}));
        ImGui::Spacing();
        px::ui::StatusBadge("Online", px::ui::BadgeVariant::Success);
        ImGui::SameLine();
        px::ui::StatusBadge("Warning", px::ui::BadgeVariant::Warning);
        ImGui::SameLine();
        px::ui::StatusBadge("Offline", px::ui::BadgeVariant::Secondary);
        ImGui::Spacing();
        if (px::ui::ActionButton({"gallery-dialog"}, "Open dialog", {.variant = px::ui::ButtonVariant::Outline})) {
            dialogRequested_ = true;
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"gallery-toast"}, "Show toast", {.variant = px::ui::ButtonVariant::Outline})) {
            toasts_.Push({.title = "Saved", .description = "The component state was saved.", .variant = px::ui::FeedbackVariant::Success});
        }
        if (dialogRequested_) {
            px::ui::OpenModal({"Component dialog"});
            dialogRequested_ = false;
        }
        px::ui::ModalScope modal{{"Component dialog"}, 420.0F};
        if (modal.Open()) {
            px::ui::SectionTitle("Confirm action");
            px::ui::FieldDescription("This demonstrates a centered, focus-safe dialog.");
            ImGui::Spacing();
            if (px::ui::ActionButton({"gallery-cancel"}, "Cancel", {.variant = px::ui::ButtonVariant::Outline})) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (px::ui::ActionButton({"gallery-confirm"}, "Confirm")) {
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    {
        px::ui::CardScope card{{"gallery-form"}, {columnWidth, px::ui::Scale(292.0F)}};
        px::ui::SectionTitle("Form controls");
        px::ui::FieldLabel("Text field");
        static_cast<void>(px::ui::TextField({"gallery-text"}, text_, "Type a value"));
        px::ui::FieldLabel("Invalid field");
        static_cast<void>(px::ui::TextField({"gallery-invalid"}, invalidText_, "Required", {.invalid = true}));
        px::ui::FieldError("This field is required.");
        ImGui::SetNextItemWidth(px::ui::Scale(170.0F));
        static_cast<void>(px::ui::NumberField({"gallery-number"}, number_, 1, 10, {.width = px::ui::Scale(170.0F)}));
        static_cast<void>(px::ui::SliderIntField({"gallery-slider"}, number_, 0, 100, px::ui::Scale(170.0F)));
        constexpr std::array options{px::ui::SelectOption{30, "30"}, px::ui::SelectOption{60, "60"}, px::ui::SelectOption{120, "120"}};
        static_cast<void>(px::ui::SelectField({"gallery-select"}, selection_, options, px::ui::Scale(170.0F)));
        static_cast<void>(px::ui::CheckboxField({"gallery-check"}, "Checkbox", checked_));
        static_cast<void>(px::ui::ToggleSwitch({"gallery-switch"}, "Switch", switched_));
    }
    {
        px::ui::CardScope card{{"gallery-navigation"}, {columnWidth, px::ui::Scale(210.0F)}};
        px::ui::SectionTitle("Navigation and data");
        static_cast<void>(
            px::ui::NavigationItem({"gallery-nav-selected"}, px::ui::VectorIcon::Monitor, "Selected item", true, px::ui::Scale(210.0F)));
        static_cast<void>(px::ui::NavigationItem({"gallery-nav"}, px::ui::VectorIcon::Settings, "Navigation item", false, px::ui::Scale(210.0F)));
        static_cast<void>(px::ui::TabItem({"gallery-tab-a"}, "Active tab", true));
        ImGui::SameLine();
        static_cast<void>(px::ui::TabItem({"gallery-tab-b"}, "Inactive tab", false));
        static_cast<void>(px::ui::SelectableRow({"gallery-row"}, "Selectable row", false));
        px::ui::KeyValueRow("Device", "MC-60", px::ui::Scale(120.0F));
        px::ui::Progress(0.65F);
    }
    ImGui::SameLine();
    {
        px::ui::CardScope card{{"gallery-feedback"}, {columnWidth, px::ui::Scale(210.0F)}};
        px::ui::SectionTitle("Feedback");
        px::ui::InlineAlert("Connection warning", "The control channel remains available while media reconnects.", px::ui::FeedbackVariant::Warning);
        ImGui::Spacing();
        px::ui::LoadingSpinner({"gallery-spinner"});
        ImGui::SameLine();
        px::ui::MutedText("Loading current state...");
    }
    toasts_.Draw();
    return action;
}

} // namespace px::panel::ui
