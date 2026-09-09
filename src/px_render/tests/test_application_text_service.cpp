#include "ingress/application_text_service.h"

#include <deque>
#include <limits>
#include <memory>
#include <vector>
#include <gtest/gtest.h>

namespace px {
namespace {

struct QueuedTextBackend final : public std::enable_shared_from_this<QueuedTextBackend> {
    struct Commit {
        std::string text{};
        std::string target{};
        std::function<bool()> authorized{};
        std::function<void(ApplicationTextOutcome)> complete{};
    };
    std::deque<std::function<void(ApplicationTextBackendState)>> queries{};
    std::deque<std::function<void(bool)>> releases{};
    std::deque<Commit> commits{};
    int executed{};

    ApplicationTextBackend Adapter() {
        const auto weak = weak_from_this();
        ApplicationTextBackend backend{};
        backend.kind = ApplicationTextCapabilities::CEF_COMMIT;
        backend.query = [weak](std::function<void(ApplicationTextBackendState)> callback) {
            if (const auto self = weak.lock())
                self->queries.push_back(std::move(callback));
        };
        backend.release_keys = [weak](std::function<void(bool)> callback) {
            if (const auto self = weak.lock())
                self->releases.push_back(std::move(callback));
        };
        backend.commit = [weak](std::string text, std::string target, std::function<bool()> authorized,
                                std::function<void(ApplicationTextOutcome)> callback) {
            if (const auto self = weak.lock())
                self->commits.push_back({std::move(text), std::move(target), std::move(authorized), std::move(callback)});
        };
        return backend;
    }

    void FinishQuery(const std::string& generation = "10", bool available = true) {
        ASSERT_FALSE(queries.empty());
        auto callback = std::move(queries.front());
        queries.pop_front();
        callback({generation, ApplicationTextState::EDITABLE, available});
    }

    void FinishRelease(bool success = true) {
        ASSERT_FALSE(releases.empty());
        auto callback = std::move(releases.front());
        releases.pop_front();
        callback(success);
    }

    void FinishCommit(ApplicationTextOutcome outcome = TEXT_SUBMITTED) {
        ASSERT_FALSE(commits.empty());
        auto commit = std::move(commits.front());
        commits.pop_front();
        if (commit.authorized()) {
            ++executed;
            commit.complete(outcome);
        } else
            commit.complete(TEXT_PERMISSION_DENIED);
    }
};

struct TextServiceFixture final {
    std::shared_ptr<LogicalSessionRegistry> registry = std::make_shared<LogicalSessionRegistry>();
    std::shared_ptr<QueuedTextBackend> backend = std::make_shared<QueuedTextBackend>();
    std::shared_ptr<std::vector<Message>> replies = std::make_shared<std::vector<Message>>();
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<ApplicationTextService> service{};
    LogicalSessionInputLease lease{};

    TextServiceFixture() {
        LogicalSessionGrant grant{};
        grant.logical_session_id = "controller";
        grant.stream_id = "stream";
        grant.subject_id = "user";
        grant.join_mode = "control";
        grant.expires_at_ms = std::numeric_limits<std::int64_t>::max();
        EXPECT_EQ(registry->Bind(grant, LogicalSessionTransport::kWs, "parent", false, 1).code, LogicalSessionAdmissionCode::kAccepted);
        lease = registry->FindControllerInputLeaseByBinding("parent", 2).value();
        service = std::make_shared<ApplicationTextService>("instance", registry, backend->Adapter());
    }

    ApplicationTextTarget Target() const {
        ApplicationTextTarget target{};
        target.set_instance_id("instance");
        target.set_lease_generation(std::to_string(lease.input_capability_generation));
        target.set_target_generation("10");
        return target;
    }

    void Handle(Message message, bool reliable = true) {
        service->Handle(
            std::move(message), lease, reliable, [available = alive]() { return *available; },
            [output = replies](Message reply) { output->push_back(std::move(reply)); });
    }

    Message Barrier(bool begin = true, const std::string& generation = "0") const {
        Message message{};
        message.set_type(kApplicationTextBarrier);
        auto& barrier = *message.mutable_application_text_barrier();
        barrier.set_request_id(begin ? "begin" : "end");
        *barrier.mutable_target() = Target();
        barrier.set_begin_editing(begin);
        barrier.set_expected_input_generation(generation);
        return message;
    }

