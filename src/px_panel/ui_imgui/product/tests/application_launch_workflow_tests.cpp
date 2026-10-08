#include <gtest/gtest.h>

#include <future>
#include <vector>

#include "application_launch_workflow.h"
#include "panel_worker.h"

namespace px::panel::product {
namespace {

struct LaunchObservations final {
    int starts{};
    int queries{};
    int authorizations{};
    int launches{};
    int closes{};
    bool ready{};
    bool rejected{};
    bool authorized{true};
    bool clientAvailable{true};
    bool clientStarts{true};
};

ApplicationLaunchOperations Operations(const std::shared_ptr<LaunchObservations>& observations) {
    return {.start =
                [observations](const std::string& applicationId, const std::string&) {
                    ++observations->starts;
                    return px_console::ConsoleUserAppInstance{.instance_id = "instance", .app_id = applicationId, .state = "starting", .revision = 1};
                },
            .query =
                [observations](const std::string& instanceId) {
                    ++observations->queries;
                    return px_console::ConsoleUserAppInstance{.instance_id = instanceId,
                                                              .app_id = "application",
                                                              .state = observations->rejected ? "failed"
                                                                       : observations->ready  ? "running"
                                                                                              : "starting",
                                                              .revision = 2};
                },
            .authorize = [observations](const std::string& instanceId, bool, const std::string&) -> ApplicationLaunchOperations::ConnectionResult {
                ++observations->authorizations;
                if (!observations->authorized) return std::unexpected{px_console::ConsoleApiError::kForbidden};
                return px_console::ConsoleNativeApplicationConnection{.host = "127.0.0.1",
                                                                      .port = 4613,
                                                                      .instance_id = instanceId,
                                                                      .session_id = "session",
                                                                      .session_revision = 2,
                                                                      .frontend_token = px::SecretBuffer::Take("test-token"),
                                                                      .transport = "native"};
            },
            .clientAvailable = [observations] { return observations->clientAvailable; },
            .launch =
                [observations](const ApplicationLaunchRequest&, const px_console::ConsoleNativeApplicationConnection&) {
                    ++observations->launches;
                    return observations->clientStarts;
                },
            .close =
                [observations](const std::string& sessionId, const std::int64_t revision) {
                    EXPECT_EQ(sessionId, "session");
                    EXPECT_EQ(revision, 2);
                    ++observations->closes;
                },
            .wait = [observations] { observations->ready = true; }};
}

ApplicationLaunchRequest Request() { return {.applicationId = "application", .applicationName = "Test App", .requestId = "request"}; }

TEST(ApplicationLaunchWorkflow, AllStagesPrepareBeforeTheUiAcknowledgesDialogClosure) {
    const auto observations = std::make_shared<LaunchObservations>();
    ApplicationLaunchWorkflow workflow{Operations(observations)};
    const auto generation = workflow.Begin(Request());
    ASSERT_TRUE(generation);
    EXPECT_FALSE(workflow.Begin(Request()));
    workflow.LaunchPrepared(*generation);
    EXPECT_EQ(observations->launches, 0);
    workflow.Prepare(*generation);
    ASSERT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Ready);
    EXPECT_EQ(workflow.Snapshot()->stage, ui::ApplicationLaunchStage::PrepareClient);
    EXPECT_EQ(observations->authorizations, 1);
    EXPECT_EQ(observations->launches, 0);
    workflow.LaunchPrepared(*generation + 1);
    EXPECT_EQ(observations->launches, 0);
    workflow.LaunchPrepared(*generation);
    workflow.LaunchPrepared(*generation);
    EXPECT_EQ(observations->launches, 1);
    EXPECT_EQ(observations->closes, 0);
    EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Succeeded);
}

