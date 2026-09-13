#pragma once

#include "px_ui/widget_types.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace px::ui {

class CardScope final {
  public:
    explicit CardScope(WidgetId id, ImVec2 size = {}, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
    CardScope(const CardScope&) = delete;
    CardScope& operator=(const CardScope&) = delete;
    CardScope(CardScope&&) = delete;
    CardScope& operator=(CardScope&&) = delete;
    ~CardScope();
    [[nodiscard]] bool Visible() const noexcept;

  private:
    std::string id_{};
    bool visible_{false};
};

void PageTitle(std::string_view title);
void SectionTitle(std::string_view title);
void StrongText(std::string_view text);
void MutedText(std::string_view text);
void StatusBadge(std::string_view text, BadgeVariant variant);
void HorizontalSeparator();

} // namespace px::ui
