//
// Created by RGAA on 22/10/2024.
//

#include "service_manager.h"
#include "px_exe_names.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include "px_common_new/log.h"

namespace px
{
    namespace {
        static const char* kServiceManagerExe = px::kPxServiceManagerExeName;

        struct ProcessResult {
            bool ok_ = false;
            int exit_code_ = -1;
            std::string stdout_{};
            std::string stderr_{};
        };

        std::string Trimmed(const std::string& value) {
            auto q = QString::fromStdString(value).trimmed();
            return q.toStdString();
        }

        ProcessResult RunServiceManager(const std::vector<std::string>& args) {
            const auto exe_path = QCoreApplication::applicationDirPath() + "/" + kServiceManagerExe;
            if (!QFileInfo::exists(exe_path)) {
                LOGE("Rust service manager not found: {}", exe_path.toStdString());
                return {};
            }

            QProcess process;
            QStringList qargs;
            for (const auto& arg : args) {
                qargs << QString::fromStdString(arg);
            }
            process.start(exe_path, qargs);
            if (!process.waitForStarted()) {
                LOGE("Start rust service manager failed: {}", exe_path.toStdString());
                return {};
            }
            process.waitForFinished();

            ProcessResult result;
            result.exit_code_ = process.exitCode();
            result.stdout_ = process.readAllStandardOutput().toStdString();
            result.stderr_ = process.readAllStandardError().toStdString();
            result.ok_ = process.exitStatus() == QProcess::NormalExit && result.exit_code_ == 0;
            if (!result.ok_) {
                LOGE("Rust service manager command failed, exit: {}, stdout: {}, stderr: {}",
                    result.exit_code_, result.stdout_, result.stderr_);
            }
            return result;
        }

        bool RunServiceManagerDetached(const std::vector<std::string>& args) {
            const auto exe_path = QCoreApplication::applicationDirPath() + "/" + kServiceManagerExe;
            if (!QFileInfo::exists(exe_path)) {
                LOGE("Rust service manager not found: {}", exe_path.toStdString());
                return false;
            }

            QStringList qargs;
            for (const auto& arg : args) {
                qargs << QString::fromStdString(arg);
            }
            const bool ok = QProcess::startDetached(exe_path, qargs, QCoreApplication::applicationDirPath());
            if (!ok) {
                LOGE("Start detached rust service manager failed: {}", exe_path.toStdString());
            }
            return ok;
        }
    }

    std::shared_ptr<ServiceManager> ServiceManager::Make() {
        return std::make_shared<ServiceManager>();
    }

    ServiceManager::ServiceManager() = default;

    void ServiceManager::Init(const std::string &srv_name, const std::string &path, const std::string &display_name,
                              const std::string &description) {
        srv_name_ = srv_name;
        srv_exe_path_ = path;
        srv_display_name_ = display_name;
        srv_description_ = description;
    }

    void ServiceManager::Install() {
        auto result = RunServiceManager({
            "install",
            "--service-bin",
            srv_exe_path_,
        });
        if (!result.ok_) {
            LOGE("Install service by rust manager failed.");
        }
    }

    void ServiceManager::Stop() {
        auto result = RunServiceManager({"stop"});
        if (!result.ok_) {
            LOGE("Stop service by rust manager failed.");
        }
    }

    void ServiceManager::StopDetached() {
        if (!RunServiceManagerDetached({"stop"})) {
            LOGE("StopDetached service by rust manager failed.");
        }
    }

    void ServiceManager::Remove(bool uninstall_service) {
        std::vector<std::string> args = {"remove"};
        if (uninstall_service) {
            args.push_back("--uninstall-service");
        }
        auto result = RunServiceManager(args);
        if (!result.ok_) {
            LOGE("Remove service by rust manager failed.");
        }
    }

    void ServiceManager::RemoveDetached(bool uninstall_service) {
        std::vector<std::string> args = {"remove"};
        if (uninstall_service) {
            args.push_back("--uninstall-service");
        }
        if (!RunServiceManagerDetached(args)) {
            LOGE("RemoveDetached service by rust manager failed.");
        }
    }

    void ServiceManager::ShutdownDetached(bool uninstall_service, uint32_t current_pid) {
        std::vector<std::string> args = {
            "shutdown",
            "--current-pid",
            std::to_string(current_pid),
        };
        if (uninstall_service) {
            args.insert(args.begin() + 1, "--uninstall-service");
        }
        if (!RunServiceManagerDetached(args)) {
            LOGE("ShutdownDetached service by rust manager failed.");
        }
    }

    ServiceStatus ServiceManager::QueryStatus() {
        auto result = RunServiceManager({"query"});
        if (!result.ok_) {
            return ServiceStatus::kUnknownStatus;
        }

        const auto status = Trimmed(result.stdout_);
        if (status == "running") {
            return ServiceStatus::kRunning;
        }
        if (status == "stopped") {
            return ServiceStatus::kStopped;
        }
        if (status == "pending") {
            return ServiceStatus::kPending;
        }
        return ServiceStatus::kUnknownStatus;
    }

    std::string ServiceManager::StatusAsString(ServiceStatus status) {
        if (status == ServiceStatus::kPending) {
            return "pending";
        }
        if (status == ServiceStatus::kStopped) {
            return "stopped";
        }
        if (status == ServiceStatus::kRunning) {
            return "running";
        }
        return "unknown";
    }

    std::optional<std::string> ServiceManager::GetServiceExecutablePath() {
        auto result = RunServiceManager({"path"});
        if (!result.ok_) {
            return {};
        }
        const auto path = Trimmed(result.stdout_);
        if (path.empty()) {
            return {};
        }
        return path;
    }
}