TEST(ApplicationLaunchWorkflow, CapacityFailureUsesLocalizedTextWithoutEnglishDiagnosticOrClientLaunch) {
    for (const auto error : {px_console::ConsoleApiError::kGpuMemoryExhausted, px_console::ConsoleApiError::kServiceUnavailable}) {
        const auto observations = std::make_shared<LaunchObservations>();
        auto operations = Operations(observations);
        operations.start = [error](const std::string&, const std::string&) -> ApplicationLaunchOperations::InstanceResult {
            return std::unexpected{error};
        };
        ApplicationLaunchWorkflow workflow{std::move(operations)};
        const auto generation = workflow.Begin(Request());
        ASSERT_TRUE(generation);
        workflow.Prepare(*generation);
        const auto progress = workflow.Snapshot();
        ASSERT_TRUE(progress);
        EXPECT_EQ(progress->status, ui::ApplicationLaunchStatus::Failed);
        EXPECT_EQ(progress->stage, ui::ApplicationLaunchStage::StartApplication);
        EXPECT_EQ(progress->error, error == px_console::ConsoleApiError::kGpuMemoryExhausted ? px::ui::TextId::ApplicationGpuMemoryExhausted
                                                                                          : px::ui::TextId::ApplicationSchedulerUnavailable);
        EXPECT_TRUE(progress->diagnostic.empty());
        px::ui::Localizer localizer{};
        localizer.SetLanguage(px::ui::Language::SimplifiedChinese);
        const std::string chinese{localizer.Text(progress->error)};
        localizer.SetLanguage(px::ui::Language::English);
        const std::string english{localizer.Text(progress->error)};
        EXPECT_FALSE(chinese.empty());
        EXPECT_FALSE(english.empty());
        EXPECT_NE(chinese, english);
        EXPECT_EQ(observations->authorizations, 0);
        EXPECT_EQ(observations->launches, 0);
    }
}

TEST(ApplicationLaunchWorkflow, NodeFailureStaysOnReadinessStepWithoutAuthorizationOrClient) {
    const auto observations = std::make_shared<LaunchObservations>();
    observations->rejected = true;
    ApplicationLaunchWorkflow workflow{Operations(observations)};
    const auto generation = workflow.Begin(Request());
    workflow.Prepare(*generation);
    workflow.LaunchPrepared(*generation);
    EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Failed);
    EXPECT_EQ(workflow.Snapshot()->stage, ui::ApplicationLaunchStage::WaitForApplication);
    EXPECT_EQ(workflow.Snapshot()->error, px::ui::TextId::ApplicationLaunchRejected);
    EXPECT_EQ(observations->authorizations, 0);
    EXPECT_EQ(observations->launches, 0);
}

TEST(ApplicationLaunchWorkflow, RetryWaitsForAnExistingReservationWithoutStartingADuplicate) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    operations.query = [observations](const std::string& instanceId) {
        ++observations->queries;
        return px_console::ConsoleUserAppInstance{
            .instance_id = instanceId, .app_id = "application", .state = observations->ready ? "running" : "reserved", .revision = 1};
    };
    ApplicationLaunchWorkflow workflow{std::move(operations)};
    auto request = Request();
    request.existingInstanceId = "instance";
    workflow.Prepare(*workflow.Begin(std::move(request)));
    EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Ready);
    EXPECT_EQ(observations->starts, 0);
    EXPECT_EQ(observations->authorizations, 1);
}

TEST(ApplicationLaunchWorkflow, ReentrantPreparationDoesNotReserveAnotherInstanceOrConnection) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    const auto reenterPreparation = std::make_shared<std::function<void()>>();
    operations.wait = [observations, reenterPreparation] {
        (*reenterPreparation)();
        observations->ready = true;
    };
    const auto workflow = std::make_shared<ApplicationLaunchWorkflow>(std::move(operations));
    const auto generation = workflow->Begin(Request());
    const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
    *reenterPreparation = [weakWorkflow, generation = *generation] {
        if (const auto activeWorkflow = weakWorkflow.lock()) activeWorkflow->Prepare(generation);
    };
    workflow->Prepare(*generation);
    EXPECT_EQ(workflow->Snapshot()->status, ui::ApplicationLaunchStatus::Ready);
    EXPECT_EQ(observations->starts, 1);
    EXPECT_EQ(observations->authorizations, 1);
    EXPECT_EQ(observations->launches, 0);
}

