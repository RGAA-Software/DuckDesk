#include "application_launch_workflow.h"

#include <thread>
#include <utility>

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
    return progress_->generation;
}

void ApplicationLaunchWorkflow::SetStage(const std::uint64_t generation, const ui::ApplicationLaunchStage stage) {
    const std::scoped_lock lock{mutex_};
    if (progress_ && progress_->generation == generation) progress_->stage = stage;
}

void ApplicationLaunchWorkflow::Fail(const std::uint64_t generation, const px::ui::TextId error, std::string diagnostic) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation) return;
    progress_->status = ui::ApplicationLaunchStatus::Failed;
    progress_->error = error;
    progress_->diagnostic = std::move(diagnostic);
}

void ApplicationLaunchWorkflow::ApiFailure(const std::uint64_t generation, const px_console::ConsoleApiError error) {
    Fail(generation, px::ui::TextId::ApplicationLaunchRequestFailed,
         px_console::ConsoleApiLastErrorMessage().empty() ? px_console::ConsoleApiErrorAsString(error)
                                                          : std::string{px_console::ConsoleApiLastErrorMessage()});
}

void ApplicationLaunchWorkflow::Close(const px_console::ConsoleNativeApplicationConnection& connection) noexcept {
    try {
        if (operations_.close && !connection.session_id.empty()) operations_.close(connection.session_id, connection.session_revision);
    } catch (...) {
    }
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
        if (!instance || (instance->state != "starting" && instance->state != "running"))
            instance = operations_.start(request.applicationId, request.requestId);
        if (!instance) {
            ApiFailure(generation, instance.error());
            return;
        }
        SetStage(generation, ui::ApplicationLaunchStage::WaitForApplication);
        const auto deadline{std::chrono::steady_clock::now() + operations_.readinessTimeout};
        while (instance->state != "running") {
            if (instance->state == "failed" || instance->state == "stopped" || instance->state == "stopping") {
                Fail(generation, px::ui::TextId::ApplicationLaunchRejected, instance->error_code);
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                Fail(generation, px::ui::TextId::ApplicationLaunchTimedOut);
                return;
            }
            if (operations_.wait)
                operations_.wait();
            else
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
            instance = operations_.query(instance->instance_id);
            if (!instance) {
                ApiFailure(generation, instance.error());
                return;
            }
        }
        SetStage(generation, ui::ApplicationLaunchStage::AuthorizeConnection);
        auto connection = operations_.authorize(instance->instance_id, request.viewOnly, request.requestId);
        if (!connection) {
            ApiFailure(generation, connection.error());
            return;
        }
        SetStage(generation, ui::ApplicationLaunchStage::PrepareClient);
        bool valid{};
        try {
            valid = !connection->host.empty() && connection->port > 0 && connection->port <= 65535 && !connection->session_id.empty() &&
                    connection->frontend_token && (connection->transport != "rdp" || connection->rdp_configuration) &&
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
        const std::scoped_lock lock{mutex_};
        prepared_ = std::move(*connection);
        progress_->status = ui::ApplicationLaunchStatus::Ready;
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
    const std::scoped_lock lock{mutex_};
    if (progress_ && progress_->generation == generation) progress_->status = ui::ApplicationLaunchStatus::Succeeded;
}

void ApplicationLaunchWorkflow::WorkerUnavailable(const std::uint64_t generation) {
    std::optional<px_console::ConsoleNativeApplicationConnection> connection{};
    {
        const std::scoped_lock lock{mutex_};
        if (!progress_ || progress_->generation != generation) return;
        connection = std::move(prepared_);
        prepared_.reset();
    }
    if (connection) Close(*connection);
    Fail(generation, px::ui::TextId::ConnectionWorkerUnavailable);
}

std::optional<ui::ApplicationLaunchProgress> ApplicationLaunchWorkflow::Snapshot() const {
    const std::scoped_lock lock{mutex_};
    return progress_;
}

}  // namespace px::panel::product
