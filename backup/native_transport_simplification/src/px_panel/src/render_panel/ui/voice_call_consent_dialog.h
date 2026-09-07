#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <QPointer>

#include "px_qt_widget/px_custom_titlebar_dialog.h"

class QLabel;
class QTimer;
class QCloseEvent;

namespace px {

struct VoiceCallConsentInfo {
    std::string visitor_device_id;
    std::string stream_id;
    std::string call_id;
    uint64_t request_id = 0;
    uint64_t expires_at_unix_ms = 0;

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] bool Matches(
        const std::string& stream, const std::string& call,
        uint64_t request) const;
};

class VoiceCallConsentDialog final : public TcCustomTitleBarDialog {
public:
    using DecisionCallback = std::function<void(bool, const std::string&)>;

    VoiceCallConsentDialog(
        VoiceCallConsentInfo info, DecisionCallback callback,
        QWidget* parent = nullptr); // NOLINT(gammaray-raw-pointer-boundary) Qt parent API

    [[nodiscard]] const VoiceCallConsentInfo& Info() const { return info_; }
    [[nodiscard]] bool Matches(
        const std::string& stream_id, const std::string& call_id,
        uint64_t request_id) const;
    void CancelWithoutDecision();
    void ShowProminently();

protected:
    void closeEvent(QCloseEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI

private:
    void UpdateCountdown();
    void Finish(bool accepted, const std::string& reason);

    VoiceCallConsentInfo info_;
    DecisionCallback callback_;
    QPointer<QLabel> countdown_label_;
    QPointer<QTimer> timer_;
    bool resolved_ = false;
};

}  // namespace px
