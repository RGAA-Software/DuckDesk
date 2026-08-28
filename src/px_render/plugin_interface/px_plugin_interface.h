//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_PLUGIN_INTERFACE_H
#define PX_PLUGIN_INTERFACE_H

#include <mutex>
#include <atomic>
#include <map>
#include <any>
#include <string>
#include <vector>
#include <functional>
#include <d3d11.h>
#include <mutex>
#include <wrl/client.h>
#include "px_plugin_settings_info.h"
#include "app/app_messages.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/file_transfer_send_result.h"

#ifndef PX_PLUGIN_EXPORT
#if defined(_WIN32)
#define PX_PLUGIN_EXPORT(PluginType) \
extern "C" __declspec(dllexport) void* GetInstance() { \
    static PluginType plugin; \
    return &plugin; \
}
#else
#define PX_PLUGIN_EXPORT(PluginType) \
extern "C" __attribute__((visibility("default"))) void* GetInstance() { \
    static PluginType plugin; \
    return &plugin; \
}
#endif
#endif


namespace px
{

    class Data;
    class Image;
    class PxPluginBaseEvent;
    class PxPluginContext;
    class PxNetPlugin;
    class Message;

    // param
    class PxPluginParam {
    public:
        std::map<std::string, std::any> cluster_;
    };

    // plugin type
    enum class PxPluginType {
        kStream,
        kEncoder,
        kNet,
        kUtil,
    };

    // encoded video type
    enum class PxPluginEncodedVideoType {
        kH264,
        kH265,
        kVp8,
        kVp9,
        kAv1
    };

    enum class PxPluginLifecycleState {
        Created,
        Running,
        Stopping,
        Destroyed,
    };

    // callback
    using PxPluginEventCallback = std::function<void(const std::shared_ptr<PxPluginBaseEvent>&)>;

    // interface
    class PxPluginInterface {
    public:
        PxPluginInterface() = default;
        virtual ~PxPluginInterface() = default;

        PxPluginInterface(const PxPluginInterface&) = delete;
        PxPluginInterface& operator=(const PxPluginInterface&) = delete;

        std::shared_ptr<PxPluginContext> GetPluginContext();

        // info
        virtual std::string GetPluginId() = 0;
        virtual std::string GetPluginName();
        virtual std::string GetPluginAuthor();
        virtual std::string GetPluginDescription();
        virtual PxPluginType GetPluginType();
        virtual bool IsStreamPlugin();

        // version
        virtual std::string GetVersionName();
        virtual uint32_t GetVersionCode();

        // enable
        virtual bool IsPluginEnabled();
        virtual void EnablePlugin();
        virtual void DisablePlugin();

        // working
        virtual bool IsWorking();

        // lifecycle
        virtual bool OnCreate(const PxPluginParam& param);
        virtual bool OnResume();
        virtual bool OnStop();
        virtual bool OnDestroy();
        bool IsStoppingOrDestroyed() const;

        // task
        void PostWorkTask(std::function<void()>&& task);
        void PostUITask(std::function<void()>&& task);
        void PostUIDelayTask(int ms, std::function<void()>&& task);

        // event
        void RegisterEventCallback(const PxPluginEventCallback& cbk);
        void CallbackEvent(const std::shared_ptr<PxPluginBaseEvent>& event);
        void CallbackEventDirectly(const std::shared_ptr<PxPluginBaseEvent>& event);

        virtual void On1Second();

        // key frame
        virtual void InsertIdr();

        virtual void OnCommand(const std::string& command);

