#pragma once

#include "px_ui/widget_types.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace px::ui {

void OpenModal(WidgetId id);
void OpenPopup(WidgetId id);

class PopupScope final {
  public:
    explicit PopupScope(WidgetId id);
    PopupScope(const PopupScope&) = delete;
    PopupScope& operator=(const PopupScope&) = delete;
    PopupScope(PopupScope&&) = delete;
    PopupScope& operator=(PopupScope&&) = delete;
    ~PopupScope();
    [[nodiscard]] bool Open() const noexcept;

  private:
    std::string id_{};
    bool open_{false};
};

class ModalScope final {
  public:
    ModalScope(WidgetId id, float width = 480.0F, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize);
    ModalScope(const ModalScope&) = delete;
    ModalScope& operator=(const ModalScope&) = delete;
    ModalScope(ModalScope&&) = delete;
    ModalScope& operator=(ModalScope&&) = delete;
    ~ModalScope();
    [[nodiscard]] bool Open() const noexcept;

  private:
    std::string id_{};
    bool open_{false};
};

class ContextMenuScope final {
  public:
    explicit ContextMenuScope(WidgetId id);
    ContextMenuScope(const ContextMenuScope&) = delete;
    ContextMenuScope& operator=(const ContextMenuScope&) = delete;
    ContextMenuScope(ContextMenuScope&&) = delete;
    ContextMenuScope& operator=(ContextMenuScope&&) = delete;
    ~ContextMenuScope();
    [[nodiscard]] bool Open() const noexcept;

  private:
    std::string id_{};
    bool open_{false};
};

class PopupMenuScope final {
  public:
    explicit PopupMenuScope(WidgetId id);
    PopupMenuScope(const PopupMenuScope&) = delete;
    PopupMenuScope& operator=(const PopupMenuScope&) = delete;
    PopupMenuScope(PopupMenuScope&&) = delete;
    PopupMenuScope& operator=(PopupMenuScope&&) = delete;
    ~PopupMenuScope();
    [[nodiscard]] bool Open() const noexcept;

  private:
    std::string id_{};
    bool open_{false};
};

enum class MenuItemVariant { Default, Destructive };

struct MenuActionOptions final {
    std::optional<VectorIcon> icon{};
    std::string_view shortcut{};
    MenuItemVariant variant{MenuItemVariant::Default};
    bool enabled{true};
    bool selected{false};
};

struct DialogHeaderOptions final {
    std::optional<VectorIcon> icon{};
    BadgeVariant tone{BadgeVariant::Secondary};
    bool closeable{true};
};

[[nodiscard]] bool DialogHeader(WidgetId closeId, std::string_view title, std::string_view description = {}, const DialogHeaderOptions& options = {});
void DialogFooter(float actionsWidth = 0.0F);
void ShowTooltip(std::string_view text);
void Tooltip(std::string_view text);
void MenuSeparator();
[[nodiscard]] bool MenuAction(WidgetId id, std::string_view label, const MenuActionOptions& options);
[[nodiscard]] bool MenuAction(WidgetId id, std::string_view label, bool enabled = true, bool selected = false);

} // namespace px::ui