TEST(ApplicationLaunchWorkflow, TimeoutAndAuthorizationRejectionDoNotLaunch) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    operations.readinessTimeout = std::chrono::milliseconds{0};
    ApplicationLaunchWorkflow timedOut{std::move(operations)};
    timedOut.Prepare(*timedOut.Begin(Request()));
    EXPECT_EQ(timedOut.Snapshot()->error, px::ui::TextId::ApplicationLaunchTimedOut);
    observations->authorized = false;
    ApplicationLaunchWorkflow rejected{Operations(observations)};
    rejected.Prepare(*rejected.Begin(Request()));
    EXPECT_EQ(rejected.Snapshot()->stage, ui::ApplicationLaunchStage::AuthorizeConnection);
    EXPECT_EQ(rejected.Snapshot()->status, ui::ApplicationLaunchStatus::Failed);
    EXPECT_EQ(observations->authorizations, 1);
    EXPECT_EQ(observations->launches, 0);
}

TEST(ApplicationLaunchWorkflow, ClientPreflightAndProcessFailureReleaseOnlyTheReservedConnection) {
    for (const bool available : {false, true}) {
        const auto observations = std::make_shared<LaunchObservations>();
        observations->clientAvailable = available;
        observations->clientStarts = false;
        ApplicationLaunchWorkflow workflow{Operations(observations)};
        const auto generation = workflow.Begin(Request());
        workflow.Prepare(*generation);
        workflow.LaunchPrepared(*generation);
        EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Failed);
        EXPECT_EQ(observations->closes, 1);
        EXPECT_EQ(observations->launches, available ? 1 : 0);
    }
}

TEST(ApplicationLaunchWorkflow, DestructionAndStoppedWorkerReleaseUnlaunchedReservations) {
    const auto observations = std::make_shared<LaunchObservations>();
    {
        ApplicationLaunchWorkflow workflow{Operations(observations)};
        workflow.Prepare(*workflow.Begin(Request()));
    }
    EXPECT_EQ(observations->closes, 1);
    {
        ApplicationLaunchWorkflow workflow{Operations(observations)};
        const auto generation = workflow.Begin(Request());
        workflow.Prepare(*generation);
        workflow.WorkerUnavailable(*generation);
        workflow.LaunchPrepared(*generation);
        EXPECT_EQ(workflow.Snapshot()->error, px::ui::TextId::ConnectionWorkerUnavailable);
    }
    EXPECT_EQ(observations->closes, 2);
    EXPECT_EQ(observations->launches, 0);
}

TEST(ApplicationLaunchWorkflow, QueuedPreparationDoesNotRetainDestroyedWorkflow) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto workflow = std::make_shared<ApplicationLaunchWorkflow>(Operations(observations));
    const auto generation = workflow->Begin(Request());
    const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
    const auto task = [weakWorkflow, generation = *generation] {
        if (const auto active = weakWorkflow.lock()) active->Prepare(generation);
    };
    workflow.reset();
    task();
    EXPECT_TRUE(weakWorkflow.expired());
    EXPECT_EQ(observations->starts, 0);
}

TEST(ApplicationLaunchWorkflow, RepeatedVisitsRejectStaleUiLaunchRequests) {
    const auto observations = std::make_shared<LaunchObservations>();
    ApplicationLaunchWorkflow workflow{Operations(observations)};
    std::uint64_t previousGeneration{};
    for (int visitIndex{}; visitIndex < 3; ++visitIndex) {
        const auto generation = workflow.Begin(Request());
        ASSERT_TRUE(generation);
        workflow.Prepare(*generation);
        workflow.WorkerUnavailable(previousGeneration);
        workflow.LaunchPrepared(previousGeneration);
        EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Ready);
        workflow.LaunchPrepared(*generation);
        previousGeneration = *generation;
    }
    EXPECT_EQ(observations->launches, 3);
    EXPECT_EQ(observations->closes, 0);
}

