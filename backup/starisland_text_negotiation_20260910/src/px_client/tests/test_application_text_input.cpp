#include "application_text_input.h"
#include <QApplication>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <gtest/gtest.h>
#include <vector>

namespace px {
namespace {

struct Fixture final {
    std::unique_ptr<QWidget> host = std::make_unique<QWidget>();
    std::shared_ptr<ApplicationTextInputGate> gate = std::make_shared<ApplicationTextInputGate>();
    std::shared_ptr<std::vector<Message>> messages = std::make_shared<std::vector<Message>>();
    std::shared_ptr<bool> read_only = std::make_shared<bool>(false);
    std::shared_ptr<bool> writable = std::make_shared<bool>(true);
    std::shared_ptr<ApplicationTextInput> input{};

    Fixture() {
        host->resize(800, 600);
        input = ApplicationTextInput::Make(
            QPointer<QWidget>(host.get()), gate,
            [sent = messages, available = writable](const Message& message) {
                if (!*available)
                    return false;
                sent->push_back(message);
                return true;
            },
            [view = read_only]() { return *view; });
        input->Connected();
    }

    QPointer<QPushButton> Button(const QString& name) {
        return QPointer<QPushButton>(
            host->findChild<QPushButton*>(name)); // NOLINT(gammaray-raw-pointer-boundary) Qt child lookup immediately guarded.
    }

    QPointer<QPlainTextEdit> Editor() {
        return QPointer<QPlainTextEdit>(
            host->findChild<QPlainTextEdit*>(QStringLiteral("applicationTextInputEditor"))); // NOLINT(gammaray-raw-pointer-boundary) Qt lookup.
    }

    void Ready() {
        Message capabilities{};
        capabilities.set_type(kApplicationTextCapabilities);
        auto& caps = *capabilities.mutable_application_text_capabilities();
        caps.set_version(1);
        caps.set_final_text_supported(true);
        caps.set_max_utf8_bytes(16384);
        caps.set_input_generation("0");
        input->HandleMessage(capabilities);
        Message state{};
        state.set_type(kApplicationTextState);
        auto& target = *state.mutable_application_text_state()->mutable_target();
        target.set_instance_id("instance-a");
        target.set_lease_generation("1");
        target.set_target_generation("2");
        state.mutable_application_text_state()->set_editability(ApplicationTextState::EDITABLE);
        input->HandleMessage(state);
    }

    void AckBarrier(bool editing, const std::string& generation) {
        const auto request = messages->back().application_text_barrier();
        Message reply{};
        reply.set_type(kApplicationTextBarrierResult);
        auto& result = *reply.mutable_application_text_barrier_result();
        result.set_request_id(request.request_id());
        *result.mutable_target() = request.target();
        result.set_outcome(TEXT_SUBMITTED);
        result.set_input_generation(generation);
        result.set_editing(editing);
        input->HandleMessage(reply);
    }
};

TEST(ApplicationTextInput, UnsupportedNeverSubmitsAndCanClose) {
    Fixture fixture{};
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.Editor()->setPlainText(QStringLiteral("中文"));
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    EXPECT_EQ(fixture.messages->size(), 1);
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    EXPECT_TRUE(fixture.gate->OrdinaryInputGeneration().has_value());
}

TEST(ApplicationTextInput, BarrierSuppressesInputAndCommitIsExplicit) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    ASSERT_EQ(fixture.messages->back().type(), kApplicationTextBarrier);
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration().has_value());
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("中文🙂\n第二行"));
    EXPECT_EQ(fixture.messages->size(), 2);
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    ASSERT_EQ(fixture.messages->back().type(), kApplicationTextSubmit);
    const auto request = fixture.messages->back().application_text_submit();
    EXPECT_EQ(request.text(), QStringLiteral("中文🙂\n第二行").toUtf8().toStdString());
    EXPECT_EQ(request.input_generation(), "1");
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->size(), 3);
    fixture.Editor()->setPlainText(QStringLiteral("新草稿"));
    Message reply{};
    reply.set_type(kApplicationTextResult);
    reply.mutable_application_text_result()->set_request_id(request.request_id());
    reply.mutable_application_text_result()->set_outcome(TEXT_SUBMITTED);
    fixture.input->HandleMessage(reply);
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("新草稿"));
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration().has_value());
    fixture.AckBarrier(false, "2");
    ASSERT_TRUE(fixture.gate->OrdinaryInputGeneration().has_value());
    EXPECT_EQ(*fixture.gate->OrdinaryInputGeneration(), "2");
}

