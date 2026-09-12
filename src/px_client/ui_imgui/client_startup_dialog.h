#pragma once

#include <string_view>

namespace px::client::imgui {

enum class StartupDialogAction { Exit, Continue };

[[nodiscard]] StartupDialogAction ShowStartupDialog(std::string_view message, std::string_view button, bool error);

} // namespace px::client::imgui