        // new client connected
        virtual void OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type);
        // client disconnected
        virtual void OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id);

        // net plugins
        void AttachNetPlugin(const std::string& id, PxNetPlugin* plugin);
        // total plugins
        void AttachPlugin(const std::string& id, PxPluginInterface* plugin);

        //
        bool HasAttachedNetPlugins();
        // Serialized proto message from Renderer
        // to see format details in px_message_new/px_message.proto
        // such as : message VideoFrame { ... }
        // you can send it to any clients
        //                       -> client 1
        // Renderer Messages ->  -> client 2
        //                       -> client 3
        // run_through: send the message even if stream was paused
        // !! Call this function in a NON-NET-PLUGIN !!
        void DispatchAllStreamMessage(std::shared_ptr<Data> msg, bool run_through = false);

        // Serialized proto message from Renderer
        // to a specific stream
        // !! Call this function in a NON-NET-PLUGIN !!
        void DispatchTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through = false);

        // Serialized proto message from Renderer
        // to file transfer
        // !! Call this function in a NON-NET-PLUGIN !!
        void DispatchTargetFileTransferMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through = false);
        [[nodiscard]] FileTransferSendResult DispatchTargetFileTransferMessageOnRoute(
            const std::string& plugin_id,
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through = false,
            const std::string& connection_instance_id = {});

        // messages from remote
        virtual void OnMessage(std::shared_ptr<Message> msg);
        // msg: Parsed messages
        virtual void OnMessageRaw(const std::any& msg);

        std::map<std::string, PxNetPlugin*> GetNetPlugins();
        int64_t GetQueuingMediaMsgCountInNetPlugins();
        int64_t GetQueuingFtMsgCountInNetPlugins();

        // settings from render panel
        // render panel -> render -> plugins
        virtual void OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings);

        // app events
        // render -> plugins
        virtual void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event);

        PxPluginSettingsInfo GetPluginSettingsInfo();

        bool DontHaveConnectedClientsNow();

        // update capturing monitors information
        virtual void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg);

        // stream
        // video
        virtual void OnVideoEncoderCreated(const std::string& mon_name, const PxPluginEncodedVideoType& type, int width, int height) {}

        // data: encode video frame, h264/h265/...
        virtual void OnEncodedVideoFrame(const std::string& mon_name,
                                         const PxPluginEncodedVideoType& video_type,
                                         const std::shared_ptr<Data>& data,
                                         uint64_t frame_index,
                                         int frame_width,
                                         int frame_height,
                                         bool key) {}
        // raw video frame
        // handle: D3D Shared texture handle
        virtual void OnRawVideoFrameSharedTexture(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {}

        // raw video frame in rgba format
        // image: Raw image
        virtual void OnRawVideoFrameRgba(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {}

        // raw video frame in yuv(I420) format
        // image: Raw image
        virtual void OnRawVideoFrameYuv(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {}

        // audio
        virtual void OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {}
        virtual void OnSplitRawAudioData(const std::shared_ptr<Data>& left_ch_data,
                                         const std::shared_ptr<Data>& right_ch_data,
                                         int samples, int channels, int bits) {}
        virtual void OnSplitFFTAudioData(const std::vector<double>& left_fft, const std::vector<double>& right_fft) {}

        PxPluginInterface* GetPluginById(const std::string& plugin_id);
    protected:
        bool HasParam(const std::string& k) {
            return param_.cluster_.count(k) > 0;
        }

        template<typename T>
        T GetConfigParam(const std::string& k) {
            if (param_.cluster_.count(k) > 0) {
                return std::any_cast<T>(param_.cluster_[k]);
            }
            return T{};
        }

        template<typename T>
        bool HoldsType(const std::any& a) {
            return a.type() == typeid(T);
        }

        std::string GetConfigStringParam(const std::string& k) { return GetConfigParam<std::string>(k); }
        int64_t GetConfigIntParam(const std::string& k) { return GetConfigParam<int64_t>(k); }
        bool GetConfigBoolParam(const std::string& k) {return GetConfigParam<bool>(k); }
        double GetConfigDoubleParam(const std::string& k) { return GetConfigParam<double>(k); }

    protected:
        std::shared_ptr<PxPluginContext> plugin_context_ = nullptr;
        std::atomic_bool stopped_ = false;
        std::atomic_bool destroyed_ = false;
        std::atomic<PxPluginLifecycleState> lifecycle_state_ = PxPluginLifecycleState::Created;
        PxPluginParam param_;
        PxPluginEventCallback event_cbk_ = nullptr;
        std::string plugin_file_name_;
        PxPluginType plugin_type_ = PxPluginType::kUtil;
        std::string plugin_author_ = "RGAA";
        std::string plugin_desc_;
        std::string plugin_version_name_ = "1.2.0";
        int64_t plugin_version_code_ = 120;
        bool plugin_enabled_ = true;
        std::string base_path_;
        std::wstring base_data_path_;
        std::string capture_audio_device_id_;
        // active net plugins...
        std::map<std::string, PxNetPlugin*> net_plugins_;
        // total plugins
        std::map<std::string, PxPluginInterface*> total_plugins_;
        // settings
        PxPluginSettingsInfo sys_settings_{};
        // no connected clients counter
        std::atomic_int64_t no_connected_clients_counter_ = 0;

    public:
        // adapter uid <==> D3D11Device
        std::map<uint64_t, Microsoft::WRL::ComPtr<ID3D11Device>> d3d11_devices_;
        // adapter uid <==> D3D11DeviceContext
        std::map<uint64_t, Microsoft::WRL::ComPtr<ID3D11DeviceContext>> d3d11_devices_context_;
    };

    // 编码音频消费端(录制插件等可选实现)。
    // 注意: 刻意不向 PxPluginInterface 添加虚函数——那会改变 PxPluginInterface/
    // PxNetPlugin 的虚表布局, 与旧插件 DLL 二进制不兼容(槽位错位导致崩溃)。
    // 消费端通过 dynamic_cast 检测, 未实现的插件不受影响。
    class PxEncodedAudioSink {
    public:
        virtual ~PxEncodedAudioSink() = default;
        // data: 编码后的音频包(Opus)
        virtual void OnEncodedAudioFrame(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size) = 0;
    };

    // Optional consumer for authorization-gated browser microphone PCM.
    // Kept outside PxPluginInterface to preserve the plugin ABI vtable.
    class PxWebRtcVoicePcmSink {
    public:
        virtual ~PxWebRtcVoicePcmSink() = default;
        virtual void OnWebRtcVoicePcm(
            const std::string& stream_id, const std::string& call_id,
            const int16_t* samples, size_t sample_count,
            int sample_rate, int channels) = 0;
    };

}

#endif //PX_PLUGIN_INTERFACE_H