    void Begin() {
        Handle(Barrier());
        backend->FinishQuery();
        backend->FinishRelease();
        ASSERT_EQ(replies->back().application_text_barrier_result().outcome(), TEXT_SUBMITTED);
    }

    Message Submit(const std::string& text = "Chinese text", const std::string& id = "request-1") const {
        Message message{};
        message.set_type(kApplicationTextSubmit);
        auto& submit = *message.mutable_application_text_submit();
        submit.set_request_id(id);
        *submit.mutable_target() = Target();
        submit.set_input_generation("1");
        submit.set_text(text);
        return message;
    }
};

TEST(ApplicationTextService, BarrierRequiresReleaseAcknowledgementAndRejectsOldInput) {
    TextServiceFixture fixture{};
    EXPECT_TRUE(fixture.service->AllowsOrdinaryInput(fixture.lease, ""));
    fixture.Handle(fixture.Barrier());
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, ""));
    fixture.backend->FinishQuery();
    EXPECT_TRUE(fixture.replies->empty());
    fixture.backend->FinishRelease();
    EXPECT_EQ(fixture.replies->back().application_text_barrier_result().input_generation(), "1");
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "1"));
    fixture.Handle(fixture.Barrier(false, "1"));
    fixture.backend->FinishQuery();
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "2"));
    fixture.backend->FinishRelease();
    EXPECT_TRUE(fixture.service->AllowsOrdinaryInput(fixture.lease, "2"));
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "1"));
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, ""));
}

TEST(ApplicationTextService, AcceptedIsNotSubmittedAndDuplicatesDoNotExecuteTwice) {
    TextServiceFixture fixture{};
    fixture.Begin();
    const auto message = fixture.Submit();
    fixture.Handle(message);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_ACCEPTED);
    EXPECT_EQ(fixture.backend->executed, 0);
    fixture.Handle(message);
    EXPECT_EQ(fixture.backend->commits.size(), 1);
    fixture.Handle(fixture.Submit("different"));
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_INVALID);
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_SUBMITTED);
    fixture.Handle(message);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_SUBMITTED);
    EXPECT_EQ(fixture.backend->executed, 1);
    EXPECT_TRUE(fixture.backend->commits.empty());
}

TEST(ApplicationTextService, RejectsInvalidUtf8ControlsAndOversizeBeforeCommit) {
    TextServiceFixture fixture{};
    fixture.Begin();
    const std::vector<std::string> invalid{"", std::string("a\0b", 3), std::string("\xc0\xaf", 2), std::string(16385, 'x'), std::string(1, '\x7f')};
    for (const auto& text : invalid) {
        fixture.Handle(fixture.Submit(text));
        EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_INVALID);
    }
    EXPECT_TRUE(fixture.backend->commits.empty());
}

TEST(ApplicationTextService, UnreliableTransportCannotSubmitOrStartBarrier) {
    TextServiceFixture fixture{};
    fixture.Handle(fixture.Barrier(), false);
    EXPECT_TRUE(fixture.backend->queries.empty());
    fixture.Begin();
    const auto count = fixture.replies->size();
    fixture.Handle(fixture.Submit(), false);
    EXPECT_TRUE(fixture.backend->commits.empty());
    EXPECT_EQ(fixture.replies->size(), count);
}

TEST(ApplicationTextService, ParentDisconnectCancelsQueuedSubmission) {
    TextServiceFixture fixture{};
    fixture.Begin();
    fixture.Handle(fixture.Submit());
    fixture.registry->CloseBindingById("parent", 3);
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_PERMISSION_DENIED);
}

TEST(ApplicationTextService, AuxiliaryDisconnectCancelsQueuedSubmission) {
    TextServiceFixture fixture{};
    fixture.Begin();
    fixture.Handle(fixture.Submit());
    *fixture.alive = false;
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
}

