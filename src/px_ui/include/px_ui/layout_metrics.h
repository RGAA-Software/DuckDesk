#pragma once

#include <imgui.h>

namespace px::ui {

inline float Scale(const float value) noexcept {
    return value * ImGui::GetStyle().FontScaleDpi;
}

inline ImVec2 Scale(const ImVec2 value) noexcept {
    const float scale{ImGui::GetStyle().FontScaleDpi};
    return {value.x * scale, value.y * scale};
}

} // namespace px::ui
