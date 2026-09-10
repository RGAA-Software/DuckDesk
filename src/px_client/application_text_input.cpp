#include "application_text_input.h"

#include <QApplication>
#include <QCheckBox>
#include <QInputMethodEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <algorithm>

namespace px {

class ApplicationTextEditor final : public QPlainTextEdit {
  public:
    explicit ApplicationTextEditor(QPointer<QWidget> parent) : QPlainTextEdit(parent.data()) {}
    bool composing_{};
    QString placeholder_before_composition_{};
    std::function<void()> composition_changed_{};

  protected:
    void inputMethodEvent(QInputMethodEvent* event) override { // NOLINT(gammaray-raw-pointer-boundary) Borrowed Qt event, synchronous only.
        const bool composing{!event->preeditString().isEmpty()};
        if (composing && !composing_) {
            placeholder_before_composition_ = placeholderText();
            setPlaceholderText({});
        } else if (!composing && composing_) {
            setPlaceholderText(placeholder_before_composition_);
            placeholder_before_composition_.clear();
        }
        composing_ = composing;
        QPlainTextEdit::inputMethodEvent(event);
        if (composition_changed_)
            composition_changed_();
    }
};

namespace {
std::string RequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

bool SameSession(const ApplicationTextTarget& first, const ApplicationTextTarget& second) {
    return first.instance_id() == second.instance_id() && first.lease_generation() == second.lease_generation();
}

QString OutcomeText(ApplicationTextOutcome outcome) {
    switch (outcome) {
    case TEXT_PERMISSION_DENIED:
        return QStringLiteral("控制权限已失效，草稿已清除。");
    case TEXT_TARGET_CHANGED:
        return QStringLiteral("输入目标已变化，请关闭面板并重新选择输入框。");
    case TEXT_TARGET_UNAVAILABLE:
        return QStringLiteral("没有可用的输入目标，请先点击远端输入框。");
    case TEXT_INVALID:
        return QStringLiteral("文字无效或超过长度限制，未发送。");
    case TEXT_BUSY:
        return QStringLiteral("目标正忙，未发送；请稍后手动重试。");
    case TEXT_UNSUPPORTED:
        return QStringLiteral("此应用或服务端不支持文字输入。");
    case TEXT_OUTCOME_UNKNOWN:
        return QStringLiteral("执行结果不确定。请核对远端内容，不会自动重发。");
    default:
        return QStringLiteral("文字提交失败，请核对远端内容后再操作。");
    }
}
} // namespace

std::shared_ptr<ApplicationTextInput> ApplicationTextInput::Make(QPointer<QWidget> host, std::shared_ptr<ApplicationTextInputGate> gate,
                                                                 Sender sender, ReadOnly read_only) {
    if (!host || !gate || !sender || !read_only)
        return {};
    auto input = std::make_shared<ApplicationTextInput>();
    input->host_ = host;
    input->gate_ = std::move(gate);
    input->sender_ = std::move(sender);
    input->read_only_ = std::move(read_only);
    input->Initialize();
    return input;
}

ApplicationTextInput::~ApplicationTextInput() {
    timer_.reset();
    if (panel_)
        panel_->deleteLater();
    if (entry_)
        entry_->deleteLater();
}

void ApplicationTextInput::Initialize() {
    if (!host_)
        return;
    const auto weak = weak_from_this();
    entry_ = new QPushButton(QStringLiteral("输入文字"), host_.data()); // NOLINT(gammaray-raw-pointer-boundary) Host owns Qt child.
    entry_->setObjectName(QStringLiteral("applicationTextInputButton"));
    entry_->setFocusPolicy(Qt::NoFocus);
    entry_->setToolTip(QStringLiteral("先选择远端输入框，再打开本机输入面板；输入法选词不会传到远端。"));
    panel_ = new QWidget(host_.data()); // NOLINT(gammaray-raw-pointer-boundary) Host owns Qt child; no smart owner.
    panel_->setObjectName(QStringLiteral("applicationTextInputPanel"));
    panel_->setStyleSheet(QStringLiteral("#applicationTextInputPanel { background: #f4f5f7; border: 1px solid #657080; border-radius: 6px; }"));
    const QPointer<QLabel> title =
        new QLabel(QStringLiteral("输入文字 · 使用本机输入法选词"), panel_.data()); // NOLINT(gammaray-raw-pointer-boundary) Qt child.
    title->setGeometry(12, 10, 440, 24);
    editor_ = new ApplicationTextEditor(panel_); // NOLINT(gammaray-raw-pointer-boundary) Panel owns editor directly.
    editor_->setObjectName(QStringLiteral("applicationTextInputEditor"));
    editor_->setAccessibleName(QStringLiteral("待发送文字"));
    editor_->setPlaceholderText(QStringLiteral("在这里输入中文或其他文字，选词完成后点击发送。"));
    editor_->composition_changed_ = [weak]() {
        if (const auto self = weak.lock())
            self->Refresh();
    };
    status_ = new QLabel(panel_.data()); // NOLINT(gammaray-raw-pointer-boundary) Qt child.
    status_->setWordWrap(true);
    status_->setAccessibleName(QStringLiteral("文字输入状态"));
    send_ = new QPushButton(QStringLiteral("发送文字"), panel_.data()); // NOLINT(gammaray-raw-pointer-boundary) Qt child.
    send_->setObjectName(QStringLiteral("applicationTextInputSend"));
    close_ = new QPushButton(QStringLiteral("关闭"), panel_.data()); // NOLINT(gammaray-raw-pointer-boundary) Qt child.
    close_->setObjectName(QStringLiteral("applicationTextInputClose"));
    const QPointer<QCheckBox> hints = new QCheckBox(QStringLiteral("显示输入提示"), panel_.data()); // NOLINT(gammaray-raw-pointer-boundary) Qt child.
    hints->setChecked(true);
    hints->setGeometry(12, 262, 160, 28);
    QObject::connect(hints.data(), &QCheckBox::toggled, host_.data(), [weak](bool checked) {
        if (const auto self = weak.lock()) {
            self->hints_enabled_ = checked;
            self->Refresh();
        }
    });
    QObject::connect(entry_.data(), &QPushButton::clicked, host_.data(), [weak]() {
        if (const auto self = weak.lock())
            self->Open();
    });
    QObject::connect(send_.data(), &QPushButton::clicked, host_.data(), [weak]() {
        if (const auto self = weak.lock())
            self->Submit();
    });
    QObject::connect(close_.data(), &QPushButton::clicked, host_.data(), [weak]() {
        if (const auto self = weak.lock())
            self->Close();
    });
    QObject::connect(editor_.data(), &QPlainTextEdit::textChanged, host_.data(), [weak]() {
        if (const auto self = weak.lock()) {
            ++self->draft_revision_;
            self->Refresh();
        }
    });
    timer_ = std::make_unique<QTimer>();
    QObject::connect(timer_.get(), &QTimer::timeout, host_.data(), [weak]() {
        if (const auto self = weak.lock())
            self->Tick();
    });
    timer_->start(100);
    panel_->hide();
    Refresh();
}

void ApplicationTextInput::Connected() {
    connected_ = true;
    capabilities_.Clear();
    state_.Clear();
    generation_.clear();
    phase_ = Phase::Unavailable;
    request_id_.clear();
    ApplyGate();
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    query_.Reset();
    query_.Sent(sender_ && sender_(query), std::chrono::steady_clock::now());
    SetStatus(QStringLiteral("正在检查文字输入能力；重新连接后请再次确认远端输入框。"));
    Refresh();
}

void ApplicationTextInput::Disconnected() {
    connected_ = false;
    phase_ = Phase::Unavailable;
    request_id_.clear();
    submitted_snapshot_.clear();
    capabilities_.Clear();
    gate_->Set(true, generation_);
    SetStatus(QStringLiteral("连接已断开。未收到结果的提交可能已执行；不会自动重发。"));
    Refresh();
}

bool ApplicationTextInput::TargetValid() const {
    return !state_.target().instance_id().empty() && !state_.target().lease_generation().empty() && !state_.target().target_generation().empty();
}

void ApplicationTextInput::ApplyGate() {
    const bool control = connected_ && !panel_open_ && (phase_ == Phase::Control || phase_ == Phase::Unavailable);
    gate_->Set(!control || background_ || (read_only_ && read_only_()), generation_);
}

void ApplicationTextInput::SetStatus(QString status) {
    if (status_)
        status_->setText(std::move(status));
}

void ApplicationTextInput::Open() {
    if (!panel_ || !connected_ || (read_only_ && read_only_()))
        return;
    if (panel_open_) {
        editor_->setFocus(Qt::OtherFocusReason);
        return;
    }
    panel_open_ = true;
    panel_->show();
    panel_->raise();
    if (phase_ == Phase::Control && TargetValid())
        Barrier(true);
    else
        SetStatus(QStringLiteral("服务端尚未提供可用的文字输入能力或输入目标。"));
    ApplyGate();
    editor_->setFocus(Qt::OtherFocusReason);
    Refresh();
}

void ApplicationTextInput::Close() {
    if (phase_ == Phase::Beginning || phase_ == Phase::Sending || phase_ == Phase::Ending)
        return;
    if (phase_ == Phase::Uncertain) {
        SetStatus(QStringLiteral("输入释放未确认，必须重新连接主会话；不会尝试重发输入屏障。"));
        return;
    }
    if (phase_ == Phase::Editing) {
        Barrier(false);
        return;
    }
    panel_open_ = false;
    if (panel_)
        panel_->hide();
    ApplyGate();
    Refresh();
}

void ApplicationTextInput::Barrier(bool begin) {
    if (!connected_ || !TargetValid())
        return;
    Message message{};
    message.set_type(kApplicationTextBarrier);
    auto& barrier = *message.mutable_application_text_barrier();
    request_id_ = RequestId();
    barrier.set_request_id(request_id_);
    *barrier.mutable_target() = state_.target();
    barrier.set_begin_editing(begin);
    barrier.set_expected_input_generation(generation_);
    phase_ = begin ? Phase::Beginning : Phase::Ending;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    ApplyGate();
    SetStatus(begin ? QStringLiteral("正在暂停远端键鼠并确认输入目标…") : QStringLiteral("正在恢复远端键鼠…"));
    if (!sender_ || !sender_(message)) {
        phase_ = begin ? Phase::Control : Phase::Editing;
        request_id_.clear();
        SetStatus(QStringLiteral("控制消息未进入发送队列，请稍后重试或重新连接。"));
        ApplyGate();
    }
    Refresh();
}

void ApplicationTextInput::Submit() {
    if (!editor_ || editor_->composing_ || phase_ != Phase::Editing || background_ || !connected_ || (read_only_ && read_only_()))
        return;
    if (state_.editability() == ApplicationTextState::NOT_EDITABLE || editing_target_.SerializeAsString() != state_.target().SerializeAsString())
        return;
    const auto draft = editor_->toPlainText();
    const auto bytes = draft.toUtf8();
    if (bytes.isEmpty() || bytes.size() > capabilities_.max_utf8_bytes())
        return;
    if (std::any_of(bytes.begin(), bytes.end(), [](char value) {
            const auto byte = static_cast<unsigned char>(value);
            return (byte < 32 && byte != 9 && byte != 10 && byte != 13) || byte == 127;
        })) {
        SetStatus(QStringLiteral("文字包含不支持的控制字符，未发送。"));
        return;
    }
    Message message{};
    message.set_type(kApplicationTextSubmit);
    auto& submit = *message.mutable_application_text_submit();
    request_id_ = RequestId();
    submit.set_request_id(request_id_);
    *submit.mutable_target() = editing_target_;
    submit.set_input_generation(generation_);
    submit.set_text(bytes.toStdString());
    submitted_snapshot_ = draft;
    submitted_revision_ = draft_revision_;
    phase_ = Phase::Sending;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    SetStatus(QStringLiteral("正在提交；等待远端执行回执…"));
    if (!sender_ || !sender_(message)) {
        phase_ = Phase::Editing;
        request_id_.clear();
        submitted_snapshot_.clear();
        SetStatus(QStringLiteral("文字未进入发送队列，未发送。请稍后手动重试或重新连接。"));
    }
    Refresh();
}

void ApplicationTextInput::HandleMessage(const Message& message) {
    if (!connected_ || !host_)
        return;
    if (message.type() == kApplicationTextCapabilities && message.has_application_text_capabilities()) {
        capabilities_ = message.application_text_capabilities();
        query_.Replied(capabilities_.version() == 1 && capabilities_.final_text_supported());
        if (capabilities_.version() == 1 && capabilities_.final_text_supported() && capabilities_.max_utf8_bytes() > 0 &&
            capabilities_.max_utf8_bytes() <= 16384 && !capabilities_.input_generation().empty() && phase_ == Phase::Unavailable) {
            generation_ = capabilities_.input_generation();
            phase_ = Phase::Control;
        }
    } else if (message.type() == kApplicationTextState && message.has_application_text_state()) {
        const auto next = message.application_text_state();
        if (!draft_instance_.empty() && draft_instance_ != next.target().instance_id() && editor_)
            editor_->clear();
        draft_instance_ = next.target().instance_id();
        if (TargetValid() && !SameSession(state_.target(), next.target())) {
            if (editor_)
                editor_->clear();
            phase_ = Phase::Unavailable;
            capabilities_.Clear();
            query_.Replied(false);
            request_id_.clear();
            submitted_snapshot_.clear();
            SetStatus(QStringLiteral("应用实例或控制权限已变化，草稿已清除，请重新连接。"));
        }
        if ((phase_ == Phase::Editing || phase_ == Phase::Sending) && editing_target_.SerializeAsString() != next.target().SerializeAsString())
            SetStatus(OutcomeText(TEXT_TARGET_CHANGED));
        state_ = next;
    } else if (message.type() == kApplicationTextBarrierResult && message.has_application_text_barrier_result()) {
        const auto& result = message.application_text_barrier_result();
        if (result.request_id() != request_id_ || (phase_ != Phase::Beginning && phase_ != Phase::Ending))
            return;
        const bool began = phase_ == Phase::Beginning;
        request_id_.clear();
        if (result.outcome() == TEXT_SUBMITTED && result.editing() == began && SameSession(state_.target(), result.target()) &&
            !result.input_generation().empty()) {
            generation_ = result.input_generation();
            *state_.mutable_target() = result.target();
            editing_target_ = result.target();
            phase_ = began ? Phase::Editing : Phase::Control;
            if (!began) {
                panel_open_ = false;
                panel_->hide();
            }
            SetStatus(QStringLiteral("发送到当前远端输入框。多行文字可能被目标应用解释为提交；不会额外发送 Enter。"));
        } else if ((result.outcome() == TEXT_TARGET_CHANGED || result.outcome() == TEXT_TARGET_UNAVAILABLE || result.outcome() == TEXT_BUSY) &&
                   SameSession(state_.target(), result.target()) && !result.input_generation().empty()) {
            // A definitive rejection reports unchanged server state. It is safe
            // to close/reselect, but never automatically retry a stale target.
            generation_ = result.input_generation();
            phase_ = result.editing() ? Phase::Editing : Phase::Control;
            SetStatus(OutcomeText(result.outcome()));
        } else {
            phase_ = Phase::Uncertain;
            SetStatus(QStringLiteral("输入屏障未确认，请重新连接后继续控制；不会自动重试。"));
        }
    } else if (message.type() == kApplicationTextResult && message.has_application_text_result()) {
        const auto& result = message.application_text_result();
        if (phase_ != Phase::Sending || result.request_id() != request_id_)
            return;
        if (result.outcome() == TEXT_ACCEPTED)
            return;
        request_id_.clear();
        phase_ = Phase::Editing;
        if (result.outcome() == TEXT_SUBMITTED) {
            if (editor_ && draft_revision_ == submitted_revision_ && editor_->toPlainText() == submitted_snapshot_)
                editor_->clear();
            SetStatus(QStringLiteral("已交给远端输入接口，请确认目标文字。未额外发送 Enter。"));
        } else {
            SetStatus(OutcomeText(result.outcome()));
            if (result.outcome() == TEXT_PERMISSION_DENIED) {
                if (editor_)
                    editor_->clear();
                phase_ = Phase::Unavailable;
                capabilities_.Clear();
                query_.Replied(false);
            }
        }
        submitted_snapshot_.clear();
    }
    ApplyGate();
    Refresh();
}

void ApplicationTextInput::Tick() {
    if (!host_)
        return;
    // A different Client window (for example file transfer) is not permission
    // to submit this panel. Other video windows retain normal control when the
    // panel is closed, so do not globally require the main window to be active.
    background_ = QApplication::applicationState() != Qt::ApplicationActive || (panel_open_ && !host_->isActiveWindow());
    const auto now = std::chrono::steady_clock::now();
    // WebSocket upgrade can precede ticket admission. Retry only this read-only
    // query up to three times; a negotiated backend is then polled for target changes.
    if (connected_ && !background_ && query_.Due(now) && !(read_only_ && read_only_())) {
        Message query{};
        query.set_type(kApplicationTextCapabilities);
        query_.Sent(sender_ && sender_(query), now);
    }
    if (read_only_ && read_only_()) {
        if (editor_ && !editor_->toPlainText().isEmpty())
            editor_->clear();
    }
    if ((phase_ == Phase::Beginning || phase_ == Phase::Ending || phase_ == Phase::Sending) && std::chrono::steady_clock::now() >= deadline_) {
        const bool sending = phase_ == Phase::Sending;
        phase_ = sending ? Phase::Editing : Phase::Uncertain;
        request_id_.clear();
        SetStatus(sending ? OutcomeText(TEXT_OUTCOME_UNKNOWN) : QStringLiteral("输入屏障超时，暂停控制；请重新连接，不会自动重试。"));
    }
    ApplyGate();
    Refresh();
}

void ApplicationTextInput::Refresh() {
    if (!host_ || !panel_ || !entry_)
        return;
    const int width = std::min(480, std::max(280, host_->width() - 24));
    entry_->setGeometry(std::max(8, host_->width() - 270), 12, 250, 32);
    entry_->setText(hints_enabled_ && state_.editability() == ApplicationTextState::EDITABLE ? QStringLiteral("需要输入文字？打开输入面板")
                                                                                             : QStringLiteral("输入文字"));
    entry_->setEnabled(connected_ && capabilities_.version() == 1 && capabilities_.final_text_supported() && TargetValid() &&
                       !(read_only_ && read_only_()));
    entry_->raise();
    panel_->setGeometry(std::max(8, host_->width() - width - 20), 52, width, 300);
    editor_->setGeometry(12, 40, width - 24, 132);
    status_->setGeometry(12, 178, width - 24, 76);
    send_->setGeometry(width - 212, 262, 106, 28);
    close_->setGeometry(width - 96, 262, 84, 28);
    const auto bytes = editor_->toPlainText().toUtf8().size();
    send_->setToolTip(QStringLiteral("UTF-8：%1 / %2 字节；请先完成输入法选词。").arg(bytes).arg(capabilities_.max_utf8_bytes()));
    send_->setEnabled(phase_ == Phase::Editing && !editor_->composing_ && connected_ && !background_ && !(read_only_ && read_only_()) && bytes > 0 &&
                      bytes <= capabilities_.max_utf8_bytes() && state_.editability() != ApplicationTextState::NOT_EDITABLE &&
                      editing_target_.SerializeAsString() == state_.target().SerializeAsString());
    close_->setEnabled(phase_ != Phase::Beginning && phase_ != Phase::Sending && phase_ != Phase::Ending);
    if (panel_open_)
        panel_->raise();
}

} // namespace px
