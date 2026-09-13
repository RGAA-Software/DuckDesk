#include "panel_product_runtime.h"

#include "px_ui/components/button.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

namespace px::panel::product {
namespace {

std::uint64_t CurrentUnixMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

class ProductVoiceCallConsentOverlay final : public ui::VoiceCallConsentOverlay {
  public:
    ProductVoiceCallConsentOverlay(std::shared_ptr<PanelProductRuntime> runtime, std::function<void()> showPanel)
        : runtime_{std::move(runtime)}, showPanel_{std::move(showPanel)} {}

    ~ProductVoiceCallConsentOverlay() override {
        if (const auto pending = runtime_->LocalServer()->PendingVoiceCall())
            runtime_->LocalServer()->ResolveVoiceCall(*pending, false, "shutdown");
    }

    void Draw(const px::ui::Localizer& localizer) override {
        constexpr std::string_view popupId{"##voice-call-consent"};
        const auto pending = runtime_->LocalServer()->PendingVoiceCall();
        if (!pending)
            return;
        if (!announced_ || announcedRequest_ != pending->requestId) {
            announced_ = true;
            announcedRequest_ = pending->requestId;
            if (showPanel_)
                showPanel_();
        }
        const auto now = CurrentUnixMilliseconds();
        if (now >= pending->expiresAtUnixMs) {
            runtime_->LocalServer()->ResolveVoiceCall(*pending, false, "timeout");
            return;
        }
        if (!ImGui::IsPopupOpen(popupId.data()))
            px::ui::OpenModal({popupId});
        px::ui::ModalScope dialog{{popupId}, 480.0F};
        if (!dialog.Open())
            return;
        static_cast<void>(px::ui::DialogHeader({"voice-call-close"}, localizer.Text(px::ui::TextId::VoiceCallIncoming),
                                               localizer.Text(px::ui::TextId::VoiceCallRequest),
                                               {.icon = px::ui::VectorIcon::Phone, .closeable = false}));
        ImGui::TextWrapped("%s", pending->visitorDeviceId.c_str());
        ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::VoiceCallWarning).data());
        const auto remaining = std::max<std::uint64_t>(1, (pending->expiresAtUnixMs - now + 999) / 1000);
        ImGui::Text("%s %llu", localizer.Text(px::ui::TextId::VoiceCallCountdown).data(), remaining);
        const float buttonWidth{px::ui::Scale(110.0F)};
        px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
        if (px::ui::ActionButton({"voice-reject"}, localizer.Text(px::ui::TextId::VoiceCallReject),
                                 {.variant = px::ui::ButtonVariant::Destructive, .icon = px::ui::VectorIcon::PhoneOff, .width = buttonWidth})) {
            runtime_->LocalServer()->ResolveVoiceCall(*pending, false, "rejected");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"voice-accept"}, localizer.Text(px::ui::TextId::VoiceCallAccept),
                                 {.icon = px::ui::VectorIcon::Phone, .width = buttonWidth})) {
            runtime_->LocalServer()->ResolveVoiceCall(*pending, true, {});
            ImGui::CloseCurrentPopup();
        }
    }

  private:
    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::function<void()> showPanel_{};
    bool announced_{};
    std::uint64_t announcedRequest_{};
};

} // namespace

std::shared_ptr<ui::VoiceCallConsentOverlay> CreateProductVoiceCallConsentOverlay(const std::shared_ptr<PanelProductRuntime>& runtime,
                                                                                  std::function<void()> showPanel) {
    return std::make_shared<ProductVoiceCallConsentOverlay>(runtime, std::move(showPanel));
}

} // namespace px::panel::product
