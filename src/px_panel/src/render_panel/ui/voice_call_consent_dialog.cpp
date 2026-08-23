#include "voice_call_consent_dialog.h"

#include <algorithm>

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QLabel>
#include <QTimer>

#include "px_label.h"
#include "px_pushbutton.h"
#include "no_margin_layout.h"

namespace px {

bool VoiceCallConsentInfo::IsValid() const {
    return !visitor_device_id.empty() && !stream_id.empty() && !call_id.empty() &&
        request_id != 0 && expires_at_unix_ms != 0;
}

bool VoiceCallConsentInfo::Matches(
    const std::string& stream, const std::string& call, uint64_t request) const {
    return stream_id == stream && call_id == call && request_id == request;
}

VoiceCallConsentDialog::VoiceCallConsentDialog(
    VoiceCallConsentInfo info, DecisionCallback callback, QWidget* parent)
    : TcCustomTitleBarDialog(tcTr("id_voice_call_incoming"), parent),
      info_(std::move(info)), callback_(std::move(callback)) {
    setObjectName("voice_call_consent_dialog");
    setFixedSize(480, 300);
    setWindowTitle(tcTr("id_voice_call_incoming"));
    setWindowModality(Qt::NonModal);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setAccessibleName(tcTr("id_voice_call_incoming"));
    setAccessibleDescription("px_voice_call_consent_v1");

    root_layout_->addSpacing(18);
    auto* content = new NoMarginVLayout();
    content->setContentsMargins(30, 0, 30, 0);

    auto* request_label = new QLabel(this);
    request_label->setTextFormat(Qt::PlainText);
    request_label->setWordWrap(true);
    request_label->setStyleSheet("font-size: 16px; font-weight: 700; color: #222222;");
    request_label->setText(tcTr("id_voice_call_consent_request").arg(
        QString::fromStdString(info_.visitor_device_id)));
    request_label->setAccessibleName(request_label->text());
    content->addWidget(request_label);
    content->addSpacing(16);

    auto* warning_label = new QLabel(this);
    warning_label->setTextFormat(Qt::PlainText);
    warning_label->setWordWrap(true);
    warning_label->setStyleSheet("font-size: 14px; color: #B54708;");
    warning_label->setText(tcTr("id_voice_call_consent_warning"));
    warning_label->setAccessibleName(warning_label->text());
    content->addWidget(warning_label);
    content->addSpacing(12);

    countdown_label_ = new QLabel(this);
    countdown_label_->setStyleSheet("font-size: 13px; color: #666666;");
    countdown_label_->setAccessibleName(tcTr("id_voice_call_consent_countdown_accessible"));
    content->addWidget(countdown_label_);
    root_layout_->addLayout(content);
    root_layout_->addStretch();

    auto* buttons = new NoMarginHLayout();
    buttons->addStretch();
    auto* reject = new TcPushButton(this);
    reject->setObjectName("voice_call_reject");
    reject->setText(tcTr("id_voice_call_reject"));
    reject->setAccessibleName(tcTr("id_voice_call_reject"));
    reject->setAccessibleDescription("px_voice_call_reject_v1");
    reject->setProperty("class", "danger");
    reject->setFixedSize(100, 32);
    buttons->addWidget(reject);
    buttons->addSpacing(18);

    auto* accept = new TcPushButton(this);
    accept->setObjectName("voice_call_accept");
    accept->setText(tcTr("id_voice_call_accept"));
    accept->setAccessibleName(tcTr("id_voice_call_accept"));
    accept->setAccessibleDescription("px_voice_call_accept_v1");
    accept->setFixedSize(100, 32);
    accept->setAutoDefault(false);
    accept->setDefault(false);
    buttons->addWidget(accept);
    buttons->addStretch();
    root_layout_->addLayout(buttons);
    root_layout_->addSpacing(24);

    connect(reject, &QPushButton::clicked, this, [this]() {
        Finish(false, "rejected");
    });
    connect(accept, &QPushButton::clicked, this, [this]() {
        Finish(true, "");
    });
    reject->setFocus(Qt::OtherFocusReason);

    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, [this]() { UpdateCountdown(); });
    timer_->start();
    UpdateCountdown();
}

bool VoiceCallConsentDialog::Matches(
    const std::string& stream_id, const std::string& call_id,
    uint64_t request_id) const {
    return info_.Matches(stream_id, call_id, request_id);
}

void VoiceCallConsentDialog::CancelWithoutDecision() {
    if (resolved_) {
        return;
    }
    resolved_ = true;
    if (timer_) {
        timer_->stop();
    }
    callback_ = {};
    hide();
    deleteLater();
}

void VoiceCallConsentDialog::ShowProminently() {
    show();
    raise();
    activateWindow();
    QApplication::alert(this, 0);
}

void VoiceCallConsentDialog::closeEvent(QCloseEvent* event) {
    if (!resolved_) {
        Finish(false, "rejected");
    }
    event->accept();
}

void VoiceCallConsentDialog::UpdateCountdown() {
    if (resolved_) {
        return;
    }
    const auto now = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    if (now >= info_.expires_at_unix_ms) {
        Finish(false, "timeout");
        return;
    }
    const auto remaining_ms = info_.expires_at_unix_ms - now;
    const auto remaining_seconds = std::max<uint64_t>(1, (remaining_ms + 999) / 1000);
    countdown_label_->setText(
        tcTr("id_voice_call_consent_countdown").arg(remaining_seconds));
}

void VoiceCallConsentDialog::Finish(bool accepted, const std::string& reason) {
    if (resolved_) {
        return;
    }
    resolved_ = true;
    if (timer_) {
        timer_->stop();
    }
    hide();
    auto callback = std::move(callback_);
    if (callback) {
        callback(accepted, reason);
    }
    deleteLater();
}

}  // namespace px
