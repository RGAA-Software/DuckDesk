//
// Created by RGAA on 2023-12-16.
//

#ifndef TC_APPLICATION_TCAPPLICATION_H
#define TC_APPLICATION_TCAPPLICATION_H

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>
#include "rd_context.h"
#include "app/app_messages.h"
#include "app_global_messages.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/concurrent_type.h"
#include "px_common_new/concurrent_queue.h"
#include "px_common_new/concurrent_hashmap.h"
#ifdef WIN32
#include <d3d11.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

#endif

namespace px
{

    using AppParams = std::unordered_map<std::string, std::string>;

    class Data;
    class Connection;
    class AppManager;
    class EncoderThread;
    class MessageListener;
    class DesktopCapture;
    class Thread;
    class AppTimer;
    class RdSettings;
    class File;
    class AppSharedMessage;
    class IAudioCapture;
    class OpusAudioEncoder;
    class OpusAudioDecoder;
    class ServerCast;
    class AppSharedInfo;
    class Message;
    class CaptureVideoFrame;
    class VigemController;
    class VigemDriverManager;
    class RdStatistics;
    class WsPanelClient;
    class PluginManager;
    class PxMonitorCapturePlugin;
    class PxVideoEncoderPlugin;
    class PxDataProviderPlugin;
    class PxAudioEncoderPlugin;
    class SharedPreference;
    class RenderServiceClient;
    class MonitorRefresher;
    class WinDesktopManager;
    class D3D11DeviceWrapper;
    class WebViewRuntime;
    class MouseEvent;
    class KeyEvent;
    class TextInput;

    class RdApplication : public std::enable_shared_from_this<RdApplication> {
    public:

        static std::shared_ptr<RdApplication> Make(const AppParams& args);

        virtual ~RdApplication();

        virtual void Init(int argc, char** argv);
        virtual int Run();
        virtual void Exit();
        virtual void CaptureControlC() = 0;

        void PostGlobalAppMessage(std::shared_ptr<AppMessage>&& msg);
        void PostGlobalTask(std::function<void()>&& task);
        void PostIpcMessage(std::shared_ptr<Data>&& msg);
        void PostIpcMessage(const std::string& msg) const;
        void PostNetMessage(std::shared_ptr<Data> msg) const;
        std::shared_ptr<RdContext> GetContext() { return context_; }
        std::shared_ptr<AppManager> GetAppManager() { return app_manager_; }
        void OnCapturedVideoFrame(const CaptureVideoFrame& frame) const;
        void OnCapturedAudioFrame(const CaptureAudioFrame& frame);
        void OnCapturedCursorBitmap(const CaptureCursorBitmap& cursor) const;
        void OnIpcVideoFrame(const std::shared_ptr<CaptureVideoFrame>& msg) const;
        // In-process hook audio from px_gh.dll via /ipc.
        void OnIpcAudioFrame(const CaptureAudioFrame& frame);
        // Sync: write file bootstrap for injected DLL (port + DXGI offsets). Not SHM.
        void PrepareGameHookBoot(uint32_t pid);
        void ResetMonitorResolution(const std::string& name, int w, int h);
        std::shared_ptr<PluginManager> GetPluginManager();
        px::PxMonitorCapturePlugin* GetWorkingMonitorCapturePlugin();
        std::map<std::string, PxVideoEncoderPlugin*> GetWorkingVideoEncoderPlugins() const;
        bool GenerateD3DDevice(uint64_t adapter_uid);
        void ClearD3DDevice(uint64_t adapter_uid);
        void ClearPluginD3DState(uint64_t adapter_uid);
        void HandleD3DDeviceFailure(uint64_t adapter_uid, const std::string& reason);
        ComPtr<ID3D11Device> GetD3DDevice(uint64_t adapter_uid);
        ComPtr<ID3D11DeviceContext> GetD3DContext(uint64_t adapter_uid);
        std::shared_ptr<SharedPreference> GetSp() const { return sp_; }
        void ReqCtrlAltDelete(const std::string& device_id, const std::string& stream_id) const;
        void RedeemConnectionTicket(
            const std::string& ticket,
            const std::string& client_nonce,
            const std::string& instance_id,
            std::function<void(bool, const std::string&, const std::vector<std::string>&,
                               const std::string&)>&& callback) const;
        // service 经 ws 下发 kSrvStopServer(Console 停止实例):先广播 kInstanceStopped
        // 给所有 RTC 客户端,留出发送时间后自行退出(不等服务强杀)
        void OnServiceRequestedStop();
        std::shared_ptr<WinDesktopManager> GetDesktopManager();
        // post to panel process
        bool PostPanelMessage(std::shared_ptr<Data> msg);
        void PostUserProxyMessage(std::shared_ptr<Data> msg);

        void HandleForceGdiEvent(bool force_gdi);

        // update capturing monitor
        void UpdateCapturingMonitorInfo();
        void RequestVirtualDisplay(
            const std::string& request_id,
            int operation,
            uint32_t width,
            uint32_t height,
            uint32_t refresh_hz,
            std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback);
        void UpdateVirtualDisplayStatus(const MsgVirtualDisplayServiceResult& result);
        void RefreshVirtualDisplayStatus(const std::string& request_prefix);
        std::pair<uint32_t, uint64_t> GetVirtualDisplayStatusSnapshot() const;
        void SendWebViewMouseEvent(const MouseEvent& event);
        void SendWebViewKeyEvent(const KeyEvent& event);
        void SendWebViewTextInput(const TextInput& event);
        void SendWebViewFocusEvent(bool focused);

