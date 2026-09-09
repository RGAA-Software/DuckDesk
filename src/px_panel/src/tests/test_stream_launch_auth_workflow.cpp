#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "px_common/blocking_executor.h"
#include "render_panel/devices/stream_launch_auth_workflow.h"

namespace px {
namespace {

using namespace std::chrono_literals;

struct WorkflowEnvironment final {
    std::shared_ptr<PxAsyncRuntime> runtime = PxAsyncRuntime::Create();
    std::shared_ptr<PxBlockingExecutor> blocking = PxBlockingExecutor::Create({.thread_count = 1, .max_pending_tasks = 32});
    std::shared_ptr<StreamLaunchAuthWorkflow> workflow;

    WorkflowEnvironment() {
        EXPECT_TRUE(runtime->Start());
        workflow = StreamLaunchAuthWorkflow::Create(runtime);
        EXPECT_TRUE(workflow);
    }

    ~WorkflowEnvironment() {
        if (workflow) {
            workflow->Stop();
        }
        if (blocking) {
            blocking->RequestStop(PxBlockingShutdownMode::kCancelPending);
            blocking->Join();
        }
        if (runtime) {
            runtime->RequestStop();
            runtime->Join();
        }
    }
};

template <typename T> T Wait(std::future<T>& future) {
    EXPECT_EQ(future.wait_for(3s), std::future_status::ready);
    return future.get();
}

StreamLaunchResolvedTicket Resolve(px_console::ConsoleConnectionTicket ticket) {
    return StreamLaunchResolvedTicket{
        .ticket = std::move(ticket),
        .host = "10.0.0.90",
        .port = 20371,
        .remote_device_id = "001190520",
    };
}

px_console::ConsoleConnectionTicket Ticket() {
    return px_console::ConsoleConnectionTicket{
        .ticket = "short-lived-ticket",
        .renewal_token = "rotating-renewal-capability",
        .launch_url = "http://10.0.0.90:20371/connect?deviceId=001190520",
        .stream_id = "stream-ticket-session",
        .permissions = {"view", "input"},
    };
}

StreamLaunchAuthHooks BaseHooks(const std::shared_ptr<PxBlockingExecutor>& blocking) {
    StreamLaunchAuthHooks hooks;
    hooks.post_blocking = [blocking](std::function<void()> task) {
        if (blocking->TryPost(std::move(task)) != PxBlockingSubmitResult::kAccepted) {
            throw std::runtime_error("blocking worker rejected task");
        }
    };
    hooks.start_app = [](const std::string&, const std::string&) {
        return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success({
            .instance_id = "instance-1",
            .state = "running",
        });
    };
    hooks.query_apps = []() { return StreamLaunchConsoleCall<std::vector<px_console::ConsoleUserApplication>>::Success({}); };
    hooks.issue_instance_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(Ticket());
    };
    hooks.issue_device_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(Ticket());
    };
    hooks.resolve_ticket = [](px_console::ConsoleConnectionTicket ticket, StreamLaunchTicketTarget) {
        return PxResult<StreamLaunchResolvedTicket>::Success(Resolve(std::move(ticket)));
    };
    hooks.probe_direct = [](const std::string&, int) { return true; };
    return hooks;
}

StreamLaunchAuthRequest DeviceRequest() {
    return StreamLaunchAuthRequest{
        .target = StreamLaunchTicketTarget::kDevice,
        .device_id = "001190520",
        .client_nonce = "nonce",
        .permissions = {"view", "input"},
        .deadline = std::chrono::steady_clock::now() + 2s,
    };
}

StreamLaunchAuthRequest AppRequest() {
    return StreamLaunchAuthRequest{
        .target = StreamLaunchTicketTarget::kApplicationInstance,
        .app_id = "app-1",
        .client_nonce = "nonce",
        .permissions = {"view"},
        .deadline = std::chrono::steady_clock::now() + 2s,
    };
}