TEST(ApplicationLaunchWorkflow, CancellationDuringAuthorizationCannotOverwriteARetry) {
    for (const bool cancelDuringPreflight : {false, true}) {
        const auto observations = std::make_shared<LaunchObservations>();
        auto operations = Operations(observations);
        const auto cancelAndRetry = std::make_shared<std::function<void()>>();
        if (cancelDuringPreflight) {
            operations.clientAvailable = [cancelAndRetry] {
                (*cancelAndRetry)();
                return true;
            };
        } else {
            const auto authorize = operations.authorize;
            operations.authorize = [authorize, cancelAndRetry](const std::string& instanceId, bool viewOnly, const std::string& requestId) {
                auto connection = authorize(instanceId, viewOnly, requestId);
                (*cancelAndRetry)();
                return connection;
            };
        }
        const auto workflow = std::make_shared<ApplicationLaunchWorkflow>(std::move(operations));
        const auto generation = *workflow->Begin(Request());
        const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
        *cancelAndRetry = [weakWorkflow, generation] {
            if (const auto activeWorkflow = weakWorkflow.lock()) {
                activeWorkflow->WorkerUnavailable(generation);
                EXPECT_TRUE(activeWorkflow->Begin(Request()));
            }
        };
        workflow->Prepare(generation);
        EXPECT_NE(workflow->Snapshot()->generation, generation);
        EXPECT_EQ(workflow->Snapshot()->status, ui::ApplicationLaunchStatus::Preparing);
        EXPECT_EQ(observations->closes, 1);
        EXPECT_EQ(observations->launches, 0);
    }
}

TEST(ApplicationLaunchWorkflow, CancellationWhileWaitingDoesNotAuthorizeOrReplaceTheFailure) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    const auto cancelPreparation = std::make_shared<std::function<void()>>();
    operations.wait = [cancelPreparation] { (*cancelPreparation)(); };
    const auto workflow = std::make_shared<ApplicationLaunchWorkflow>(std::move(operations));
    const auto generation = *workflow->Begin(Request());
    const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
    *cancelPreparation = [weakWorkflow, generation] {
        if (const auto activeWorkflow = weakWorkflow.lock()) activeWorkflow->WorkerUnavailable(generation);
    };
    workflow->Prepare(generation);
    EXPECT_EQ(workflow->Snapshot()->error, px::ui::TextId::ConnectionWorkerUnavailable);
    EXPECT_EQ(observations->authorizations, 0);
    EXPECT_EQ(observations->queries, 0);
}

TEST(ApplicationLaunchWorkflow, LateWorkerFailureDoesNotInvalidateALaunchedClient) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    const auto reportWorkerFailure = std::make_shared<std::function<void()>>();
    operations.launch = [observations, reportWorkerFailure](const ApplicationLaunchRequest&, const px_console::ConsoleNativeApplicationConnection&) {
        ++observations->launches;
        (*reportWorkerFailure)();
        return true;
    };
    const auto workflow = std::make_shared<ApplicationLaunchWorkflow>(std::move(operations));
    const auto generation = *workflow->Begin(Request());
    const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
    *reportWorkerFailure = [weakWorkflow, generation] {
        if (const auto activeWorkflow = weakWorkflow.lock()) activeWorkflow->WorkerUnavailable(generation);
    };
    workflow->Prepare(generation);
    workflow->LaunchPrepared(generation);
    workflow->WorkerUnavailable(generation);
    EXPECT_EQ(workflow->Snapshot()->status, ui::ApplicationLaunchStatus::Succeeded);
    EXPECT_EQ(observations->closes, 0);
}

TEST(ApplicationLaunchWorkflow, ProcessLaunchFailureCanBeRetriedWithoutClosingTheNewConnection) {
    const auto observations = std::make_shared<LaunchObservations>();
    observations->clientStarts = false;
    ApplicationLaunchWorkflow workflow{Operations(observations)};
    const auto failedGeneration = *workflow.Begin(Request());
    workflow.Prepare(failedGeneration);
    workflow.LaunchPrepared(failedGeneration);
    EXPECT_EQ(observations->closes, 1);
    observations->clientStarts = true;
    const auto retryGeneration = *workflow.Begin(Request());
    workflow.Prepare(retryGeneration);
    workflow.WorkerUnavailable(failedGeneration);
    workflow.LaunchPrepared(retryGeneration);
    EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Succeeded);
    EXPECT_EQ(observations->closes, 1);
    EXPECT_EQ(observations->launches, 2);
}

