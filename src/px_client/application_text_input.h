#pragma once

#include "application_text_input_gate.h"
#include "px_message.pb.h"
#include <QPointer>
#include <QString>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

class QWidget;
class QPushButton;
class QLabel;
class QTimer;

namespace px {

class ApplicationTextEditor;

// UI-thread workflow; transport callbacks must marshal HandleMessage to the UI.
class ApplicationTextInput final : public std::enable_shared_from_this<ApplicationTextInput> {
  public:
    using Sender = std::function<bool(const Message&)>;
    using ReadOnly = std::function<bool()>;
    static std::shared_ptr<ApplicationTextInput> Make(QPointer<QWidget> host, std::shared_ptr<ApplicationTextInputGate> gate, Sender sender,
                                                      ReadOnly read_only);
    ~ApplicationTextInput();
    void Connected();
    void Disconnected();
    void HandleMessage(const Message& message);

  private:
    enum class Phase { Unavailable, Control, Beginning, Editing, Sending, Ending, Uncertain };
    void Initialize();
    void Open();
    void Close();
    void Submit();
    void Barrier(bool begin);
    void Tick();
    void Refresh();
    void ApplyGate();
    void SetStatus(QString status);
    bool TargetValid() const;

    QPointer<QWidget> host_{};
    QPointer<QWidget> panel_{};
    QPointer<QPushButton> entry_{};
    QPointer<QPushButton> send_{};
    QPointer<QPushButton> close_{};
    QPointer<QLabel> status_{};
    QPointer<ApplicationTextEditor> editor_{};
    std::unique_ptr<QTimer> timer_{};
    std::shared_ptr<ApplicationTextInputGate> gate_{};
    Sender sender_{};
    ReadOnly read_only_{};
    ApplicationTextCapabilities capabilities_{};
    ApplicationTextState state_{};
    ApplicationTextTarget editing_target_{};
    std::string draft_instance_{};
    Phase phase_{Phase::Unavailable};
    std::string generation_{};
    std::string request_id_{};
    QString submitted_snapshot_{};
    std::uint64_t draft_revision_{};
    std::uint64_t submitted_revision_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::chrono::steady_clock::time_point next_query_{};
    std::chrono::steady_clock::time_point query_deadline_{};
    bool query_pending_{};
    bool initial_query_sent_{};
    bool connected_{};
    bool panel_open_{};
    bool hints_enabled_{true};
    bool background_{};
};

} // namespace px
