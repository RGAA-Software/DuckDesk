//
// Created by RGAA on 2024/1/17.
//

#ifndef TC_SERVER_STEAM_CONTEXT_H
#define TC_SERVER_STEAM_CONTEXT_H

#include <memory>
#include <atomic>
#include <string>
#include "px_common_new/ip_util.h"
#include "px_common_new/message_notifier.h"
#include <asio2/asio2.hpp>
#include "px_common_new/expected.h"

#include <QObject>
#include <QTimer>

namespace px_relay
{
    class RelayDeviceInfo;
}

namespace px
{

    class SteamManager;
    class SharedPreference;
    class PxSettings;
    class DBGameOperator;
    class PxResources;
    class PxRenderController;
    class PxRunGameManager;
    class ServiceManager;
    class PxApplication;
    class NotifyManager;
    class PxDatabase;
    class PxConsoleManager;
    class PxEventManager;

    // Device list
    class StreamDBOperator;
    class TaskRuntime;
    class RunningStreamManager;

    class PxContext : public QObject, public std::enable_shared_from_this<PxContext> {
    public:
        explicit PxContext(QWidget* main_window);

        bool Init(const std::shared_ptr<PxApplication>& app);
        void Exit();
        std::shared_ptr<SteamManager> GetSteamManager();
        // Post task in runtime
        void PostTask(std::function<void()>&& task);

        // Post potentially blocking network work away from the dedicated DB thread.
        void PostNetworkTask(std::function<void()>&& task);

        // Post task in runtime and receive returned value
        // like:
        // auto ret = exec_task();
        // ckb_task(ret);
        void PostTask(std::function<std::any()>&& exec_task, std::function<void(std::any)>&& cbk_task);

        // Post in Qt UI Thread(Windows messages looping thread)
        void PostUITask(std::function<void()>&& task);

        // Post in Qt UI Thread(Windows messages looping thread), task will exec after specific milliseconds
        void PostUIDelayTask(std::function<void()>&& task, int ms);
        void PostDelayTask(std::function<void()>&& task, int ms);

        // Like PostTask, but always in a fixed thread
        void PostDBTask(std::function<void()>&& task);

        // Like PostTask, but always in a fixed thread
        void PostDBTask(std::function<std::any()>&& exec_task, std::function<void(std::any)>&& cbk_task);

        int GetIndexByUniqueId();
        std::vector<EthernetInfo> GetIps();
        std::string GetFirstAvailableIp();

        std::string MakeDesktopLinkMessage(const std::vector<EthernetInfo>& info = {});

        std::shared_ptr<DBGameOperator> GetDBGameManager();
        std::shared_ptr<ServiceManager> GetServiceManager();
        std::shared_ptr<PxApplication> GetApplication();
        std::shared_ptr<PxConsoleManager> GetConsoleManager();
        std::shared_ptr<PxEventManager> GetEventManager();

        template<typename T>
        void SendAppMessage(const T& m) {
            if (msg_notifier_) {
                msg_notifier_->SendAppMessage(m);
            }
        }
        std::shared_ptr<MessageNotifier> GetMessageNotifier();
        std::shared_ptr<MessageListener> ObtainMessageListener();
        std::shared_ptr<MessageListener> ObtainUIMessageListener();
        std::shared_ptr<PxRenderController> GetRenderController();
        std::shared_ptr<PxRunGameManager> GetRunGameManager();
        static std::string GetCurrentExeFolder();
        std::shared_ptr<StreamDBOperator> GetStreamDBManager();
        std::shared_ptr<RunningStreamManager> GetRunningStreamManager();
        std::shared_ptr<PxDatabase> GetDatabase();
        bool IsDatabaseReady() const { return db_ready_; }
        const std::string& GetDatabaseError() const { return db_error_; }
        bool IsPreferenceReady() const;
        // return ip address if device id is empty
        std::string GetDeviceIdOrIpAddress();

        // Display a message at right-bottom
        std::shared_ptr<NotifyManager> GetNotifyManager();
        void NotifyAppMessage(const QString& title, const QString& msg, std::function<void()>&& cbk = []() {});
        void NotifyAppErrMessage(const QString& title, const QString& msg, std::function<void()>&& cbk = []() {});

        // console
        // will add prefix: server
        // id ==> server_111333444
        // relay_host: relay server host for the device
        // relay_port: relay server port for the device
        // relay_app_key: app key for this relay server
        std::shared_ptr<px_relay::RelayDeviceInfo> GetRelayServerSideDeviceInfo(const std::string& relay_host,
                                                                             int relay_port,
                                                                             const std::string& relay_appkey,
                                                                             const std::string& device_id,
                                                                             bool show_dialog = true);

        // sp operations
        void SpPutString(const std::string& key, const std::string& value);
        std::string SpGetString(const std::string& key, const std::string& def_val = "");

        void SpPutInteger(const std::string& key, int value);
        int SpGetInteger(const std::string& key, int def_val = 0);

    private:
        void StartTimers();

    private:
        QWidget* main_window_ = nullptr;
        PxSettings* settings_ = nullptr;
        SharedPreference* sp_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<SteamManager> steam_mgr_ = nullptr;
        std::shared_ptr<TaskRuntime> task_rt_ = nullptr;
        //std::vector<EthernetInfo> ips_;
        std::shared_ptr<DBGameOperator> db_game_manager_ = nullptr;
        std::shared_ptr<PxResources> res_manager_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<PxRenderController> srv_manager_ = nullptr;
        std::shared_ptr<asio2::timer> timer_ = nullptr;
        std::shared_ptr<PxRunGameManager> run_game_manager_ = nullptr;
        std::shared_ptr<ServiceManager> service_manager_ =  nullptr;
        std::shared_ptr<StreamDBOperator> stream_db_mgr_ = nullptr;
        std::shared_ptr<RunningStreamManager> running_stream_mgr_ = nullptr;
        std::shared_ptr<NotifyManager> notify_mgr_ = nullptr;
        std::shared_ptr<PxDatabase> database_ = nullptr;
        std::shared_ptr<PxConsoleManager> console_manager_ = nullptr;
        std::shared_ptr<PxEventManager> event_manager_ = nullptr;
        bool db_ready_ = false;
        std::string db_error_;
        std::atomic_bool exiting_ = false;
    };

}
#endif //TC_SERVER_STEAM_CONTEXT_H
