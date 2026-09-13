#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace px::ui {

class ScopedStyleColor final {
  public:
    ScopedStyleColor(const ImGuiCol index, const ImVec4 color) noexcept : count_{1} { ImGui::PushStyleColor(index, color); }
    ScopedStyleColor(const ScopedStyleColor&) = delete;
    ScopedStyleColor& operator=(const ScopedStyleColor&) = delete;
    ScopedStyleColor(ScopedStyleColor&&) = delete;
    ScopedStyleColor& operator=(ScopedStyleColor&&) = delete;
    ~ScopedStyleColor() { ImGui::PopStyleColor(count_); }

  private:
    int count_{0};
};

class ScopedStyleVar final {
  public:
    ScopedStyleVar(const ImGuiStyleVar index, const float value) noexcept : count_{1} { ImGui::PushStyleVar(index, value); }
    ScopedStyleVar(const ImGuiStyleVar index, const ImVec2 value) noexcept : count_{1} { ImGui::PushStyleVar(index, value); }
    ScopedStyleVar(const ScopedStyleVar&) = delete;
    ScopedStyleVar& operator=(const ScopedStyleVar&) = delete;
    ScopedStyleVar(ScopedStyleVar&&) = delete;
    ScopedStyleVar& operator=(ScopedStyleVar&&) = delete;
    ~ScopedStyleVar() { ImGui::PopStyleVar(count_); }

  private:
    int count_{0};
};

class ScopedId final {
  public:
    explicit ScopedId(const std::string_view id) : value_{id} { ImGui::PushID(value_.c_str()); }
    ScopedId(const ScopedId&) = delete;
    ScopedId& operator=(const ScopedId&) = delete;
    ScopedId(ScopedId&&) = delete;
    ScopedId& operator=(ScopedId&&) = delete;
    ~ScopedId() { ImGui::PopID(); }

  private:
    std::string value_{};
};

class ScopedDisabled final {
  public:
    explicit ScopedDisabled(const bool disabled = true) noexcept : active_{disabled} {
        if (active_) {
            ImGui::BeginDisabled();
        }
    }
    ScopedDisabled(const ScopedDisabled&) = delete;
    ScopedDisabled& operator=(const ScopedDisabled&) = delete;
    ScopedDisabled(ScopedDisabled&&) = delete;
    ScopedDisabled& operator=(ScopedDisabled&&) = delete;
    ~ScopedDisabled() {
        if (active_) {
            ImGui::EndDisabled();
        }
    }

  private:
    bool active_{false};
};

} // namespace px::ui
