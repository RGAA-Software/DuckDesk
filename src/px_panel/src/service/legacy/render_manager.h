//
// Created by RGAA on 2024-03-30.
//

#ifndef TC_SERVER_STEAM_TC_APP_MANAGER_H
#define TC_SERVER_STEAM_TC_APP_MANAGER_H

#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <QProcess>
#include "px_exe_names.h"
#include "render_process.h"
#include "px_common_new/response.h"
#include "px_common_new/concurrent_hashmap.h"

namespace px
{

    static const std::string kPxPanelName = px::kPxPanelExeName;
    static const std::string kPxRenderName = px::kPxRenderExeName;
    static const std::string kPxClientName = px::kPxClientExeName;
    static const std::string kPxOsInfoName = px::kPxOsInfoExeName;

    class ServiceContext;
    class MessageListener;
    class ProcessInfo;

    class RenderManager : public std::enable_shared_from_this<RenderManager> {
    public:
        static std::shared_ptr<RenderManager> Make(const std::shared_ptr<ServiceContext>& ctx);
        explicit RenderManager(const std::shared_ptr<ServiceContext>& ctx);
        ~RenderManager();

        // desktop
        bool StartDesktopRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        bool StopDesktopRender();
        bool ReStartDesktopRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        [[nodiscard]] bool IsDesktopRenderAlive() const;

        // others
        bool StartRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        bool ReStartRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        bool StopRender(RenderProcessId id);

        void Exit();

    private:
        void RegisterListeners();
        void CheckAliveRenders(const std::vector<std::shared_ptr<ProcessInfo>>& processes);
        // desktop
        bool StartDesktopRenderInternal(const std::string& work_dir, const std::string& app_path, const std::string& args);
        bool CheckPanelAlive(const std::vector<std::shared_ptr<ProcessInfo>>& processes);

        // others

    private:
        std::shared_ptr<ServiceContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        // desktop
        bool is_desktop_render_alive_ = false;
        std::shared_ptr<ProcessInfo> desktop_render_process_ = nullptr;
        std::string desktop_app_path_;
        std::string desktop_app_args_;
        std::string desktop_work_dir_;

        // others
        px::ConcurrentHashMap<RenderProcessId, std::shared_ptr<RenderProcess>> render_processes_;
        std::atomic_bool exiting_{false};
    };

}

#endif //TC_SERVER_STEAM_TC_APP_MANAGER_H