TEST(StreamLaunchAuthWorkflow, DeviceTicketAndDirectProbeCompleteOnce) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    const auto generation = env.workflow->Start(DeviceRequest(), std::move(hooks),
                                                [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); });
    ASSERT_TRUE(generation);
    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().generation, *generation);
    EXPECT_EQ(result.Value().resolved.host, "10.0.0.90");
    EXPECT_EQ(result.Value().resolved.port, 20371);
    EXPECT_TRUE(result.Value().direct_available);
}

TEST(StreamLaunchAuthWorkflow, ApplicationPollsUntilMatchingInstanceRuns) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    auto queries = std::make_shared<std::atomic_int>(0);
    hooks.start_app = [](const std::string&, const std::string&) {
        return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success({
            .instance_id = "instance-1",
            .state = "starting",
        });
    };
    hooks.query_apps = [queries]() {
        const int query = queries->fetch_add(1) + 1;
        auto instance = std::make_shared<px_console::ConsoleUserAppInstance>(px_console::ConsoleUserAppInstance{
            .instance_id = "instance-1",
            .state = query >= 2 ? "running" : "starting",
        });
        return StreamLaunchConsoleCall<std::vector<px_console::ConsoleUserApplication>>::Success({{
            .app_id = "app-1",
            .running_instance = std::move(instance),
        }});
    };
    auto request = AppRequest();
    request.deadline = std::chrono::steady_clock::now() + 4s;
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(std::move(request), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_TRUE(result);
    ASSERT_TRUE(result.Value().instance);
    EXPECT_EQ(result.Value().instance->state, "running");
    EXPECT_EQ(queries->load(), 2);
}

TEST(StreamLaunchAuthWorkflow, RdpTicketSkipsNativeHostConfigurationProbe) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    const auto probes = std::make_shared<std::atomic_int>(0);
    hooks.probe_direct = [probes](const std::string&, int) { ++*probes; return false; };
    hooks.issue_instance_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        auto ticket = Ticket();
        ticket.rdp_configuration = std::make_shared<SecretBuffer>("test-only-private-bootstrap");
        ticket.permissions.push_back("rdp");
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(std::move(ticket));
    };
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(AppRequest(), std::move(hooks),
        [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.Value().direct_available);
    EXPECT_NE(result.Value().resolved.ticket.rdp_configuration, nullptr);
    EXPECT_EQ(probes->load(), 0);
}

TEST(StreamLaunchAuthWorkflow, ApplicationRejectsMissingInstanceId) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    hooks.start_app = [](const std::string&, const std::string&) {
        return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success({
            .state = "starting",
        });
    };
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(AppRequest(), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kProtocolError);
    EXPECT_EQ(result.Error().stage, "stream-launch.start-app");
    EXPECT_EQ(result.Error().StableCode(), "INVALID_APP_INSTANCE");
}

TEST(StreamLaunchAuthWorkflow, NativeTicketAlwaysProbesDirectEndpoint) {
    WorkflowEnvironment env{};
    auto hooks = BaseHooks(env.blocking);
    const auto probes = std::make_shared<std::atomic_int>(0);
    hooks.resolve_ticket = [](px_console::ConsoleConnectionTicket ticket, StreamLaunchTicketTarget) {
        ticket.rtc_ice_config_json = R"({"direct_probe_enabled":false})";
        ticket.relay_host = "relay.example.com";
        return PxResult<StreamLaunchResolvedTicket>::Success(Resolve(std::move(ticket)));
    };
    hooks.probe_direct = [probes](const std::string&, int) {
        ++*probes;
        return true;
    };
    const auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    const auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.Value().direct_available);
    EXPECT_EQ(probes->load(), 1);
}

TEST(StreamLaunchAuthWorkflow, UnreachableEndpointDoesNotBecomeRelaySuccess) {
    WorkflowEnvironment env{};
    auto hooks = BaseHooks(env.blocking);
    hooks.probe_direct = [](const std::string&, int) { return false; };
    const auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    const auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result.Value().direct_available);
}

