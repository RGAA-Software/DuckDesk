#include "application_launch_workflow.h"

#include <thread>
#include <utility>

#include "px_common/log.h"
#include "px_common/uuid.h"

namespace px::panel::product {

ApplicationLaunchWorkflow::ApplicationLaunchWorkflow(ApplicationLaunchOperations operations) : operations_{std::move(operations)} {}

ApplicationLaunchWorkflow::~ApplicationLaunchWorkflow() {
    if (prepared_) Close(*prepared_);
}

std::optional<std::uint64_t> ApplicationLaunchWorkflow::Begin(ApplicationLaunchRequest request) {
    const std::scoped_lock lock{mutex_};
    if (progress_ && progress_->status != ui::ApplicationLaunchStatus::Failed && progress_->status != ui::ApplicationLaunchStatus::Succeeded)
        return std::nullopt;
    request_ = std::move(request);
    preparationStarted_ = false;
    progress_ = ui::ApplicationLaunchProgress{.generation = nextGeneration_++, .applicationName = request_.applicationName};
    LOGI("Application launch started: application={} request={} generation={}", request_.applicationId, request_.requestId, progress_->generation);
    return progress_->generation;
}

bool ApplicationLaunchWorkflow::SetStage(const std::uint64_t generation, const ui::ApplicationLaunchStage stage) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation || progress_->status != ui::ApplicationLaunchStatus::Preparing) return false;
    progress_->stage = stage;
    return true;
}

void ApplicationLaunchWorkflow::Fail(const std::uint64_t generation, const px::ui::TextId error, std::string diagnostic) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation || progress_->status == ui::ApplicationLaunchStatus::Failed ||
        progress_->status == ui::ApplicationLaunchStatus::Succeeded)
        return;
    progress_->status = ui::ApplicationLaunchStatus::Failed;
    progress_->error = error;
    progress_->diagnostic = std::move(diagnostic);
    LOGW("Application launch failed: application={} request={} generation={} stage={} error={} diagnostic={}", request_.applicationId,
         request_.requestId, generation, static_cast<int>(progress_->stage), static_cast<int>(error), progress_->diagnostic);
}

void ApplicationLaunchWorkflow::ApiFailure(const std::uint64_t generation, const px_console::ConsoleApiError error) {
    if (error == px_console::ConsoleApiError::kConnectionBusy) {
        Fail(generation, px::ui::TextId::ApplicationConnectionBusy);
        return;
    }
    Fail(generation, px::ui::TextId::ApplicationLaunchRequestFailed,
         px_console::ConsoleApiLastErrorMessage().empty() ? px_console::ConsoleApiErrorAsString(error)
                                                          : std::string{px_console::ConsoleApiLastErrorMessage()});
}

void ApplicationLaunchWorkflow::Close(const px_console::ConsoleNativeApplicationConnection& connection) noexcept {
    try {
        if (operations_.close && !connection.session_id.empty()) {
            LOGI("Unlaunched application connection cleanup requested: session={} revision={}", connection.session_id, connection.session_revision);
            operations_.close(connection.session_id, connection.session_revision);
        }
    } catch (...) {
        LOGW("Unlaunched application connection cleanup threw: session={}", connection.session_id);
    }
}

bool ApplicationLaunchWorkflow::AwaitReadiness(const std::uint64_t generation, ApplicationLaunchOperations::InstanceResult& instance) {
    if (!instance) {
        ApiFailure(generation, instance.error());
        return false;
    }
    if (!SetStage(generation, ui::ApplicationLaunchStage::WaitForApplication)) return false;
    const auto deadline{std::chrono::steady_clock::now() + operations_.readinessTimeout};
    while (instance->state != "running") {
        if (instance->state == "failed" || instance->state == "stopped" || instance->state == "stopping") {
            Fail(generation, px::ui::TextId::ApplicationLaunchRejected, instance->error_code);
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            Fail(generation, px::ui::TextId::ApplicationLaunchTimedOut);
            return false;
        }
        if (operations_.wait)
            operations_.wait();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        if (!SetStage(generation, ui::ApplicationLaunchStage::WaitForApplication)) return false;
        instance = operations_.query(instance->instance_id);
        if (!instance) {
            ApiFailure(generation, instance.error());
            return false;
        }
    }
    return true;
}