TEST(ApplicationTextService, RevokeAndRebindCannotReviveQueuedOldLease) {
    TextServiceFixture fixture{};
    fixture.Begin();
    fixture.Handle(fixture.Submit());
    fixture.registry->CloseBindingById("parent", 3);
    LogicalSessionGrant grant{};
    grant.logical_session_id = "controller";
    grant.stream_id = "stream";
    grant.subject_id = "user";
    grant.join_mode = "control";
    grant.expires_at_ms = std::numeric_limits<std::int64_t>::max();
    ASSERT_EQ(fixture.registry->Bind(grant, LogicalSessionTransport::kWs, "parent", false, 4).code, LogicalSessionAdmissionCode::kAccepted);
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
}

TEST(ApplicationTextService, InputCapabilityRevocationAndRestoreNeverRevivesQueuedAuthorization) {
    TextServiceFixture fixture{};
    fixture.Begin();
    fixture.Handle(fixture.Submit());
    ASSERT_EQ(fixture.backend->commits.size(), 1);
    const auto old_authorize = fixture.backend->commits.front().authorized;
    const auto old_target = fixture.Target();
    const auto old_lease = fixture.lease;
    EXPECT_TRUE(old_authorize());

    fixture.registry->UpdateInputCapabilityByStream("stream", false);
    EXPECT_FALSE(old_authorize());
    EXPECT_FALSE(fixture.registry->FindControllerInputLeaseByBinding("parent", 3));
    fixture.registry->UpdateInputCapabilityByStream("stream", true);
    EXPECT_FALSE(old_authorize());
    const auto restored = fixture.registry->FindControllerInputLeaseByBinding("parent", 4);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->generation, old_lease.generation);
    EXPECT_NE(restored->input_capability_generation, old_lease.input_capability_generation);
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_PERMISSION_DENIED);

    fixture.lease = *restored;
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    fixture.Handle(query);
    fixture.backend->FinishQuery();
    const auto new_target = fixture.replies->back().application_text_state().target();
    EXPECT_NE(new_target.lease_generation(), old_target.lease_generation());
    EXPECT_EQ(new_target.lease_generation(), std::to_string(restored->input_capability_generation));
    EXPECT_FALSE(old_authorize());
    fixture.Begin();
    auto stale = fixture.Submit("stale", "old-target");
    *stale.mutable_application_text_submit()->mutable_target() = old_target;
    fixture.Handle(stale);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_TARGET_CHANGED);
    fixture.Handle(fixture.Submit());
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 1);
}

TEST(ApplicationTextService, StopCancelsQueuedCommitAndReleaseCallbacks) {
    TextServiceFixture fixture{};
    fixture.Begin();
    fixture.Handle(fixture.Submit());
    const auto count = fixture.replies->size();
    fixture.service->Stop();
    fixture.service->Stop();
    fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
    EXPECT_EQ(fixture.replies->size(), count);
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "1"));

    TextServiceFixture releasing{};
    releasing.Handle(releasing.Barrier());
    releasing.backend->FinishQuery();
    releasing.service->Stop();
    releasing.backend->FinishRelease();
    EXPECT_TRUE(releasing.replies->empty());
}

TEST(ApplicationTextService, StopCancelsQueuedCapabilityStateReply) {
    TextServiceFixture fixture{};
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    fixture.Handle(query);
    ASSERT_EQ(fixture.replies->size(), 1);
    fixture.service->Stop();
    fixture.backend->FinishQuery();
    EXPECT_EQ(fixture.replies->size(), 1);
}

TEST(ApplicationTextService, StopFromAcceptedCallbackDoesNotDeadlockOrExecute) {
    TextServiceFixture fixture{};
    fixture.Begin();
    const auto weak = std::weak_ptr<ApplicationTextService>(fixture.service);
    fixture.service->Handle(
        fixture.Submit(), fixture.lease, true, []() { return true; },
        [weak](Message response) {
            if (response.application_text_result().outcome() == TEXT_ACCEPTED) {
                if (const auto self = weak.lock())
                    self->Stop();
            }
        });
    if (!fixture.backend->commits.empty())
        fixture.backend->FinishCommit();
    EXPECT_EQ(fixture.backend->executed, 0);
}

TEST(ApplicationTextService, ExpiredOwnerAndRepeatedConstructionAreSafe) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        TextServiceFixture fixture{};
        fixture.Begin();
        fixture.Handle(fixture.Submit());
        fixture.service.reset();
        fixture.backend->FinishCommit();
        EXPECT_EQ(fixture.backend->executed, 0);
    }
}