TEST(StreamLaunchAuthWorkflow, ConsoleFailurePreservesStageAndApiCode) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    hooks.issue_device_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(px_console::ConsoleApiError::kAuthenticationRequired,
                                                                                     "session expired");
    };
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().stage, "stream-launch.issue-device-ticket");
    EXPECT_EQ(result.Error().message, "session expired");
    EXPECT_EQ(result.Error().StableCode(), std::to_string(static_cast<int>(px_console::ConsoleApiError::kAuthenticationRequired)));
}

TEST(StreamLaunchAuthWorkflow, ApplicationRunningDeadlineIsTypedTimeout) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    hooks.start_app = [](const std::string&, const std::string&) {
        return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Success({
            .instance_id = "instance-1",
            .state = "starting",
        });
    };
    auto request = AppRequest();
    request.deadline = std::chrono::steady_clock::now() + 30ms;
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(std::move(request), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_EQ(result.Error().stage, "stream-launch.wait-running");
    EXPECT_TRUE(result.Error().retryable);
}

TEST(StreamLaunchAuthWorkflow, NewGenerationCancelsLateOlderResult) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    auto calls = std::make_shared<std::atomic_int>(0);
    hooks.issue_device_ticket = [calls](const std::string&, const std::string&, const std::vector<std::string>&) {
        if (calls->fetch_add(1) == 0) {
            std::this_thread::sleep_for(80ms);
        }
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(Ticket());
    };
    auto firstPromise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto secondPromise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto first = firstPromise->get_future();
    auto second = secondPromise->get_future();
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), hooks,
                                    [firstPromise](std::uint64_t, StreamLaunchAuthResult result) { firstPromise->set_value(std::move(result)); }));
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), std::move(hooks),
                                    [secondPromise](std::uint64_t, StreamLaunchAuthResult result) { secondPromise->set_value(std::move(result)); }));
    auto firstResult = Wait(first);
    auto secondResult = Wait(second);
    ASSERT_FALSE(firstResult);
    EXPECT_EQ(firstResult.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_TRUE(secondResult);
}

