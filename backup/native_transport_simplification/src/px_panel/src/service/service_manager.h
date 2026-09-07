//
// Created by RGAA on 22/10/2024.
//

#ifndef PX_SERVICE_MANAGER_H
#define PX_SERVICE_MANAGER_H

#include <memory>
#include <string>
#include <optional>

namespace px
{
    enum class ServiceStatus {
        kUnknownStatus,
        kPending,
        kRunning,
        kStopped,
    };

    class ServiceManager {
    public:
        static std::shared_ptr<ServiceManager> Make();
        ServiceManager();

        void Init(const std::string& srv_name, const std::string& path, const std::string& display_name, const std::string& description);
        void Install();
        void Stop();
        void StopDetached();
        void Remove(bool uninstall_service);
        void RemoveDetached(bool uninstall_service);
        [[nodiscard]] bool ShutdownDetached(bool uninstall_service, uint32_t current_pid);
        std::optional<std::string> GetServiceExecutablePath();
        ServiceStatus QueryStatus();

        static std::string StatusAsString(ServiceStatus status);

    private:
        std::string srv_name_;
        std::string srv_exe_path_;
        std::string srv_display_name_;
        std::string srv_description_;
    };

}

#endif //PX_SERVICE_MANAGER_H
