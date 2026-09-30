#include "client_statistics_overlay.h"

#include <imgui.h>

#include <algorithm>

#include "client_session.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

namespace px::client::imgui {
namespace {

void DrawLegibleText(ImDrawList& draw, const ImVec2 position, const std::string_view text, const px::ui::ThemeTokens& tokens) {
    const float outline{std::max(1.0F, px::ui::Scale(1.0F))};
    // Video pixels are independent of the UI theme; outlined text remains readable on both bright and dark frames.
    for (const ImVec2 offset : {ImVec2{-outline, 0.0F}, ImVec2{outline, 0.0F}, ImVec2{0.0F, -outline}, ImVec2{0.0F, outline}}) {
        draw.AddText({position.x + offset.x, position.y + offset.y}, ImGui::GetColorU32(tokens.videoOverlayOutline), text.data(),
                     text.data() + text.size());
    }
    draw.AddText(position, ImGui::GetColorU32(tokens.videoOverlayForeground), text.data(), text.data() + text.size());
}

}  // namespace

void ClientStatisticsOverlay::SetVisible(const bool visible) noexcept {
    if (visible && !visible_) nextRefresh_ = {};
    visible_ = visible;
}

void ClientStatisticsOverlay::SetLanguage(const bool english) noexcept {
    if (english_ != english) nextRefresh_ = {};
    english_ = english;
}

void ClientStatisticsOverlay::Draw(const ClientSession& session, const ControllerArea& contentArea) {
    if (!visible_) return;
    const auto now = std::chrono::steady_clock::now();
    if (now >= nextRefresh_) {
        rows_ = BuildClientStatisticsRows(session.StatisticsSnapshot(), english_);
        nextRefresh_ = now + std::chrono::seconds{1};
    }
    const float margin{px::ui::Scale(16.0F)};
    const float rowHeight{ImGui::GetTextLineHeight() + px::ui::Scale(3.0F)};
    float labelWidth{};
    float valueWidth{};
    for (const auto& row : rows_) {
        const auto label = ClientTextValue(row.label, english_);
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label.data(), label.data() + label.size()).x);
        valueWidth = std::max(valueWidth, ImGui::CalcTextSize(row.value.c_str()).x);
    }
    const float width{labelWidth + px::ui::Scale(20.0F) + valueWidth};
    const float left{std::max(contentArea.left + margin, contentArea.left + contentArea.width - margin - width)};
    const float top{std::max(contentArea.top + margin, contentArea.top + contentArea.height - margin - rowHeight * static_cast<float>(rows_.size()))};
    ImDrawList& draw{*ImGui::GetForegroundDrawList()};
    draw.PushClipRect({contentArea.left, contentArea.top}, {contentArea.left + contentArea.width, contentArea.top + contentArea.height}, true);
    const auto tokens = px::ui::CurrentThemeTokens();
    float rowTop{top};
    for (const auto& row : rows_) {
        DrawLegibleText(draw, {left, rowTop}, ClientTextValue(row.label, english_), tokens);
        DrawLegibleText(draw, {left + labelWidth + px::ui::Scale(20.0F), rowTop}, row.value, tokens);
        rowTop += rowHeight;
    }
    draw.PopClipRect();
    // Foreground drawing has no ImGui hit target: mouse and keyboard continue to reach the remote desktop underneath.
}

}  // namespace px::client::imgui