TEST(ApplicationTextInput, CompositionDoesNotSendPreeditAndCancellingDoesNotCommit) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("已有文字"));
    QInputMethodEvent composing(QStringLiteral("zhongwen"), {});
    QApplication::sendEvent(fixture.Editor().data(), &composing);
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->size(), 2);
    QInputMethodEvent cancelled{};
    QApplication::sendEvent(fixture.Editor().data(), &cancelled);
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("已有文字"));
    EXPECT_TRUE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    QInputMethodEvent committed{};
    committed.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(fixture.Editor().data(), &committed);
    EXPECT_EQ(fixture.messages->size(), 2);
}

TEST(ApplicationTextInput, PlaceholderDoesNotOverlapPreeditAndReturnsAfterCancellation) {
    Fixture fixture{};
    const auto editor{fixture.Editor()};
    const auto placeholder{editor->placeholderText()};
    ASSERT_FALSE(placeholder.isEmpty());
    QInputMethodEvent composing{QStringLiteral("nihao"), {}};
    QApplication::sendEvent(editor.data(), &composing);
    EXPECT_TRUE(editor->placeholderText().isEmpty());
    EXPECT_TRUE(editor->toPlainText().isEmpty());
    QInputMethodEvent continuing{QStringLiteral("nihaoa"), {}};
    QApplication::sendEvent(editor.data(), &continuing);
    QInputMethodEvent cancelled{};
    QApplication::sendEvent(editor.data(), &cancelled);
    EXPECT_EQ(editor->placeholderText(), placeholder);
    EXPECT_TRUE(editor->toPlainText().isEmpty());
}

TEST(ApplicationTextInput, ReadOnlyAndDisconnectNeverSend) {
    Fixture fixture{};
    fixture.Ready();
    *fixture.read_only = true;
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    EXPECT_EQ(fixture.messages->size(), 1);
    *fixture.read_only = false;
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("中文"));
    fixture.input->Disconnected();
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->size(), 2);
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration().has_value());
}

TEST(ApplicationTextInput, ByteLimitAndControlCharactersDoNotSend) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QString(6000, QChar(0x4E2D)));
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    fixture.Editor()->setPlainText(QStringLiteral("a") + QChar(0) + QStringLiteral("b"));
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->size(), 2);
    fixture.Editor()->setPlainText(QString(16384, QChar('a')));
    EXPECT_TRUE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
}

TEST(ApplicationTextInput, StaleBarrierReplyDoesNotReleaseGate) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    Message stale{};
    stale.set_type(kApplicationTextBarrierResult);
    stale.mutable_application_text_barrier_result()->set_request_id("not-the-pending-request");
    stale.mutable_application_text_barrier_result()->set_outcome(TEXT_SUBMITTED);
    stale.mutable_application_text_barrier_result()->set_input_generation("100");
    fixture.input->HandleMessage(stale);
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration().has_value());
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputClose"))->isEnabled());
}

TEST(ApplicationTextInput, DefinitiveBeginRejectionAllowsCloseWithoutAutomaticRetry) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    const auto request = fixture.messages->back().application_text_barrier();
    Message rejected{};
    rejected.set_type(kApplicationTextBarrierResult);
    auto& result = *rejected.mutable_application_text_barrier_result();
    result.set_request_id(request.request_id());
    *result.mutable_target() = request.target();
    result.set_outcome(TEXT_TARGET_CHANGED);
    result.set_input_generation("0");
    result.set_editing(false);
    fixture.input->HandleMessage(rejected);
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration());
    EXPECT_EQ(fixture.messages->size(), 2);
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    EXPECT_TRUE(fixture.gate->OrdinaryInputGeneration());
    EXPECT_EQ(fixture.messages->size(), 2);
}

TEST(ApplicationTextInput, FailedEnqueueDoesNotLeaveHealthyConnectionPermanentlyBlocked) {
    Fixture fixture{};
    fixture.Ready();
    *fixture.writable = false;
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    EXPECT_EQ(fixture.messages->size(), 1);
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    EXPECT_TRUE(fixture.gate->OrdinaryInputGeneration());
    *fixture.writable = true;
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    *fixture.writable = false;
    fixture.Editor()->setPlainText(QStringLiteral("草稿"));
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->size(), 2);
    EXPECT_TRUE(fixture.Button(QStringLiteral("applicationTextInputClose"))->isEnabled());
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("草稿"));
}

TEST(ApplicationTextInput, ReleaseFailureRequiresReconnectAndCloseNeverRetriesBarrier) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    const auto request = fixture.messages->back().application_text_barrier();
    Message uncertain{};
    uncertain.set_type(kApplicationTextBarrierResult);
    auto& result = *uncertain.mutable_application_text_barrier_result();
    result.set_request_id(request.request_id());
    *result.mutable_target() = request.target();
    result.set_outcome(TEXT_OUTCOME_UNKNOWN);
    result.set_input_generation("1");
    result.set_editing(true);
    fixture.input->HandleMessage(uncertain);
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    EXPECT_EQ(fixture.messages->size(), 2);
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration());
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
}