TEST(StreamLaunchAuthWorkflow, StopWithQueuedWorkIsSafe) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    hooks.issue_device_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        std::this_thread::sleep_for(80ms);
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(Ticket());
    };
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(DeviceRequest(), std::move(hooks),
                                    [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    env.workflow->Stop();
    auto result = Wait(future);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
}

TEST(StreamLaunchAuthWorkflow, RepeatedLifecycleTenRounds) {
    for (int round = 0; round < 10; ++round) {
        WorkflowEnvironment env;
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        ASSERT_TRUE(env.workflow->Start(DeviceRequest(), BaseHooks(env.blocking),
                                        [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(result.HasValue()); }));
        EXPECT_TRUE(Wait(future));
        env.workflow->Stop();
        EXPECT_FALSE(env.workflow->Start(DeviceRequest(), BaseHooks(env.blocking), [](std::uint64_t, StreamLaunchAuthResult) {}));
    }
}

std::shared_ptr<RdpLaunchRecovery> Recovery() {
    return std::make_shared<RdpLaunchRecovery>(RdpLaunchRecovery{
        .app_id = "app-1", .instance_id = "instance-1", .nonce = "original-nonce", .logical_id = "logical-one",
        .stream_id = "stream-ticket-session", .host = "10.0.0.90", .port = 32016, .device_id = "001190520",
        .renewal = SecretBuffer::Take("recovery-capability"), .configuration = SecretBuffer::Take("protected-test-configuration"),
    });
}

StreamLaunchAuthRequest RecoveryRequest() {
    auto request = AppRequest();
    request.recovery = Recovery();
    request.instance_id = request.recovery->instance_id;
    request.client_nonce = request.recovery->nonce;
    return request;
}

TEST(RdpRecoveryStore, OnlyClosedOwnerCanConsumeOnceAndExpiryReleasesSecrets) {
    auto store = RdpRecoveryStore{};
    const auto now = RdpRecoveryStore::Clock::now();
    auto recovery = Recovery();
    const auto secret = std::weak_ptr<const SecretBuffer>{recovery->configuration};
    store.Remember(recovery);
    EXPECT_FALSE(store.Take("app-1", now));
    store.Closed("app-1", "old-stream", now);
    EXPECT_FALSE(store.Take("app-1", now));
    store.Closed("app-1", recovery->stream_id, now);
    EXPECT_EQ(store.Take("app-1", now + 1s), recovery);
    EXPECT_FALSE(store.Take("app-1", now + 1s));
    store.Remember(recovery);
    store.Closed("app-1", recovery->stream_id, now);
    recovery.reset();
    EXPECT_FALSE(secret.expired());
    EXPECT_FALSE(store.Take("app-1", now + 5s));
    EXPECT_TRUE(secret.expired());
}

TEST(RdpRecoveryStore, IdentityChangeAndOldCloseCannotReviveOrExpireReplacement) {
    auto store = RdpRecoveryStore{};
    const auto now = RdpRecoveryStore::Clock::now();
    auto recovery = Recovery();
    store.Remember(recovery);
    store.Clear();
    store.Closed("app-1", recovery->stream_id, now);
    EXPECT_FALSE(store.Take("app-1", now));
    auto replacement = Recovery();
    replacement->stream_id = "replacement-stream";
    store.Remember(replacement);
    store.Closed("app-1", recovery->stream_id, now);
    EXPECT_FALSE(store.Take("app-1", now + 1s));
    store.Closed("app-1", replacement->stream_id, now);
    EXPECT_EQ(store.Take("app-1", now + 1s), replacement);
}

TEST(StreamLaunchAuthWorkflow, RdpRecoveryRenewsOriginalOwnerWithoutStartingOrIssuingNewTicket) {
    WorkflowEnvironment env;
    auto hooks = BaseHooks(env.blocking);
    hooks.start_app = [](const std::string&, const std::string&) {
        ADD_FAILURE() << "Recovery must not start another instance";
        return StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>::Failure(px_console::ConsoleApiError::kInternalError);
    };
    hooks.issue_instance_ticket = [](const std::string&, const std::string&, const std::vector<std::string>&) {
        ADD_FAILURE() << "Recovery must not issue a different logical owner";
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Failure(px_console::ConsoleApiError::kInternalError);
    };
    hooks.renew_rdp_ticket = [](const RdpLaunchRecovery& recovery) {
        EXPECT_EQ(recovery.nonce, "original-nonce");
        EXPECT_EQ(recovery.renewal->View(), "recovery-capability");
        auto ticket = Ticket();
        ticket.logical_session_id = recovery.logical_id;
        ticket.join_mode = "control";
        ticket.permissions.push_back("rdp");
        return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(std::move(ticket));
    };
    auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(env.workflow->Start(RecoveryRequest(), std::move(hooks),
        [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().client_nonce, "original-nonce");
    EXPECT_EQ(result.Value().resolved.ticket.logical_session_id, "logical-one");
    EXPECT_EQ(result.Value().resolved.ticket.rdp_configuration->View(), "protected-test-configuration");
    EXPECT_EQ(result.Value().resolved.port, 32016);
}

TEST(StreamLaunchAuthWorkflow, RdpRecoveryRejectsChangedBindingAndCancellation) {
    for (const bool cancel : {false, true}) {
        WorkflowEnvironment env;
        auto hooks = BaseHooks(env.blocking);
        hooks.renew_rdp_ticket = [](const RdpLaunchRecovery&) {
            std::this_thread::sleep_for(50ms);
            auto ticket = Ticket();
            ticket.logical_session_id = "another-owner";
            return StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>::Success(std::move(ticket));
        };
        auto promise = std::make_shared<std::promise<StreamLaunchAuthResult>>();
        auto future = promise->get_future();
        ASSERT_TRUE(env.workflow->Start(RecoveryRequest(), std::move(hooks),
            [promise](std::uint64_t, StreamLaunchAuthResult result) { promise->set_value(std::move(result)); }));
        if (cancel) { env.workflow->Stop(); }
        auto result = Wait(future);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.Error().code, cancel ? PxAsyncErrorCode::kCancelled : PxAsyncErrorCode::kProtocolError);
    }
}

} // namespace
} // namespace px
