#include "px_ui/vector_icon.h"

#include <algorithm>
#include <array>
#include <string>

namespace px::ui {
namespace {

struct IconCanvas final {
    ImDrawList& draw;
    ImVec2 origin{};
    float scale{};
    ImU32 color{};
    float thickness{};

    [[nodiscard]] ImVec2 Point(const float x, const float y) const {
        return {origin.x + x * scale, origin.y + y * scale};
    }
    void Line(const float x1, const float y1, const float x2, const float y2) const {
        draw.AddLine(Point(x1, y1), Point(x2, y2), color, thickness);
    }
    void Rect(const float x1, const float y1, const float x2, const float y2, const float rounding = 0.0F) const {
        const float scaledRounding{rounding * scale};
        draw.AddRect(Point(x1, y1), Point(x2, y2), color, scaledRounding, ImDrawFlags_None, thickness);
    }
    void Circle(const float x, const float y, const float radius) const {
        const float scaledRadius{radius * scale};
        draw.AddCircle(Point(x, y), scaledRadius, color, 0, thickness);
    }
    void Bezier(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4,
                const float y4) const {
        draw.AddBezierCubic(Point(x1, y1), Point(x2, y2), Point(x3, y3), Point(x4, y4), color, thickness);
    }
};

ImU32 ButtonTextColor() {
    return ImGui::GetColorU32(ImGuiCol_Text);
}

ImVec2 ResolveButtonSize(const std::string_view text, const ImVec2 requested, const bool iconOnly) {
    const auto& style = ImGui::GetStyle();
    const float height{requested.y > 0.0F ? requested.y : ImGui::GetFrameHeight()};
    if (requested.x != 0.0F) {
        return {requested.x, height};
    }
    const float iconSize{std::max(12.0F, height - style.FramePadding.y * 2.0F)};
    const float textWidth{iconOnly ? 0.0F : ImGui::CalcTextSize(text.data(), text.data() + text.size()).x};
    const float gap{iconOnly ? 0.0F : style.ItemInnerSpacing.x};
    return {style.FramePadding.x * 2.0F + iconSize + gap + textWidth, height};
}

bool DrawButton(const VectorIcon icon, const std::string_view text, const std::string_view id, const std::string_view tooltip, const ImVec2 requested,
                const bool iconOnly) {
    const ImVec2 buttonSize{ResolveButtonSize(text, requested, iconOnly)};
    std::string imguiId{"##px-icon-button-"};
    imguiId.append(id);
    const bool pressed{ImGui::Button(imguiId.c_str(), buttonSize)};
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    const auto& style = ImGui::GetStyle();
    const float iconSize{std::max(12.0F, maximum.y - minimum.y - style.FramePadding.y * 2.0F)};
    const float contentWidth{iconOnly ? iconSize
                                      : iconSize + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text.data(), text.data() + text.size()).x};
    const float left{iconOnly ? minimum.x + (maximum.x - minimum.x - iconSize) * 0.5F
                              : minimum.x + std::max(style.FramePadding.x, (maximum.x - minimum.x - contentWidth) * 0.5F)};
    const float top{minimum.y + (maximum.y - minimum.y - iconSize) * 0.5F};
    const ImU32 color{ButtonTextColor()};
    DrawVectorIcon(icon, {left, top}, iconSize, color);
    if (!iconOnly) {
        const ImVec2 textSize{ImGui::CalcTextSize(text.data(), text.data() + text.size())};
        const std::string visible{text};
        ImGui::GetWindowDrawList()->AddText({left + iconSize + style.ItemInnerSpacing.x, minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F},
                                            color, visible.c_str());
    }
    if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        const std::string textValue{tooltip};
        ImGui::SetTooltip("%s", textValue.c_str());
    }
    return pressed;
}

} // namespace