    public:
        template<typename T>
        void SendAppMessage(const T& m) {
            context_->SendAppMessage(m);
        }
    protected:
        explicit RdApplication(const AppParams& args);
    private:
        void InitAppTimer();
        void InitMessages();
        void InitAudioCapture();
        void WriteBoostUpInfoForPid(uint32_t pid);
        void StartProcessWithHook();
        void StartProcessWithScreenCapture();
        void StartWebView();
        bool HasConnectedPeer() const;

        // to panel
        void ReportAudioSpectrum2Panel();
        // to clients
        void SendAudioSpectrumMessage() const;
        void SendClipboardMessage(const std::string& msg) const;
        void SendConfigurationBack();
        void RequestRestartMe() const;

        bool SwitchGdiCapture();
        bool SwitchDdaCapture();
        bool IsCurrentGdiCapture();
        bool IsCurrentDdaCapture();
        bool TryInitDdaCapture();

    protected:
        RdSettings* settings_ = nullptr;
        std::shared_ptr<WsPanelClient> ws_panel_client_ = nullptr;
        std::shared_ptr<AppManager> app_manager_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<EncoderThread> encoder_thread_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageListener> state_msg_listener_ = nullptr;
        std::shared_ptr<AppTimer> app_timer_ = nullptr;

        std::shared_ptr<File> debug_encode_file_ = nullptr;
        std::shared_ptr<AppSharedMessage> app_shared_message_ = nullptr;
        std::atomic_bool exit_app_ = false;

        std::shared_ptr<Thread> audio_capture_thread_ = nullptr;
        std::shared_ptr<AppSharedInfo> app_shared_info_ = nullptr;

        uint64_t last_post_audio_time_ = 0;
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        std::shared_ptr<SharedPreference> sp_;

        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
        std::mutex task_mutex_;
        std::queue<std::shared_ptr<AppMessage>> pending_tasks_;
        DWORD main_thread_id_ = 0;
        // working capture plugin
        std::mutex capture_plugin_mtx_;
        px::PxMonitorCapturePlugin* capture_plugin_ = nullptr;
        px::PxMonitorCapturePlugin* gdi_capture_plugin_ = nullptr;
        px::PxMonitorCapturePlugin* dda_capture_plugin_ = nullptr;
        px::PxDataProviderPlugin* data_provider_plugin = nullptr;
        px::PxDataProviderPlugin* audio_capture_plugin_ = nullptr;
        px::PxAudioEncoderPlugin* audio_encoder_plugin_ = nullptr;

        // uint64_t adapter_uid <==> D3D11Device/D3D11DeviceContext
        std::map<uint64_t, std::shared_ptr<D3D11DeviceWrapper>> d3d11_devices_;
        std::map<uint64_t, int> d3d11_device_failure_counts_;

        std::vector<double> fft_left_;
        std::vector<double> fft_right_;

        std::shared_ptr<px::RenderServiceClient> service_client_ = nullptr;

        std::shared_ptr<WinDesktopManager> desktop_mgr_ = nullptr;
        std::unique_ptr<WebViewRuntime> webview_runtime_;

        // timer count
        int64_t timer_count_16ms_ = 0;

        std::atomic<bool> force_gdi_ = false;

        std::atomic_uint32_t restart_counter_ = 0;

        // Incremented whenever the WebRTC client set changes.  Delayed
        // game-hook shutdowns capture this generation so an old disconnect
        // cannot terminate a newly reconnected session.
        std::atomic_uint64_t client_disconnect_generation_ = 0;
        // A stale transport close can be delivered while a game-hook instance
        // is starting. Do not treat it as "the last viewer left" until this
        // instance has observed a real client connection.
        std::atomic_bool game_hook_has_seen_client_ = false;
        // The injected game needs a few seconds to bring up its local web
        // listener. Ignore transport churn during that startup window.
        std::atomic_bool game_hook_startup_grace_complete_ = false;
        // Network plugins can report an old disconnect after a newer client
        // has already connected.  Keep the event connection ids here instead
        // of basing the game lifetime on a transient plugin aggregate.
        mutable std::mutex game_hook_clients_mutex_;
        std::unordered_set<std::string> game_hook_client_ids_;

        std::atomic_bool monitor_changed_ = false;
        std::atomic_uint32_t virtual_display_owned_count_ = 0;
        std::atomic_uint64_t virtual_display_topology_generation_ = 0;
        std::atomic_uint64_t virtual_display_request_sequence_ = 0;
        std::atomic_bool virtual_display_refresh_pending_ = false;
        bool init_failed_ = false;
        std::string init_error_;

    };

    extern std::shared_ptr<RdApplication> rdApp;

    // Windows
    class WinApplication : public RdApplication {
    public:
        ~WinApplication() override;

        int Run() override;
        void Exit() override;
        void CaptureControlC() override;
        void LoadDxAddress();
    protected:
        explicit WinApplication(const AppParams& args);
    };

}

#endif //TC_APPLICATION_TCAPPLICATION_H