void ApplicationLaunchWorkflow::Prepare(const std::uint64_t generation) {
    ApplicationLaunchRequest request{};
    {
        const std::scoped_lock lock{mutex_};
        if (!progress_ || progress_->generation != generation || progress_->status != ui::ApplicationLaunchStatus::Preparing || preparationStarted_)
            return;
        preparationStarted_ = true;
        request = request_;
    }
    try {
        ApplicationLaunchOperations::InstanceResult instance{std::unexpected{px_console::ConsoleApiError::kNotFound}};
        if (!request.existingInstanceId.empty()) {
            instance = operations_.query(request.existingInstanceId);
            if (!instance && instance.error() != px_console::ConsoleApiError::kNotFound) {
                ApiFailure(generation, instance.error());
                return;
            }
        }
        if (!SetStage(generation, ui::ApplicationLaunchStage::StartApplication)) return;
        if (!instance || (instance->state != "reserved" && instance->state != "starting" && instance->state != "running"))
            instance = operations_.start(request.applicationId, request.requestId);
        if (!AwaitReadiness(generation, instance)) return;
        const auto retirementStarted = std::chrono::steady_clock::now();
        const auto retirementDeadline = retirementStarted + operations_.retirementTimeout;
        bool waitingForRetirement{};
        ApplicationLaunchOperations::ConnectionResult connection{std::unexpected{px_console::ConsoleApiError::kConnectionRetiring}};
        while (true) {
            if (!SetStage(generation, ui::ApplicationLaunchStage::AuthorizeConnection)) return;
            connection = operations_.authorize(instance->instance_id, request.viewOnly, request.requestId);
            if (connection) break;
            if (connection.error() != px_console::ConsoleApiError::kConnectionRetiring) {
                ApiFailure(generation, connection.error());
                return;
            }
            if (!waitingForRetirement) {
                LOGI("Application launch awaiting previous connection retirement: application={} instance={} request={}", request.applicationId,
                     instance->instance_id, request.requestId);
                waitingForRetirement = true;
            }
            if (std::chrono::steady_clock::now() >= retirementDeadline) {
                Fail(generation, px::ui::TextId::ApplicationConnectionRetirementTimedOut);
                return;
            }
            {
                const std::scoped_lock lock{mutex_};
                if (!progress_ || progress_->generation != generation || progress_->status != ui::ApplicationLaunchStatus::Preparing) return;
                progress_->awaitingConnectionRetirement = true;
                progress_->retirementWaitSeconds = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - retirementStarted).count());
            }
            if (operations_.wait)
                operations_.wait();
            else
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
            if (!SetStage(generation, ui::ApplicationLaunchStage::AuthorizeConnection)) return;
            instance = operations_.query(instance->instance_id);
            while (instance && instance->state == "stopping") {
                if (std::chrono::steady_clock::now() >= retirementDeadline) {
                    Fail(generation, px::ui::TextId::ApplicationConnectionRetirementTimedOut);
                    return;
                }
                if (operations_.wait)
                    operations_.wait();
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds{500});
                if (!SetStage(generation, ui::ApplicationLaunchStage::AuthorizeConnection)) return;
                instance = operations_.query(instance->instance_id);
            }
            if (instance && instance->state == "stopped") {
                // The old Render can idle-exit while its connection slot is retiring.
                // A fresh request identity prevents replaying the stopped launch.
                request.requestId = px::GetCanonicalUUID();
                LOGI("Application stopped during connection retirement; starting a fresh instance: application={} request={}", request.applicationId,
                     request.requestId);
                instance = operations_.start(request.applicationId, request.requestId);
            }
            if (!AwaitReadiness(generation, instance)) return;
        }
        if (waitingForRetirement) {
            LOGI("Application connection retirement completed; continuing launch: application={} session={} wait_ms={}", request.applicationId,
                 connection->session_id,
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - retirementStarted).count());
        }
        if (!SetStage(generation, ui::ApplicationLaunchStage::PrepareClient)) {
            Close(*connection);
            return;
        }
        bool valid{};
        try {
            valid = !connection->host.empty() && connection->port > 0 && connection->port <= 65535 && !connection->session_id.empty() &&
                    connection->frontend_token && !connection->frontend_token->Bytes().empty() &&
                    (connection->transport != "rdp" || connection->rdp_configuration) &&
                    (!request.forceRelay ||
                     (!connection->relay_host.empty() && connection->relay_port > 0 && !connection->relay_admission_ticket.empty())) &&
                    operations_.clientAvailable();
        } catch (...) {
            valid = false;
        }
        if (!valid) {
            Close(*connection);
            Fail(generation, px::ui::TextId::ApplicationClientUnavailable);
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            if (progress_ && progress_->generation == generation && progress_->status == ui::ApplicationLaunchStatus::Preparing) {
                request_ = request;
                progress_->awaitingConnectionRetirement = false;
                prepared_ = std::move(*connection);
                progress_->status = ui::ApplicationLaunchStatus::Ready;
                return;
            }
        }
        Close(*connection);
    } catch (...) {
        Fail(generation, px::ui::TextId::ApplicationLaunchRequestFailed);
    }
}

void ApplicationLaunchWorkflow::LaunchPrepared(const std::uint64_t generation) {
    std::optional<px_console::ConsoleNativeApplicationConnection> connection{};
    ApplicationLaunchRequest request{};
    {
        const std::scoped_lock lock{mutex_};
        if (!progress_ || progress_->generation != generation || progress_->status != ui::ApplicationLaunchStatus::Ready || !prepared_) return;
        progress_->status = ui::ApplicationLaunchStatus::Launching;
        connection = std::move(prepared_);
        prepared_.reset();
        request = request_;
    }
    bool launched{};
    try {
        launched = operations_.launch(request, *connection);
    } catch (...) {
    }
    if (!launched) {
        Close(*connection);
        Fail(generation, px::ui::TextId::ConnectionClientLaunchFailed);
        return;
    }
    LOGI("Application client launched: application={} request={} session={}", request.applicationId, request.requestId, connection->session_id);
    const std::scoped_lock lock{mutex_};
    if (progress_ && progress_->generation == generation) progress_->status = ui::ApplicationLaunchStatus::Succeeded;
}

void ApplicationLaunchWorkflow::WorkerUnavailable(const std::uint64_t generation) {
    std::optional<px_console::ConsoleNativeApplicationConnection> connection{};
    {
        const std::scoped_lock lock{mutex_};
        if (!progress_ || progress_->generation != generation ||
            (progress_->status != ui::ApplicationLaunchStatus::Preparing && progress_->status != ui::ApplicationLaunchStatus::Ready))
            return;
        connection = std::move(prepared_);
        prepared_.reset();
        progress_->status = ui::ApplicationLaunchStatus::Failed;
        progress_->error = px::ui::TextId::ConnectionWorkerUnavailable;
        LOGW("Application launch worker unavailable: application={} request={} generation={}", request_.applicationId, request_.requestId,
             generation);
    }
    if (connection) Close(*connection);
}

std::optional<ui::ApplicationLaunchProgress> ApplicationLaunchWorkflow::Snapshot() const {
    const std::scoped_lock lock{mutex_};
    return progress_;
}

}  // namespace px::panel::product