TEST(ApplicationLaunchWorkflow, RetirementWaitContinuesAndRestartsAnIdleExitedInstance) {
    for (const bool instanceExits : {false, true}) {
        const auto observations = std::make_shared<LaunchObservations>();
        auto operations = Operations(observations);
        const auto authorize = operations.authorize;
        const auto launchRequests = std::make_shared<std::vector<std::string>>();
        const auto start = operations.start;
        operations.start = [start, launchRequests](const std::string& applicationId, const std::string& requestId) {
            launchRequests->push_back(requestId);
            return start(applicationId, requestId);
        };
        operations.authorize = [observations, authorize](const std::string& instanceId, const bool viewOnly,
                                                         const std::string& requestId) -> ApplicationLaunchOperations::ConnectionResult {
            if (observations->authorizations == 0) {
                ++observations->authorizations;
                return std::unexpected{px_console::ConsoleApiError::kConnectionRetiring};
            }
            return authorize(instanceId, viewOnly, requestId);
        };
        operations.query = [observations, instanceExits](const std::string& instanceId) {
            return px_console::ConsoleUserAppInstance{
                .instance_id = instanceId,
                .app_id = "application",
                .state = instanceExits && observations->authorizations == 1 && observations->starts == 1 ? "stopped" : "running",
                .revision = 2};
        };
        const auto inspectWait = std::make_shared<std::function<void()>>();
        operations.wait = [inspectWait] {
            if (*inspectWait) (*inspectWait)();
        };
        const auto workflow = std::make_shared<ApplicationLaunchWorkflow>(std::move(operations));
        const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow};
        *inspectWait = [weakWorkflow, observations] {
            if (observations->authorizations != 1) return;
            if (const auto activeWorkflow = weakWorkflow.lock()) EXPECT_TRUE(activeWorkflow->Snapshot()->awaitingConnectionRetirement);
        };
        const auto generation = *workflow->Begin(Request());
        workflow->Prepare(generation);
        ASSERT_EQ(workflow->Snapshot()->status, ui::ApplicationLaunchStatus::Ready);
        EXPECT_FALSE(workflow->Snapshot()->awaitingConnectionRetirement);
        EXPECT_EQ(observations->starts, instanceExits ? 2 : 1);
        if (instanceExits) EXPECT_NE(launchRequests->front(), launchRequests->back());
        workflow->LaunchPrepared(generation);
        EXPECT_EQ(observations->launches, 1);
    }
}

TEST(ApplicationLaunchWorkflow, BusyOrForbiddenConnectionsAreNotAutomaticallyRetried) {
    for (const auto error : {px_console::ConsoleApiError::kConnectionBusy, px_console::ConsoleApiError::kForbidden}) {
        const auto observations = std::make_shared<LaunchObservations>();
        auto operations = Operations(observations);
        operations.authorize = [observations, error](const std::string&, bool, const std::string&) -> ApplicationLaunchOperations::ConnectionResult {
            ++observations->authorizations;
            return std::unexpected{error};
        };
        ApplicationLaunchWorkflow workflow{std::move(operations)};
        workflow.Prepare(*workflow.Begin(Request()));
        EXPECT_EQ(observations->authorizations, 1);
        EXPECT_EQ(workflow.Snapshot()->status, ui::ApplicationLaunchStatus::Failed);
        EXPECT_EQ(workflow.Snapshot()->error, error == px_console::ConsoleApiError::kConnectionBusy ? px::ui::TextId::ApplicationConnectionBusy
                                                                                                    : px::ui::TextId::ApplicationLaunchRequestFailed);
    }
}

TEST(ApplicationLaunchWorkflow, RetirementTimeoutRetainsAnExplicitFailureWithoutLaunching) {
    const auto observations = std::make_shared<LaunchObservations>();
    auto operations = Operations(observations);
    operations.retirementTimeout = std::chrono::milliseconds{0};
    operations.authorize = [](const std::string&, bool, const std::string&) -> ApplicationLaunchOperations::ConnectionResult {
        return std::unexpected{px_console::ConsoleApiError::kConnectionRetiring};
    };
    ApplicationLaunchWorkflow workflow{std::move(operations)};
    workflow.Prepare(*workflow.Begin(Request()));
    EXPECT_EQ(workflow.Snapshot()->error, px::ui::TextId::ApplicationConnectionRetirementTimedOut);
    EXPECT_EQ(observations->launches, 0);
}

}  // namespace
}  // namespace px::panel::product