void DrawVectorIcon(const VectorIcon icon, const ImVec2 topLeft, const float size, const ImU32 color, const float thickness) {
    auto& draw = *ImGui::GetWindowDrawList();
    const IconCanvas canvas{.draw = draw, .origin = topLeft, .scale = size / 24.0F, .color = color, .thickness = thickness};
    switch (icon) {
    case VectorIcon::Minimize:
        canvas.Line(5.0F, 12.0F, 19.0F, 12.0F);
        break;
    case VectorIcon::Maximize:
        canvas.Rect(4.0F, 4.0F, 20.0F, 20.0F, 1.0F);
        break;
    case VectorIcon::Restore:
        canvas.Rect(7.0F, 7.0F, 20.0F, 20.0F, 1.0F);
        canvas.Line(4.0F, 17.0F, 4.0F, 4.0F);
        canvas.Line(4.0F, 4.0F, 17.0F, 4.0F);
        break;
    case VectorIcon::Close:
        canvas.Line(5.0F, 5.0F, 19.0F, 19.0F);
        canvas.Line(19.0F, 5.0F, 5.0F, 19.0F);
        break;
    case VectorIcon::Monitor:
        canvas.Rect(3.0F, 4.0F, 21.0F, 17.0F, 1.5F);
        canvas.Line(8.0F, 21.0F, 16.0F, 21.0F);
        canvas.Line(12.0F, 17.0F, 12.0F, 21.0F);
        break;
    case VectorIcon::Cloud:
        canvas.Bezier(6.0F, 18.0F, 2.0F, 18.0F, 2.0F, 11.0F, 7.0F, 11.0F);
        canvas.Bezier(7.0F, 11.0F, 8.0F, 4.0F, 18.0F, 4.0F, 18.0F, 11.0F);
        canvas.Bezier(18.0F, 10.0F, 23.0F, 10.0F, 23.0F, 18.0F, 18.0F, 18.0F);
        canvas.Line(6.0F, 18.0F, 18.0F, 18.0F);
        break;
    case VectorIcon::Activity:
        canvas.Line(3.0F, 12.0F, 7.0F, 12.0F);
        canvas.Line(7.0F, 12.0F, 10.0F, 5.0F);
        canvas.Line(10.0F, 5.0F, 14.0F, 19.0F);
        canvas.Line(14.0F, 19.0F, 17.0F, 12.0F);
        canvas.Line(17.0F, 12.0F, 21.0F, 12.0F);
        break;
    case VectorIcon::Shield:
        canvas.Line(12.0F, 3.0F, 20.0F, 6.0F);
        canvas.Line(20.0F, 6.0F, 20.0F, 12.0F);
        canvas.Bezier(20.0F, 12.0F, 20.0F, 17.0F, 16.0F, 20.0F, 12.0F, 22.0F);
        canvas.Bezier(12.0F, 22.0F, 8.0F, 20.0F, 4.0F, 17.0F, 4.0F, 12.0F);
        canvas.Line(4.0F, 12.0F, 4.0F, 6.0F);
        canvas.Line(4.0F, 6.0F, 12.0F, 3.0F);
        break;
    case VectorIcon::Settings:
        canvas.Circle(12.0F, 12.0F, 3.0F);
        canvas.Circle(12.0F, 12.0F, 8.0F);
        for (const auto& line : std::array<std::array<float, 4>, 4>{{{12, 2, 12, 5}, {12, 19, 12, 22}, {2, 12, 5, 12}, {19, 12, 22, 12}}})
            canvas.Line(line[0], line[1], line[2], line[3]);
        break;
    case VectorIcon::LogOut:
        canvas.Line(10.0F, 4.0F, 5.0F, 4.0F);
        canvas.Line(5.0F, 4.0F, 5.0F, 20.0F);
        canvas.Line(5.0F, 20.0F, 10.0F, 20.0F);
        canvas.Line(13.0F, 12.0F, 22.0F, 12.0F);
        canvas.Line(18.0F, 8.0F, 22.0F, 12.0F);
        canvas.Line(22.0F, 12.0F, 18.0F, 16.0F);
        break;
    case VectorIcon::Eye:
    case VectorIcon::EyeOff:
        canvas.Bezier(2.0F, 12.0F, 6.0F, 5.0F, 18.0F, 5.0F, 22.0F, 12.0F);
        canvas.Bezier(22.0F, 12.0F, 18.0F, 19.0F, 6.0F, 19.0F, 2.0F, 12.0F);
        canvas.Circle(12.0F, 12.0F, 3.0F);
        if (icon == VectorIcon::EyeOff)
            canvas.Line(3.0F, 3.0F, 21.0F, 21.0F);
        break;
    case VectorIcon::Copy:
        canvas.Rect(8.0F, 8.0F, 20.0F, 20.0F, 1.5F);
        canvas.Line(16.0F, 8.0F, 16.0F, 4.0F);
        canvas.Line(16.0F, 4.0F, 4.0F, 4.0F);
        canvas.Line(4.0F, 4.0F, 4.0F, 16.0F);
        canvas.Line(4.0F, 16.0F, 8.0F, 16.0F);
        break;
    case VectorIcon::QrCode:
        canvas.Rect(3.0F, 3.0F, 9.0F, 9.0F, 1.0F);
        canvas.Rect(15.0F, 3.0F, 21.0F, 9.0F, 1.0F);
        canvas.Rect(3.0F, 15.0F, 9.0F, 21.0F, 1.0F);
        canvas.Line(15.0F, 15.0F, 15.0F, 18.0F);
        canvas.Line(15.0F, 18.0F, 18.0F, 18.0F);
        canvas.Line(18.0F, 15.0F, 21.0F, 15.0F);
        canvas.Line(21.0F, 15.0F, 21.0F, 21.0F);
        canvas.Line(15.0F, 21.0F, 18.0F, 21.0F);
        break;
    case VectorIcon::ExternalLink:
        canvas.Line(14.0F, 4.0F, 20.0F, 4.0F);
        canvas.Line(20.0F, 4.0F, 20.0F, 10.0F);
        canvas.Line(20.0F, 4.0F, 11.0F, 13.0F);
        canvas.Line(18.0F, 13.0F, 18.0F, 20.0F);
        canvas.Line(18.0F, 20.0F, 4.0F, 20.0F);
        canvas.Line(4.0F, 20.0F, 4.0F, 6.0F);
        canvas.Line(4.0F, 6.0F, 11.0F, 6.0F);
        break;
    case VectorIcon::Refresh:
        draw.PathArcTo(canvas.Point(12.0F, 12.0F), 8.0F * canvas.scale, -2.7F, 0.7F, 18);
        draw.PathStroke(color, ImDrawFlags_None, thickness);
        canvas.Line(18.0F, 4.0F, 20.0F, 9.0F);
        canvas.Line(20.0F, 9.0F, 15.0F, 8.0F);
        draw.PathArcTo(canvas.Point(12.0F, 12.0F), 8.0F * canvas.scale, 0.45F, 3.85F, 18);
        draw.PathStroke(color, ImDrawFlags_None, thickness);
        canvas.Line(6.0F, 20.0F, 4.0F, 15.0F);
        canvas.Line(4.0F, 15.0F, 9.0F, 16.0F);
        break;
    case VectorIcon::Connect:
        canvas.Circle(6.0F, 12.0F, 3.0F);
        canvas.Circle(18.0F, 12.0F, 3.0F);
        canvas.Line(9.0F, 12.0F, 15.0F, 12.0F);
        break;
    case VectorIcon::Play:
        canvas.Line(7.0F, 4.0F, 20.0F, 12.0F);
        canvas.Line(20.0F, 12.0F, 7.0F, 20.0F);
        canvas.Line(7.0F, 20.0F, 7.0F, 4.0F);
        break;
    case VectorIcon::Stop:
        canvas.Rect(5.0F, 5.0F, 19.0F, 19.0F, 1.0F);
        break;
    case VectorIcon::More:
        canvas.Circle(5.0F, 12.0F, 1.0F);
        canvas.Circle(12.0F, 12.0F, 1.0F);
        canvas.Circle(19.0F, 12.0F, 1.0F);
        break;
    case VectorIcon::Pencil:
        canvas.Line(4.0F, 20.0F, 8.0F, 19.0F);
        canvas.Line(8.0F, 19.0F, 20.0F, 7.0F);
        canvas.Line(20.0F, 7.0F, 17.0F, 4.0F);
        canvas.Line(17.0F, 4.0F, 5.0F, 16.0F);
        canvas.Line(5.0F, 16.0F, 4.0F, 20.0F);
        canvas.Line(5.0F, 16.0F, 8.0F, 19.0F);
        break;
    case VectorIcon::User:
        canvas.Circle(12.0F, 8.0F, 4.0F);
        canvas.Bezier(4.0F, 21.0F, 4.0F, 14.0F, 20.0F, 14.0F, 20.0F, 21.0F);
        break;
    case VectorIcon::List:
        canvas.Circle(5.0F, 6.0F, 1.0F);
        canvas.Circle(5.0F, 12.0F, 1.0F);
        canvas.Circle(5.0F, 18.0F, 1.0F);
        canvas.Line(9.0F, 6.0F, 21.0F, 6.0F);
        canvas.Line(9.0F, 12.0F, 21.0F, 12.0F);
        canvas.Line(9.0F, 18.0F, 21.0F, 18.0F);
        break;
    case VectorIcon::Search:
        canvas.Circle(10.0F, 10.0F, 6.0F);
        canvas.Line(14.5F, 14.5F, 21.0F, 21.0F);
        break;
    case VectorIcon::FileTransfer:
        canvas.Rect(3.0F, 5.0F, 21.0F, 19.0F, 2.0F);
        canvas.Line(7.0F, 10.0F, 17.0F, 10.0F);
        canvas.Line(14.0F, 7.0F, 17.0F, 10.0F);
        canvas.Line(17.0F, 10.0F, 14.0F, 13.0F);
        canvas.Line(17.0F, 15.0F, 7.0F, 15.0F);
        break;
    case VectorIcon::Trash:
        canvas.Line(4.0F, 7.0F, 20.0F, 7.0F);
        canvas.Line(9.0F, 7.0F, 9.0F, 4.0F);
        canvas.Line(9.0F, 4.0F, 15.0F, 4.0F);
        canvas.Line(15.0F, 4.0F, 15.0F, 7.0F);
        canvas.Line(6.0F, 7.0F, 7.0F, 21.0F);
        canvas.Line(7.0F, 21.0F, 17.0F, 21.0F);
        canvas.Line(17.0F, 21.0F, 18.0F, 7.0F);
        canvas.Line(10.0F, 11.0F, 10.0F, 17.0F);
        canvas.Line(14.0F, 11.0F, 14.0F, 17.0F);
        break;
    }
}

bool IconButton(const VectorIcon icon, const std::string_view text, const std::string_view id, const ImVec2 size) {
    return DrawButton(icon, text, id, {}, size, false);
}

bool IconOnlyButton(const VectorIcon icon, const std::string_view id, const std::string_view tooltip, const ImVec2 size) {
    return DrawButton(icon, {}, id, tooltip, size, true);
}

} // namespace px::ui