TEST(ApplicationTextInput, TargetNavigationRequiresExplicitReopen) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("草稿"));
    Message next{};
    next.set_type(kApplicationTextState);
    auto& state = *next.mutable_application_text_state();
    *state.mutable_target() = fixture.messages->back().application_text_barrier().target();
    state.mutable_target()->set_target_generation("3");
    state.set_editability(ApplicationTextState::EDITABLE);
    fixture.input->HandleMessage(next);
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("草稿"));
    state.mutable_target()->set_instance_id("instance-b");
    fixture.input->HandleMessage(next);
    EXPECT_TRUE(fixture.Editor()->toPlainText().isEmpty());
}

TEST(ApplicationTextInput, HookTargetTokenIsOpaqueAndPreservedExactly) {
    Fixture fixture{};
    fixture.Ready();
    Message state{};
    state.set_type(kApplicationTextState);
    auto& target = *state.mutable_application_text_state()->mutable_target();
    target.set_instance_id("instance-a");
    target.set_lease_generation("1");
    target.set_target_generation("7412:134101027412007000:734109:22");
    fixture.input->HandleMessage(state);
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    EXPECT_EQ(fixture.messages->back().application_text_barrier().target().target_generation(), target.target_generation());
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("游戏中文"));
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    EXPECT_EQ(fixture.messages->back().application_text_submit().target().target_generation(), target.target_generation());
}

TEST(ApplicationTextInput, AcceptedDoesNotClearDraftAndSubmittedClearsOnlySnapshot) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("测试"));
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    Message result{};
    result.set_type(kApplicationTextResult);
    result.mutable_application_text_result()->set_request_id(fixture.messages->back().application_text_submit().request_id());
    result.mutable_application_text_result()->set_outcome(TEXT_ACCEPTED);
    fixture.input->HandleMessage(result);
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("测试"));
    EXPECT_FALSE(fixture.Button(QStringLiteral("applicationTextInputSend"))->isEnabled());
    result.mutable_application_text_result()->set_outcome(TEXT_SUBMITTED);
    fixture.input->HandleMessage(result);
    EXPECT_TRUE(fixture.Editor()->toPlainText().isEmpty());
}

TEST(ApplicationTextInput, NewDraftWithIdenticalTextIsNotClearedByOldReceipt) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Editor()->setPlainText(QStringLiteral("重复文字"));
    fixture.Button(QStringLiteral("applicationTextInputSend"))->click();
    fixture.Editor()->clear();
    fixture.Editor()->setPlainText(QStringLiteral("重复文字"));
    Message result{};
    result.set_type(kApplicationTextResult);
    result.mutable_application_text_result()->set_request_id(fixture.messages->back().application_text_submit().request_id());
    result.mutable_application_text_result()->set_outcome(TEXT_SUBMITTED);
    fixture.input->HandleMessage(result);
    EXPECT_EQ(fixture.Editor()->toPlainText(), QStringLiteral("重复文字"));
}

TEST(ApplicationTextInput, QueuedClickAfterOwnerDestroyedAndRepeatedCreationAreSafe) {
    for (int index = 0; index < 10; ++index) {
        Fixture fixture{};
        fixture.Ready();
        const auto button = fixture.Button(QStringLiteral("applicationTextInputButton"));
        QTimer::singleShot(0, button.data(), [button]() {
            if (button)
                button->click();
        });
        fixture.input.reset();
        QApplication::processEvents();
        EXPECT_EQ(fixture.messages->size(), 1);
    }
}

TEST(ApplicationTextInput, HostDestroyedBeforeQueuedBarrierReplyIsSafe) {
    Fixture fixture{};
    fixture.Ready();
    fixture.Button(QStringLiteral("applicationTextInputButton"))->click();
    fixture.AckBarrier(true, "1");
    fixture.Button(QStringLiteral("applicationTextInputClose"))->click();
    fixture.host.reset();
    fixture.AckBarrier(false, "2");
    EXPECT_FALSE(fixture.gate->OrdinaryInputGeneration());
    fixture.input.reset();
}

TEST(ApplicationTextInputGate, CapturedOldGenerationDoesNotChangeAcrossBarrier) {
    ApplicationTextInputGate gate{};
    gate.Set(false, "1");
    const auto old = gate.OrdinaryInputGeneration();
    gate.Set(true, "2");
    EXPECT_FALSE(gate.OrdinaryInputGeneration());
    gate.Set(false, "3");
    EXPECT_EQ(*old, "1");
    EXPECT_EQ(*gate.OrdinaryInputGeneration(), "3");
}

} // namespace
} // namespace px

int main() {
    int argc = 0;
    QApplication application(argc, nullptr);
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
