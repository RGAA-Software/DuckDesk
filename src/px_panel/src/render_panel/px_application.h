//
// Created by RGAA on 2024/1/17.
//

#ifndef TC_SERVER_STEAM_GR_APPLICATION_H
#define TC_SERVER_STEAM_GR_APPLICATION_H

#include <memory>
#include <QTimer>
#include <QObject>
#include <QAbstractNativeEventFilter>
#include "px_common_new/message_notifier.h"

namespace px
{

    class Data;
    class Thread;
    class PxContext;
    class WsPanelServer;
    class UdpBroadcaster;
    class PxRenderController;
    class PxSettings;
    class PxSystemMonitor;
    class PxServiceClient;
    class WsSigClient;
    class MgrClientSdk;
    class MessageListener;
    class WinMessageLoop;
    class PxConnectedManager;
    class PxBaseStreamMessage;
    class PxRenderMsgProcessor;
    class ClipboardManager;
    class PanelCompanion;
    class PxConsoleClient;
    class ConsoleScanner;
    class SkinInterface;
    class PxUserManager;
    class PxDeviceManager;
    class MonitorRefresher;

    class PxApplication : public QObject, public QAbstractNativeEventFilter, public std::enable_shared_from_this<PxApplication> {
    public:

        static std::shared_ptr<PxApplication> Make(QWidget* main_window, bool run_automatically, const std::string& skin_name = "");

        ~PxApplication() override;

        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
        void PrepareForShutdown();
        void Exit();

        std::shared_ptr<PxContext> GetContext() { return context_; }
        std::shared_ptr<WsPanelServer> GetWsPanelServer() { return ws_panel_server_; }
        std::shared_ptr<PxServiceClient> GetServiceClient() { return service_client_; }
        std::shared_ptr<PxRenderMsgProcessor> GetRenderMsgProcessor() { return rd_msg_processor_; }
        std::shared_ptr<ClipboardManager> GetClipboardManager() { return clipboard_mgr_; }
        std::shared_ptr<WinMessageLoop> GetWinMessageLoop() { return win_msg_loop_; }
        bool IsServiceConnected() const;
        // panel -> service
        // msg: protobuf message
        bool PostMessage2Service(const std::string& msg);
        bool IsRendererConnected();
        // panel -> render
        // msg: protobuf message
        bool PostMessage2Renderer(std::shared_ptr<Data> msg);
        bool RequestNewClientId(bool force_update, bool sync = false);
        std::shared_ptr<MessageNotifier> GetMessageNotifier();

        // 1. device id is empty ?
        // 2. device id & password paired ?
        bool CheckLocalDeviceInfoWithPopup();

        // compare local safety password and password in pr server
        // refresh server if they are not equal
        // 1. when the app starts and has pr server info
        // 2. when pr server info is obtained
        void UpdateServerSecurityPasswordIfNeeded();

        // send the message to remote render in json format
        bool PostMessage2RemoteRender(const std::shared_ptr<PxBaseStreamMessage>& msg);

        // companion for private logics
        PanelCompanion* GetCompanion();

        // get appkey from companion
        std::string GetAppkey();

        // refresh console server host/port/appkey...
        void RefreshClientManagerSettings();

        // console scanner
        std::shared_ptr<ConsoleScanner> GetConsoleScanner();

        // skin
        SkinInterface* GetSkin();
        std::string GetSkinName();

        // console ws client alive or not
        bool IsConsoleClientAlive();

        // user manager
        std::shared_ptr<PxUserManager> GetUserManager();

        // device info valid or not
        bool IsDeviceInfoOk();

        // device manager
        std::shared_ptr<PxDeviceManager> GetDeviceManager();

        // can we connect the console server
        // Attention: Block to request a net request.
        [[nodiscard]] bool CanConnectConsoleServer();

    protected:
        explicit PxApplication(QWidget* main_window, bool run_automatically, const std::string& skin_name = "");

    private:
        void Init();
        void RegisterMessageListener();
        void RegisterFirewall();

        // if there isn't a security password, will pop up a dialog for you to input it
        void CheckSecurityPassword();

        // windows messages looping
        void StartWindowsMessagesLooping();

        // load panel companion
        void LoadPanelCompanion();

        // start console client if needed
        void StartConsoleClientIfNeeded();

    private:
        QWidget* main_window_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<WsPanelServer> ws_panel_server_ = nullptr;
        //std::shared_ptr<UdpBroadcaster> udp_broadcaster_ = nullptr;
        std::shared_ptr<PxSystemMonitor> sys_monitor_ = nullptr;
        std::shared_ptr<PxServiceClient> service_client_ = nullptr;
        QTimer* timer_ = nullptr;
        PxSettings* settings_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        // window messages looping
        std::shared_ptr<Thread> win_msg_thread_ = nullptr;
        std::shared_ptr<WinMessageLoop> win_msg_loop_ = nullptr;
        // listen connections info and show the info panel
        std::shared_ptr<PxConnectedManager> px_connected_manager_ = nullptr;

        // render messages processor
        // message from render -> panel
        std::shared_ptr<PxRenderMsgProcessor> rd_msg_processor_ = nullptr;

        // clipboard manager
        std::shared_ptr<ClipboardManager> clipboard_mgr_ = nullptr;

        // is started by OS when logon?
        bool run_automatically_ = false;

        // panel companion
        std::shared_ptr<PanelCompanion> companion_ = nullptr;

        // panel console client
        std::shared_ptr<PxConsoleClient> console_client_ = nullptr;

        // console scanner
        std::shared_ptr<ConsoleScanner> console_scanner_ = nullptr;

        // skin interface
        SkinInterface* skin_ = nullptr;

        // user manager
        std::shared_ptr<PxUserManager> user_mgr_ = nullptr;

        // device manager
        std::shared_ptr<PxDeviceManager> device_mgr_ = nullptr;

        // monitor refresher
        std::shared_ptr<MonitorRefresher> monitor_refresher_ = nullptr;

        // requested skin name from command line
        std::string requested_skin_name_;

        // last console connection info used by PxConsoleClient
        std::string using_appkey_;
        std::string using_console_host_;
        int using_console_port_ = 0;
        bool using_console_ssl_ = true;
        bool shutdown_prepared_ = false;
    };

    extern std::shared_ptr<PxApplication> grApp;

}

#endif //TC_SERVER_STEAM_GR_APPLICATION_H
