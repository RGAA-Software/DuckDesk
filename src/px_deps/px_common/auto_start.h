#ifndef GAMMARAYPREMIUM_AUTO_START_H
#define GAMMARAYPREMIUM_AUTO_START_H

#ifdef WIN32

#include <filesystem>
#include <string_view>

#include <Windows.h>
#include <taskschd.h>
#include <wrl/client.h>

#pragma comment(lib, "taskschd.lib")

namespace px {

class AutoStart final {
public:
    static bool SetAutoStart(const std::filesystem::path& executable, bool enabled);
    static bool SetAutoStartAdmin(const std::filesystem::path& executable, bool enabled);

    AutoStart();
    ~AutoStart();

    AutoStart(const AutoStart&) = delete;
    AutoStart& operator=(const AutoStart&) = delete;

    [[nodiscard]] bool IsReady() const noexcept;
    bool CreateLogonTask(std::string_view task_name, const std::filesystem::path& executable, std::string_view arguments,
                         std::string_view author);
    bool Delete(std::string_view task_name);

private:
    bool com_initialized_{false};
    Microsoft::WRL::ComPtr<ITaskService> task_service_{};
    Microsoft::WRL::ComPtr<ITaskFolder> root_folder_{};
};

}  // namespace px

#endif  // WIN32
#endif  // GAMMARAYPREMIUM_AUTO_START_H
