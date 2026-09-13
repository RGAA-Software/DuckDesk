#include "connection_qr_dialog.h"

#include "px_common/qrcode/qr_generator.h"

#include "px_ui/components/button.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace px::panel::ui {
namespace {

px::ui::TextId ContextText(const ConnectionQrKind kind) noexcept {
    switch (kind) {
    case ConnectionQrKind::DesktopLink:
        return px::ui::TextId::DesktopLink;
    case ConnectionQrKind::WebClientAddress:
        return px::ui::TextId::WebClientAddress;
    }
    return px::ui::TextId::DesktopLink;
}

} // namespace

void ConnectionQrDialog::Open(std::string value, const ConnectionQrKind kind) {
    value_ = std::move(value);
    kind_ = kind;
    pixels_.clear();
    moduleCount_ = 0;
    if (!value_.empty()) {
        try {
            auto image = px::QrGenerator::GenQRImage(value_, -1);
            moduleCount_ = image.width == image.height ? image.width : 0;
            pixels_ = std::move(image.rgba);
        } catch (...) {
            pixels_.clear();
            moduleCount_ = 0;
        }
    }
    openRequested_ = true;
}

void ConnectionQrDialog::Draw(const px::ui::Localizer& localizer) {
    if (openRequested_) {
        px::ui::OpenModal({"ConnectionQrCode"});
        openRequested_ = false;
    }
    px::ui::ModalScope dialog{{"ConnectionQrCode"}, 340.0F};
    if (!dialog.Open()) {
        return;
    }

    const std::string title{std::string{localizer.Text(px::ui::TextId::QrCode)} + " (" + std::string{localizer.Text(ContextText(kind_))} + ")"};
    static_cast<void>(px::ui::DialogHeader({"close-connection-qr-header"}, title, {}, {.icon = px::ui::VectorIcon::QrCode, .closeable = false}));
    const std::size_t moduleCount{static_cast<std::size_t>(moduleCount_)};
    const std::size_t expectedPixels{std::multiplies<std::size_t>{}(std::multiplies<std::size_t>{}(moduleCount, moduleCount), 4U)};
    if (moduleCount_ > 0 && pixels_.size() == expectedPixels) {
        constexpr int quietZoneModules{4};
        const int totalModules{moduleCount_ + quietZoneModules * 2};
        const float maximumSize{px::ui::Scale(260.0F)};
        const float moduleSize{std::max(1.0F, std::floor(maximumSize / static_cast<float>(totalModules)))};
        const float qrSize{moduleSize * static_cast<float>(totalModules)};
        const float availableWidth{ImGui::GetContentRegionAvail().x};
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (availableWidth - qrSize) * 0.5F));
        const ImVec2 minimum{ImGui::GetCursorScreenPos()};
        ImGui::Dummy({qrSize, qrSize});
        auto& draw = *ImGui::GetWindowDrawList();
        draw.AddRectFilled(minimum, {minimum.x + qrSize, minimum.y + qrSize}, IM_COL32_WHITE);
        for (int row{}; row < moduleCount_; ++row) {
            for (int column{}; column < moduleCount_; ++column) {
                const int linearOffset{std::multiplies<int>{}(row, moduleCount_) + column};
                const std::size_t pixelOffset{std::multiplies<std::size_t>{}(static_cast<std::size_t>(linearOffset), 4U)};
                if (pixels_[pixelOffset] >= 128) {
                    continue;
                }
                const float left{minimum.x + static_cast<float>(column + quietZoneModules) * moduleSize};
                const float top{minimum.y + static_cast<float>(row + quietZoneModules) * moduleSize};
                draw.AddRectFilled({left, top}, {left + moduleSize, top + moduleSize}, IM_COL32_BLACK);
            }
        }
    } else {
        px::ui::MutedText(localizer.Text(px::ui::TextId::OperationFailed));
    }
    const float buttonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(buttonWidth);
    if (px::ui::ActionButton({"close-connection-qr"}, localizer.Text(px::ui::TextId::Confirm), {.width = buttonWidth})) {
        ImGui::CloseCurrentPopup();
    }
}

} // namespace px::panel::ui
