#pragma once

#include "px_ui/widget_types.h"

#include <string_view>

namespace px::ui {

[[nodiscard]] bool AvatarButton(WidgetId id, std::string_view tooltip, float size, bool emphasized = true);

} // namespace px::ui
