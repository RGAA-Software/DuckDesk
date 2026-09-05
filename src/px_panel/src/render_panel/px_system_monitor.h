//
// Created by RGAA on 2024/3/26.
//

#ifndef TC_APPLICATION_SYSTEM_MONITOR_H
#define TC_APPLICATION_SYSTEM_MONITOR_H

#include <memory>
#include <string>
#include <deque>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include "px_common/response.h"

namespace px
{

    class Thread;
    class PxContext;
    class PxApplication;
    class VigemDriverManager;
    class MessageListener;
    class ServiceManager;
    class PxSettings;

    class PxSystemMonitor : public std::enable_shared_from_this<PxSystemMonitor> {
    public:

        static std::shared_ptr<PxSystemMonitor> Make(const std::shared_ptr<PxApplication>& app);

        explicit PxSystemMonitor(const std::shared_ptr<PxApplication>& app);
        ~PxSystemMonitor();
        void Start();
        void Exit();
        std::vector<double> GetCurrentCpuFrequency();

    private:
        static bool CheckViGEmDriver();
        bool TryConnectViGEmDriver();
        static bool GetFileVersion(const std::wstring& filePath, unsigned long& major, unsigned long& minor);
        static void InstallViGem(bool silent);
        void NotifyViGEnState(bool ok);
        void RegisterMessageListener();
        Response<bool, bool> CheckRenderAlive();
        void CheckServiceAlive();
        void StartServer();
        // bool VerifyOnlineServers();
        // void CheckOnlineServers();
        void CheckThisDeviceInfo();

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<Thread> monitor_thread_ = nullptr;
        std::atomic_bool exit_ = false;
        std::mutex exit_mutex_;
        std::condition_variable exit_cv_;

        std::shared_ptr<VigemDriverManager> vigem_driver_manager_ = nullptr;
        bool connect_vigem_success_ = false;

        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageListener> state_msg_listener_ = nullptr;
        std::shared_ptr<ServiceManager> service_manager_ = nullptr;
        std::mutex cpu_frequency_mtx_;
        std::deque<double> current_cpu_frequency_;
    };

}

#endif //TC_APPLICATION_SYSTEM_MONITOR_H
