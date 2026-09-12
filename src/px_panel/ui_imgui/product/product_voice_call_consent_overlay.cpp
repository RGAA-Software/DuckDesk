#include "panel_product_runtime.h"

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
            ImGui::OpenPopup(popupId.data());
        ImGui::SetNextWindowSize(ImVec2{px::ui::Scale(480.0F), 0.0F}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(popupId.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted(localizer.Text(px::ui::TextId::VoiceCallIncoming).data());
        ImGui::Separator();
        ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::VoiceCallRequest).data());
        ImGui::TextWrapped("%s", pending->visitorDeviceId.c_str());
        ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::VoiceCallWarning).data());
        const auto remaining = std::max<std::uint64_t>(1, (pending->expiresAtUnixMs - now + 999) / 1000);
        ImGui::Text("%s %llu", localizer.Text(px::ui::TextId::VoiceCallCountdown).data(), remaining);
        if (ImGui::Button(localizer.Text(px::ui::TextId::VoiceCallReject).data(), ImVec2{px::ui::Scale(110.0F), 0.0F})) {
            runtime_->LocalServer()->ResolveVoiceCall(*pending, false, "rejected");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::VoiceCallAccept).data(), ImVec2{px::ui::Scale(110.0F), 0.0F})) {
            runtime_->LocalServer()->ResolveVoiceCall(*pending, true, {});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
