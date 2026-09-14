#pragma once

#include "px_ui/vector_icon.h"

#include <imgui.h>

#include <optional>
#include <string_view>

namespace px::ui {

struct WidgetId final {
    std::string_view value{};
};

enum class WidgetSize { Xs, Sm, Default, Lg, IconXs, IconSm, Icon, IconLg };
enum class ButtonVariant { Primary, Secondary, Accent, Outline, Ghost, GhostDestructive, Destructive, Link };
enum class ButtonContentAlignment { Center, Leading };
enum class BadgeVariant { Default, Secondary, Outline, Success, Warning, Destructive };

struct ButtonOptions final {
    ButtonVariant variant{ButtonVariant::Primary};
    WidgetSize size{WidgetSize::Default};
    std::optional<VectorIcon> icon{};
    bool iconTrailing{false};
    float width{0.0F};
    float height{0.0F};
    ButtonContentAlignment contentAlignment{ButtonContentAlignment::Center};
    float contentInset{0.0F};
    bool circular{false};
    bool disabled{false};
    bool busy{false};
};

struct FieldOptions final {
    float width{0.0F};
    std::optional<VectorIcon> leadingIcon{};
    bool disabled{false};
    bool invalid{false};
    bool readOnly{false};
};

} // namespace px::ui