TEST(ApplicationTextService, FailedReleaseKeepsOrdinaryInputSuspended) {
    TextServiceFixture fixture{};
    fixture.Handle(fixture.Barrier());
    fixture.backend->FinishQuery();
    fixture.backend->FinishRelease(false);
    EXPECT_EQ(fixture.replies->back().application_text_barrier_result().outcome(), TEXT_OUTCOME_UNKNOWN);
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "1"));
}

TEST(ApplicationTextService, FailedReleaseCannotBeBlindlyRetriedButNewPrimaryLeaseRecovers) {
    TextServiceFixture fixture{};
    fixture.Handle(fixture.Barrier());
    fixture.backend->FinishQuery();
    fixture.backend->FinishRelease(false);
    fixture.Handle(fixture.Barrier(false, "1"));
    EXPECT_EQ(fixture.replies->back().application_text_barrier_result().outcome(), TEXT_BUSY);
    EXPECT_TRUE(fixture.backend->queries.empty());
    EXPECT_TRUE(fixture.backend->releases.empty());
    EXPECT_FALSE(fixture.service->AllowsOrdinaryInput(fixture.lease, "1"));

    fixture.registry->CloseBindingById("parent", 3);
    LogicalSessionGrant grant{};
    grant.logical_session_id = "controller";
    grant.stream_id = "stream";
    grant.subject_id = "user";
    grant.join_mode = "control";
    grant.expires_at_ms = std::numeric_limits<std::int64_t>::max();
    ASSERT_EQ(fixture.registry->Bind(grant, LogicalSessionTransport::kWs, "new-parent", false, 4).code, LogicalSessionAdmissionCode::kAccepted);
    const auto old_generation = fixture.lease.generation;
    fixture.lease = fixture.registry->FindControllerInputLeaseByBinding("new-parent", 5).value();
    EXPECT_NE(fixture.lease.generation, old_generation);
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    fixture.Handle(query);
    EXPECT_EQ(fixture.replies->back().application_text_capabilities().input_generation(), "0");
    fixture.backend->FinishQuery();
    fixture.Begin();
    fixture.Handle(fixture.Barrier(false, "1"));
    fixture.backend->FinishQuery();
    fixture.backend->FinishRelease();
    EXPECT_TRUE(fixture.service->AllowsOrdinaryInput(fixture.lease, "2"));
}

TEST(ApplicationTextService, RepeatedCapabilityRequestsCoalescePendingBackendQueries) {
    TextServiceFixture fixture{};
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    for (int index = 0; index < 64; ++index)
        fixture.Handle(query);
    EXPECT_EQ(fixture.backend->queries.size(), 1);
    fixture.backend->FinishQuery();
    fixture.Handle(query);
    EXPECT_EQ(fixture.backend->queries.size(), 1);
    fixture.backend->FinishQuery();
    EXPECT_TRUE(fixture.backend->queries.empty());
}

TEST(ApplicationTextService, StopFromCapabilitiesReplyDoesNotScheduleBackendQuery) {
    TextServiceFixture fixture{};
    Message query{};
    query.set_type(kApplicationTextCapabilities);
    const auto weak = std::weak_ptr<ApplicationTextService>(fixture.service);
    fixture.service->Handle(
        query, fixture.lease, true, []() { return true; },
        [weak](Message) {
            if (const auto self = weak.lock())
                self->Stop();
        });
    EXPECT_TRUE(fixture.backend->queries.empty());
}

TEST(ApplicationTextService, WrongTargetStaleGenerationAndConcurrentRequestDoNotExecute) {
    TextServiceFixture fixture{};
    fixture.Begin();
    auto wrong = fixture.Submit();
    wrong.mutable_application_text_submit()->mutable_target()->set_instance_id("other");
    fixture.Handle(wrong);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_TARGET_CHANGED);
    auto stale = fixture.Submit();
    stale.mutable_application_text_submit()->set_input_generation("0");
    fixture.Handle(stale);
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_TARGET_CHANGED);
    fixture.Handle(fixture.Submit());
    fixture.Handle(fixture.Submit("next", "request-2"));
    EXPECT_EQ(fixture.replies->back().application_text_result().outcome(), TEXT_BUSY);
    EXPECT_EQ(fixture.backend->commits.size(), 1);
}

} // namespace
} // namespace px
